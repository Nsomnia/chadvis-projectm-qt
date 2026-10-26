#include <QtTest>

#include "qml_bridge/SunoMutationsSection.hpp"

#include <QSignalSpy>
#include <QVariantMap>

using qml_bridge::SunoMutationsSection;

// ── Why this suite needs no client ───────────────────────────────────────────
// The section refuses an unrecognised `updateType` BEFORE the mutation service
// is consulted, so the refusal is observable with no `SunoController` attached
// and no network stack at all. The recognised members are for the service to
// accept or refuse; with no service they settle nothing, which is the
// signed-out no-op every other mutation verb performs.
//
// What is pinned here is the seam the extraction created: the section owns the
// refusal, the routing and the verdict strings, and the bridge only relays.

namespace {

constexpr const char* kRecognisedTypes[] = {"add", "remove", "remove_by_id", "reorder"};

} // namespace

class TestSunoMutationsSection : public QObject {
    Q_OBJECT

private slots:
    void unattachedSectionIsUnavailableWithASentence() {
        SunoMutationsSection section;
        QVERIFY(!section.available());
        QVERIFY(!section.busy());
        const QString reason = section.unavailableReason();
        QVERIFY(!reason.isEmpty());
        QVERIFY(reason.contains(QLatin1String("signed-in")));
    }

    void unrecognisedUpdateTypeIsRefusedNotGuessed() {
        SunoMutationsSection section;
        QSignalSpy playlistSettled(&section, &SunoMutationsSection::playlistSettled);
        QSignalSpy clipSettled(&section, &SunoMutationsSection::clipSettled);
        QSignalSpy failure(&section, &SunoMutationsSection::failure);

        section.updatePlaylistClips(QStringLiteral("playlist-1"), QStringLiteral("shuffle"), {});

        // Exactly one settled result, on the playlist-scoped channel.
        QCOMPARE(playlistSettled.count(), 1);
        QCOMPARE(clipSettled.count(), 0);
        const QVariantList result = playlistSettled.takeFirst();
        QCOMPARE(result.at(0).toString(), QStringLiteral("playlist-1"));
        QCOMPARE(result.at(1).toString(), QStringLiteral("playlist-update-clips"));
        QCOMPARE(result.at(2).toString(), QStringLiteral("refused"));

        // The refusal is explained, and names the captured members so a QML
        // author can fix the call site without reading C++.
        const QString reason = result.at(3).toString();
        QVERIFY(!reason.isEmpty());
        QVERIFY(reason.contains(QLatin1String("add")));
        QVERIFY(reason.contains(QLatin1String("remove_by_id")));
        QVERIFY(reason.contains(QLatin1String("reorder")));

        // The refusal also gets a visible home, so a control cannot fail
        // silently.
        QCOMPARE(failure.count(), 1);
        QCOMPARE(failure.takeFirst().at(0).toString(), reason);
    }

    void recognisedUpdateTypesSettleNothingWithoutAService() {
        SunoMutationsSection section;
        QSignalSpy playlistSettled(&section, &SunoMutationsSection::playlistSettled);

        for (const char* type : kRecognisedTypes) {
            section.updatePlaylistClips(QStringLiteral("playlist-1"),
                                        QString::fromLatin1(type), {});
            QCOMPARE(playlistSettled.count(), 0);
        }

        // An unmapped member is the one case that settles without a service.
        section.updatePlaylistClips(QStringLiteral("playlist-1"), QStringLiteral("append"), {});
        QCOMPARE(playlistSettled.count(), 1);
        QCOMPARE(playlistSettled.takeFirst().at(2).toString(), QStringLiteral("refused"));
    }

    void everyVerbIsSafeWithoutAService() {
        SunoMutationsSection section;
        QSignalSpy clipSettled(&section, &SunoMutationsSection::clipSettled);
        QSignalSpy playlistSettled(&section, &SunoMutationsSection::playlistSettled);

        // No client, no service, no controller: every verb must be a no-op
        // rather than a crash or a made-up mutation.
        section.setClipVisibility(QStringLiteral("clip-1"), true);
        section.setClipRemixPermission(QStringLiteral("clip-1"), true);
        section.setClipShowRemixes(QStringLiteral("clip-1"), false);
        section.setClipFeedback(QStringLiteral("clip-1"), QStringLiteral("thumbs up"));
        section.shareClip(QStringLiteral("clip-1"), QStringLiteral("clip"));
        section.setPlaylistMetadata(QStringLiteral("playlist-1"), QStringLiteral("name"),
                                    QStringLiteral("description"));
        section.addClipsToPlaylist(QStringLiteral("playlist-1"),
                                   {QStringLiteral("clip-1")});
        section.removeClipsFromPlaylist(QStringLiteral("playlist-1"),
                                        {QStringLiteral("clip-1")});
        section.reorderPlaylistTracks(QStringLiteral("playlist-1"), {0});
        section.setPlaylistCoverImage(QStringLiteral("playlist-1"), QStringLiteral("image-1"));

        QCOMPARE(clipSettled.count(), 0);
        QCOMPARE(playlistSettled.count(), 0);
    }

    void detachWithoutAServiceIsSafe() {
        SunoMutationsSection section;
        QSignalSpy availability(&section, &SunoMutationsSection::availabilityChanged);
        QSignalSpy busy(&section, &SunoMutationsSection::busyChanged);

        section.detach();
        section.detach();

        // Nothing was ever created, so nothing changed to announce.
        QCOMPARE(availability.count(), 0);
        QCOMPARE(busy.count(), 0);
    }
};

int runTestSunoMutationsSection(int argc, char** argv) {
    TestSunoMutationsSection test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_SunoMutationsSection.moc"
