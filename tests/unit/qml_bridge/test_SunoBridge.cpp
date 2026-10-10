#include <QtTest>

#include "qml_bridge/SunoBridge.hpp"

#include <QSignalSpy>
#include <QVariantMap>

using qml_bridge::SunoBridge;

// ── Why this suite needs no client ───────────────────────────────────────────
// `updatePlaylistClips` refuses an unrecognised `updateType` BEFORE the
// mutation service is consulted, so the refusal is observable with no
// `SunoController` attached and no network stack at all: a fresh bridge has
// null `s_controller`/`s_client`, `ensureMutations()` can create nothing, and
// the only signal that fires is the local refusal. That ordering is the fix:
// the previous code checked the service first and then fell through to
// `PlaylistUpdateType::Add` with empty metadata — a real `add` mutation for a
// request the caller never made, and one that produced no signal at all when
// no service existed, which is why nothing caught it.
//
// The two facts pinned here:
//   1. A bad `updateType` settles as `refused` on `playlistMutationSettled`
//      (never on `clipMutationSettled`), with the playlist id it was about and
//      a reason naming the four captured members.
//   2. The four captured members produce no bridge-local refusal — they are
//      for the service to accept or refuse, not for this boundary to guess at.

namespace {

constexpr const char* kRecognisedTypes[] = {"add", "remove", "remove_by_id", "reorder"};

} // namespace

class TestSunoBridge : public QObject {
    Q_OBJECT

private slots:
    void unrecognisedUpdateTypeIsRefusedNotGuessed() {
        SunoBridge bridge;
        QVERIFY(bridge.errorMessage().isEmpty());
        QSignalSpy playlistSettled(&bridge, &SunoBridge::playlistMutationSettled);
        QSignalSpy clipSettled(&bridge, &SunoBridge::clipMutationSettled);
        QSignalSpy errorMessage(&bridge, &SunoBridge::errorMessageChanged);

        bridge.updatePlaylistClips(QStringLiteral("playlist-1"), QStringLiteral("shuffle"), {});

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

        // The single emit path also gives the refusal a visible home.
        QCOMPARE(errorMessage.count(), 1);
    }

    void recognisedUpdateTypesReachTheServiceUnrefused() {
        SunoBridge bridge;
        QSignalSpy playlistSettled(&bridge, &SunoBridge::playlistMutationSettled);

        // No controller is attached, so no mutation service can exist and a
        // recognised type settles nothing here — the signed-out no-op every
        // other mutation invokable performs. The refusal for an unrecognised
        // type still fires, which is only possible if it happens before the
        // service rather than through it.
        for (const char* type : kRecognisedTypes) {
            bridge.updatePlaylistClips(QStringLiteral("playlist-1"), QString::fromLatin1(type), {});
            QCOMPARE(playlistSettled.count(), 0);
        }

        bridge.updatePlaylistClips(QStringLiteral("playlist-1"), QStringLiteral("append"), {});
        QCOMPARE(playlistSettled.count(), 1);
        QCOMPARE(playlistSettled.takeFirst().at(2).toString(), QStringLiteral("refused"));
    }
};

int runTestSunoBridge(int argc, char** argv) {
    TestSunoBridge test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_SunoBridge.moc"
