#include <QtTest>

#include "suno/SunoDatabase.hpp"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>

using namespace vc::suno;

namespace {

/// The connection name init() registers — the process-wide contract these
/// tests pin from the outside, because the handle itself is private.
constexpr auto kConnectionName = "suno_db";
constexpr auto kProbeConnection = "suno_db_test_probe";

/// A second connection to the same file, to observe the pragmas and the
/// schema as any other process would. The previous probe's registration is
/// reclaimed here; the local handle released at the caller's scope end is
/// what makes the next removal safe.
QSqlDatabase openProbe(const QString& path) {
    if (QSqlDatabase::contains(kProbeConnection)) {
        QSqlDatabase::removeDatabase(kProbeConnection);
    }
    auto db = QSqlDatabase::addDatabase("QSQLITE", kProbeConnection);
    db.setDatabaseName(path);
    db.open();
    return db;
}

SunoClip makeClip(const char* id, const char* title, const char* createdAt) {
    SunoClip clip;
    clip.id = id;
    clip.title = title;
    clip.status = "complete";
    clip.created_at = createdAt;
    clip.metadata.prompt = "a generated prompt";
    clip.metadata.tags = "electronic, chill";
    return clip;
}

QString pragmaText(QSqlDatabase& db, const char* statement) {
    QSqlQuery query(db);
    if (!query.exec(statement) || !query.next()) return {};
    return query.value(0).toString();
}

} // namespace

// SunoDatabase had zero tests (T0100's largest untested class). These pin the
// registration lifecycle (contains-guard, removeDatabase-on-destroy), the
// durability pragmas, the sort index, and the round trips the library UI
// depends on — all against a temp file, never the real app-support DB.
class TestSunoDatabase : public QObject {
    Q_OBJECT

private slots:
    void initEnablesWalSetsBusyTimeoutAndCreatesTheIndex();
    void inMemoryDatabasesInitWithoutWal();
    void aSecondConnectionInTheSameProcessIsRefused();
    void theRegistrationDiesWithTheObject();
    void uninitializedOperationsAreRefused();
    void clipsRoundTripAndSearch();
};

void TestSunoDatabase::initEnablesWalSetsBusyTimeoutAndCreatesTheIndex() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    SunoDatabase db;
    QVERIFY2(db.init(dir.filePath("lib.db").toStdString()).isOk(), "init failed");

    {
        auto probe = openProbe(dir.filePath("lib.db"));
        QCOMPARE(pragmaText(probe, "PRAGMA journal_mode"), QStringLiteral("wal"));
        QCOMPARE(pragmaText(probe, "PRAGMA busy_timeout"), QStringLiteral("5000"));

        QSqlQuery indexQuery(probe);
        QVERIFY(indexQuery.exec("SELECT name FROM sqlite_master WHERE type='index' "
                                "AND name='idx_clips_created_at'"));
        QVERIFY2(indexQuery.next(), "the created_at index is missing");
    }
}

void TestSunoDatabase::inMemoryDatabasesInitWithoutWal() {
    // SQLite cannot put an in-memory database into WAL mode (the pragma
    // returns "memory" unchanged), so init() must skip the WAL demand for
    // ":memory:" rather than fail it — TestSunoLibraryManager's ":memory:"
    // databases depend on this, and its blocked-worker teardown turns any
    // early test failure into a 300-second hang.
    SunoDatabase db;
    QVERIFY2(db.init(":memory:").isOk(), ":memory: init must not demand WAL");
    QVERIFY(db.saveClips({makeClip("m1", "Memory Only", "2026-09-03T00:00:00Z")}).isOk());
    QCOMPARE(db.getAllClips().value().size(), std::size_t{1});
}

void TestSunoDatabase::aSecondConnectionInTheSameProcessIsRefused() {
    QTemporaryDir dirA;
    QTemporaryDir dirB;

    SunoDatabase first;
    QVERIFY(first.init(dirA.filePath("a.db").toStdString()).isOk());

    // The object that owns the live registration stays alive for this whole
    // test, so the guard must refuse the second init outright.
    SunoDatabase second;
    const auto refused = second.init(dirB.filePath("b.db").toStdString());
    QVERIFY2(refused.isErr(), "init() re-registered a live connection");
}

void TestSunoDatabase::theRegistrationDiesWithTheObject() {
    QTemporaryDir dir;
    QVERIFY(!QSqlDatabase::contains(kConnectionName)); // earlier tests cleaned up
    {
        SunoDatabase db;
        QVERIFY(db.init(dir.filePath("c.db").toStdString()).isOk());
    }
    QVERIFY2(!QSqlDatabase::contains(kConnectionName),
             "the destructor left the connection registered");
}

void TestSunoDatabase::uninitializedOperationsAreRefused() {
    SunoDatabase db;
    QVERIFY(db.getAllClips().isErr());
    QVERIFY(db.saveClips({makeClip("x", "X", "2026-09-01T00:00:00Z")}).isErr());
}

void TestSunoDatabase::clipsRoundTripAndSearch() {
    QTemporaryDir dir;
    SunoDatabase db;
    QVERIFY(db.init(dir.filePath("d.db").toStdString()).isOk());

    const std::vector<SunoClip> clips = {
            makeClip("older", "Chillwave Night", "2026-09-01T10:00:00Z"),
            makeClip("newer", "Bass Drop Dawn", "2026-09-02T10:00:00Z"),
    };
    QVERIFY(db.saveClips(clips).isOk());

    auto all = db.getAllClips();
    QVERIFY(all.isOk());
    QCOMPARE(all.value().size(), std::size_t{2});
    QVERIFY2(all.value().front().id == "newer", "getAllClips is not created_at DESC");
    QVERIFY2(all.value().front().metadata.tags == "electronic, chill",
             "clip metadata did not round-trip");

    auto found = db.searchClips("bass drop");
    QVERIFY(found.isOk());
    QCOMPARE(found.value().size(), std::size_t{1});
    QVERIFY2(found.value().front().id == "newer", "searchClips missed the title match");

    QVERIFY(db.saveAlignedLyrics("older", "{\"lines\":[]}").isOk());
    QVERIFY(db.hasLyrics("older"));
    QVERIFY(!db.hasLyrics("newer"));
    QVERIFY2(db.getAlignedLyrics("older").value() == "{\"lines\":[]}",
             "aligned lyrics did not round-trip");
}

#include "test_SunoDatabase.moc"

int runTestSunoDatabase(int argc, char** argv) {
    TestSunoDatabase tc;
    return QTest::qExec(&tc, argc, argv);
}
