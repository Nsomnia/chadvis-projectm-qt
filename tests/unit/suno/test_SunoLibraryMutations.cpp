#include <QtTest>

#include "suno/SunoLibraryMutations.hpp"

#include <QJsonArray>

// Pins the UI-07 trash contract (ENDPOINT-INVENTORY §4.12): the two new
// verbs' flat bodies and path templates, and their fail-closed refusals.
// No client is attached, so this also pins that a disabled service refuses
// before anything is sent.

using namespace vc::suno;

class TestSunoLibraryMutations : public QObject {
    Q_OBJECT

private slots:
    void clipTrashBodyIsFlatAndNamed() {
        const QJsonObject trash = SunoLibraryMutations::clipTrashBody(
            {QStringLiteral("clip-1"), QStringLiteral("clip-2")}, true);
        QCOMPARE(trash.size(), 2);
        QVERIFY(trash.value(QStringLiteral("trash")).toBool());
        const QJsonArray ids = trash.value(QStringLiteral("clip_ids")).toArray();
        QCOMPARE(ids.size(), 2);
        QCOMPARE(ids.at(0).toString(), QStringLiteral("clip-1"));

        const QJsonObject restore = SunoLibraryMutations::clipTrashBody(
            {QStringLiteral("clip-1")}, false);
        QVERIFY(!restore.value(QStringLiteral("trash")).toBool());
        QCOMPARE(restore.value(QStringLiteral("clip_ids")).toArray().size(), 1);
    }

    void playlistTrashBodyCarriesUndo() {
        const QJsonObject trash = SunoLibraryMutations::playlistTrashBody(false);
        QCOMPARE(trash.size(), 1);
        QVERIFY(!trash.value(QStringLiteral("undo")).toBool());

        const QJsonObject recover = SunoLibraryMutations::playlistTrashBody(true);
        QVERIFY(recover.value(QStringLiteral("undo")).toBool());
    }

    void endpointTemplatesAreCapturedPaths() {
        QCOMPARE(SunoLibraryMutations::endpointFor(MutationVerb::PlaylistTrashV2,
                                                   QStringLiteral("playlist-1")),
                 QStringLiteral("/playlist/v2/playlist-1/trash"));
        // `gen/trash` is a collection route: no id segment may be invented.
        QVERIFY(SunoLibraryMutations::endpointFor(MutationVerb::ClipTrashRestore,
                                                  QStringLiteral("clip-1"))
                    .isEmpty());
    }

    void emptyBatchIsRefusedLocally() {
        SunoLibraryMutations service(nullptr);
        QVERIFY(!service.isEnabled());
        QVERIFY(!service.setClipsTrashed({}, true));
        QVERIFY(!service.setClipsTrashed({QStringLiteral("")}, true));
        QVERIFY(!service.setPlaylistTrashed({}, true));
    }

    void validArgumentsRefuseWhileDisabled() {
        SunoLibraryMutations service(nullptr);
        QVERIFY(!service.setClipsTrashed({QStringLiteral("clip-1")}, true));
        QVERIFY(!service.setPlaylistTrashed(QStringLiteral("playlist-1"), true));
        QVERIFY(!service.isAvailable());
    }

    void verbNamesAreDistinctAndStable() {
        QCOMPARE(QString::fromLatin1(toString(MutationVerb::ClipTrashRestore)),
                 QStringLiteral("clip-trash-restore"));
        QCOMPARE(QString::fromLatin1(toString(MutationVerb::PlaylistTrashV2)),
                 QStringLiteral("playlist-trash-v2"));
    }
};

#include "test_SunoLibraryMutations.moc"

int runTestSunoLibraryMutations(int argc, char** argv) {
    TestSunoLibraryMutations tc;
    return QTest::qExec(&tc, argc, argv);
}
