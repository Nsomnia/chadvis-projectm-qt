#include <QtTest>

#include "suno/SunoDatabase.hpp"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlRecord>
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

/// Install a BEFORE INSERT trigger that aborts inserts of one clip id, so a
/// multi-clip sync fails mid-batch the way a bad payload would. The trigger
/// only faults the statement; the batch's own rollback owns atomicity.
bool failInsertsOf(const QString& path, const char* triggerName, const char* clipId) {
    auto probe = openProbe(path);
    QSqlQuery query(probe);
    return query.exec(QStringLiteral("CREATE TRIGGER %1 BEFORE INSERT ON clips "
                                     "WHEN NEW.id = '%2' "
                                     "BEGIN SELECT RAISE(ABORT, 'forced sync failure'); END")
                              .arg(triggerName, clipId));
}

/// Build a pre-migration database file: the old minimal clips schema at
/// user_version 0 with one row. SunoDatabase::init must upgrade it in place;
/// the row is the evidence nothing was dropped.
bool createLegacyDatabase(const QString& path) {
    constexpr auto kLegacyConnection = "suno_db_test_legacy";
    if (QSqlDatabase::contains(kLegacyConnection)) {
        QSqlDatabase::removeDatabase(kLegacyConnection);
    }
    auto db = QSqlDatabase::addDatabase("QSQLITE", kLegacyConnection);
    db.setDatabaseName(path);
    if (!db.open()) return false;
    QSqlQuery query(db);
    const bool ok = query.exec("CREATE TABLE clips (id TEXT PRIMARY KEY, title TEXT, "
                               "created_at TEXT, duration TEXT)")
                 && query.exec("INSERT INTO clips (id, title, created_at, duration) "
                               "VALUES ('legacy1', 'Legacy Song', "
                               "'2026-01-01T00:00:00Z', '65.0')");
    db.close();
    db = QSqlDatabase();
    QSqlDatabase::removeDatabase(kLegacyConnection);
    return ok;
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
    void syncUpsertRoundTripsTitleTagsAndArtworkFields();
    void failedSyncRollsBackWithoutPartialRows();
    void failedTransactionPreservesExistingData();
    void migrationUpgradesSchemaWithoutDroppingRows();
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

void TestSunoDatabase::syncUpsertRoundTripsTitleTagsAndArtworkFields() {
    QTemporaryDir dir;
    SunoDatabase db;
    QVERIFY(db.init(dir.filePath("sync.db").toStdString()).isOk());

    // First sync inserts the clip with its original fields.
    SunoClip first = makeClip("sync1", "Rough Mix", "2026-09-03T10:00:00Z");
    first.metadata.tags = "draft, drums";
    first.image_url = "https://cdn.example/art-small.png";
    first.image_large_url = "https://cdn.example/art-large.png";
    QVERIFY(db.saveClips({first}).isOk());

    // A re-sync of the same id must update in place, not duplicate a row.
    SunoClip synced = makeClip("sync1", "Final Mix", "2026-09-03T10:00:00Z");
    synced.metadata.tags = "mastered, synthwave";
    synced.image_url = "https://cdn.example/art-small-v2.png";
    synced.image_large_url = "https://cdn.example/art-large-v2.png";
    QVERIFY(db.saveClips({synced}).isOk());

    auto stored = db.getClip("sync1");
    QVERIFY(stored.isOk());
    QVERIFY(stored.value().has_value());
    const SunoClip& roundTripped = stored.value().value();
    QVERIFY2(roundTripped.title == "Final Mix", "the sync did not update the title");
    QVERIFY2(roundTripped.metadata.tags == "mastered, synthwave",
             "the sync did not update the tags");
    QVERIFY2(roundTripped.image_url == "https://cdn.example/art-small-v2.png",
             "the sync did not update the artwork");
    QVERIFY2(roundTripped.image_large_url == "https://cdn.example/art-large-v2.png",
             "the sync did not update the large artwork");
    QCOMPARE(db.getAllClips().value().size(), std::size_t{1});
}

void TestSunoDatabase::failedSyncRollsBackWithoutPartialRows() {
    QTemporaryDir dir;
    SunoDatabase db;
    QVERIFY(db.init(dir.filePath("rollback.db").toStdString()).isOk());
    QVERIFY(failInsertsOf(dir.filePath("rollback.db"), "poison_rollback", "poison1"));

    const auto failed = db.saveClips({
        makeClip("good1", "Good Song", "2026-09-04T10:00:00Z"),
        makeClip("poison1", "Poison Song", "2026-09-04T11:00:00Z"),
    });
    QVERIFY2(failed.isErr(), "the poisoned batch returned success");

    QCOMPARE(db.getAllClips().value().size(), std::size_t{0});
    auto missing = db.getClip("good1");
    QVERIFY(missing.isOk());
    QVERIFY2(!missing.value().has_value(),
             "the pre-failure insert survived the rollback");
}

void TestSunoDatabase::failedTransactionPreservesExistingData() {
    QTemporaryDir dir;
    SunoDatabase db;
    QVERIFY(db.init(dir.filePath("preserve.db").toStdString()).isOk());
    QVERIFY(db.saveClip(makeClip("anchor", "Original", "2026-09-05T10:00:00Z")).isOk());
    QVERIFY(failInsertsOf(dir.filePath("preserve.db"), "poison_preserve", "poison2"));

    SunoClip clobber = makeClip("anchor", "Overwritten", "2026-09-05T10:00:00Z");
    clobber.metadata.tags = "clobbered";
    const auto failed = db.saveClips({
        clobber,
        makeClip("poison2", "Poison Song", "2026-09-05T11:00:00Z"),
        makeClip("aftermath", "Trailing Song", "2026-09-05T12:00:00Z"),
    });
    QVERIFY2(failed.isErr(), "the batch with the poisoned clip returned success");

    auto stored = db.getClip("anchor");
    QVERIFY(stored.isOk());
    QVERIFY(stored.value().has_value());
    QVERIFY2(stored.value().value().title == "Original",
             "the rollback leaked the in-batch update");
    QVERIFY2(stored.value().value().metadata.tags == "electronic, chill",
             "the rollback leaked the in-batch tag update");
    QCOMPARE(db.getAllClips().value().size(), std::size_t{1});
}

void TestSunoDatabase::migrationUpgradesSchemaWithoutDroppingRows() {
    QTemporaryDir dir;
    const QString path = dir.filePath("legacy.db");
    QVERIFY2(createLegacyDatabase(path), "legacy fixture failed");

    SunoDatabase db;
    QVERIFY2(db.init(path.toStdString()).isOk(), "init must migrate the legacy file");

    auto stored = db.getClip("legacy1");
    QVERIFY(stored.isOk());
    QVERIFY2(stored.value().has_value(), "the legacy row vanished during migration");
    QVERIFY2(stored.value().value().title == "Legacy Song",
             "the legacy title did not survive migration");
    QVERIFY2(stored.value().value().metadata.duration == "01:05",
             "the x.x duration was not converted to mm:ss");

    {
        auto probe = openProbe(path);
        QCOMPARE(pragmaText(probe, "PRAGMA user_version"), QStringLiteral("3"));
        const QSqlRecord record = probe.record(QStringLiteral("clips"));
        QVERIFY2(record.indexOf(QStringLiteral("image_large_url")) != -1,
                 "the v1 backfill did not add image_large_url");
        QVERIFY2(record.indexOf(QStringLiteral("play_count")) != -1,
                 "the v2 backfill did not add play_count");
        QVERIFY2(record.indexOf(QStringLiteral("media_urls")) != -1,
                 "the v3 backfill did not add media_urls");
    }
}

#include "test_SunoDatabase.moc"

int runTestSunoDatabase(int argc, char** argv) {
    TestSunoDatabase tc;
    return QTest::qExec(&tc, argc, argv);
}
