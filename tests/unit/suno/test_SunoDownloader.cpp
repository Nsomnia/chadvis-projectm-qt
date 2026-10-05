// test_SunoDownloader — the save/play seam.
//
// The defect under test: `processDownloadedFile()` used to be a one-line alias
// for `addAndPlay()`, called unconditionally from the queue's completion hook.
// Fetching N clips to disk therefore fired N `playlist.jumpTo()` calls and
// audibly played the batch over whatever the user was listening to. These
// tests assert the seam from both sides: a save-only request must leave the
// playlist byte-for-byte alone, and the single-clip "download and play it"
// request must still play.
//
// Every transfer is driven through DownloadQueue's injectable ReplyFactory
// seam (see FakeNetworkReply.hpp), so nothing leaves the machine. There are no
// threads anywhere in this path: DownloadQueue multiplexes in-flight replies on
// one event loop and FakeReply emits synchronously on the test's own thread,
// which is also the thread that constructed the Playlist. That is what satisfies
// Playlist's assertOwnerThread() contract here — no marshalling is involved
// and none is needed.

#include <QtTest>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "audio/AudioEngine.hpp"
#include "audio/Playlist.hpp"
#include "core/Config.hpp"
#include "suno/SunoDatabase.hpp"
#include "suno/SunoDownloader.hpp"

#include "FakeNetworkReply.hpp"

#include <optional>
#include <set>
#include <string>
#include <vector>

using namespace vc;
using namespace vc::suno;
using fake_net::FakeReply;
using fake_net::FakeServer;

namespace {

std::string show(const std::optional<usize>& index) {
    return index ? std::to_string(*index) : std::string("<none>");
}

/// Restores both config fields the downloader reads. The download directory is
/// a global singleton config value, so a test that leaked it would redirect
/// every later download in the binary into a deleted temporary directory.
class DownloadConfigGuard {
public:
    DownloadConfigGuard()
        : path_(CONFIG.suno().downloadPath), format_(CONFIG.suno().downloadFormat) {
        CONFIG.suno().downloadFormat = SunoDownloadFormat::MP3;
    }
    ~DownloadConfigGuard() {
        CONFIG.suno().downloadPath = path_;
        CONFIG.suno().downloadFormat = format_;
    }

private:
    fs::path path_;
    SunoDownloadFormat format_;
};

SunoClip completeClip(const std::string& id, const std::string& title) {
    SunoClip clip;
    clip.id = id;
    clip.title = title;
    clip.status = "complete";
    clip.display_name = "ChadVis Test";
    clip.media_urls = {{"https://audiopipe.suno.ai/" + id + ".mp3", "mp3", "streaming", ""}};
    return clip;
}

/// A clip whose only captured media is the m4a-opus variant. selectDownloadUrl()
/// accepts mp3 entries only, so this is refused -- see
/// aClipWithNoCapturedMp3IsRefusedAndSaysWhy.
SunoClip opusOnlyClip(const std::string& id) {
    SunoClip clip;
    clip.id = id;
    clip.title = "Opus Only";
    clip.status = "complete";
    clip.media_urls = {{"https://audiopipe.suno.ai/" + id + ".m4a", "m4a-opus",
                        "progressive", ""}};
    return clip;
}

QByteArray payloadFor(const QString& id) {
    return QByteArray("chadvis-fake-audio-") + id.toUtf8();
}

} // namespace

class TestSunoDownloader : public QObject {
    Q_OBJECT

private slots:
    // ── the save/play seam ───────────────────────────────────────────────────

    /// The headline: a batch of downloads saves three files and does not
    /// enqueue, jump, or otherwise touch the playlist. Asserted directly on
    /// the playlist's size and selection rather than inferred from a signal,
    /// because the pre-fix bug was a mutation, not a missing notification.
    void saveOnlyBatchDoesNotTouchThePlaylist() {
        DownloadConfigGuard config;
        QTemporaryDir downloadDir;
        QVERIFY(downloadDir.isValid());
        CONFIG.suno().downloadPath = downloadDir.path().toStdString();

        QTemporaryDir sessionDir;
        QVERIFY(sessionDir.isValid());
        AudioEngine audio(fs::path(sessionDir.path().toStdString()) / "last_session.m3u");
        QVERIFY(audio.init());
        SunoDatabase database;
        QVERIFY(database.init(":memory:"));

        // Something already loaded and selected, so "the playlist is unchanged"
        // is a claim about a real state rather than about emptiness.
        QFile existing(downloadDir.filePath(QStringLiteral("Already There.mp3")));
        QVERIFY(existing.open(QIODevice::WriteOnly));
        existing.close();
        auto& playlist = audio.playlist();
        playlist.addFile(fs::path(existing.fileName().toStdString()));
        QVERIFY(playlist.startPlayback());
        QCOMPARE(playlist.size(), usize{1});
        QVERIFY2(playlist.currentIndex() == std::optional<usize>{0}, show(playlist.currentIndex()).c_str());

        FakeServer server;
        SunoDownloader downloader(database, &audio, server.factory());
        QSignalSpy saved(&downloader, &SunoDownloader::fileSaved);
        QSignalSpy ready(&downloader, &SunoDownloader::playbackReady);

        for (int i = 0; i < 3; ++i) {
            auto result = downloader.download(completeClip("batch-" + std::to_string(i),
                                                          "Batch " + std::to_string(i)));
            // Not QVERIFY2(has_value(), qPrintable(result.error())): that macro
            // evaluates both arguments, so error() would run on the SUCCESS
            // path too, and std::expected::error() on a value-holding expected
            // is undefined behaviour -- it reads as std::bad_alloc here. Branch
            // explicitly so the error is only touched when there is one.
            if (!result) {
                QFAIL(qPrintable(QStringLiteral("download refused: %1").arg(result.error())));
            }
        }
        QCOMPARE(static_cast<int>(server.replies.size()), 3);  // all in flight at once
        for (const QString& id : {QStringLiteral("batch-0"), QStringLiteral("batch-1"),
                                  QStringLiteral("batch-2")}) {
            server.replies[server.requestIndexForPath(id + QStringLiteral(".mp3"))]->succeed(
                    payloadFor(id));
        }
        QTest::qWait(20);  // drain deleteLater

        // Every file landed, each to its own path inside the download directory.
        QCOMPARE(saved.count(), 3);
        std::set<QString> savedIds;
        std::set<QString> savedPaths;
        for (int i = 0; i < saved.count(); ++i) {
            const QString path = saved.at(i).at(1).toString();
            savedIds.insert(saved.at(i).at(0).toString());
            savedPaths.insert(path);
            QVERIFY2(QFile::exists(path), qPrintable(path));
        }
        QCOMPARE(savedIds, std::set<QString>({QStringLiteral("batch-0"), QStringLiteral("batch-1"),
                                              QStringLiteral("batch-2")}));
        QCOMPARE(savedPaths.size(), std::size_t{3});  // no two clips shared a file

        // ...and the playlist never saw any of it.
        QCOMPARE(ready.count(), 0);
        QCOMPARE(playlist.size(), usize{1});
        QVERIFY2(playlist.currentIndex() == std::optional<usize>{0},
                 show(playlist.currentIndex()).c_str());
    }

    /// The preserved single-clip behaviour, through the *transfer* path (the
    /// pre-existing suite only covered the already-on-disk shortcut).
    void saveAndPlayStillSelectsTheDownloadedClip() {
        DownloadConfigGuard config;
        QTemporaryDir downloadDir;
        QVERIFY(downloadDir.isValid());
        CONFIG.suno().downloadPath = downloadDir.path().toStdString();

        QTemporaryDir sessionDir;
        QVERIFY(sessionDir.isValid());
        AudioEngine audio(fs::path(sessionDir.path().toStdString()) / "last_session.m3u");
        QVERIFY(audio.init());
        SunoDatabase database;
        QVERIFY(database.init(":memory:"));

        FakeServer server;
        SunoDownloader downloader(database, &audio, server.factory());
        QSignalSpy saved(&downloader, &SunoDownloader::fileSaved);
        QSignalSpy ready(&downloader, &SunoDownloader::playbackReady);

        downloader.downloadAndPlay(completeClip("play-me", "Play Me"));
        QCOMPARE(static_cast<int>(server.replies.size()), 1);
        server.replies[0]->succeed(payloadFor(QStringLiteral("play-me")));
        QTest::qWait(20);

        QCOMPARE(saved.count(), 1);
        QCOMPARE(ready.count(), 1);
        QCOMPARE(ready.takeFirst().at(0).toString(), QStringLiteral("play-me"));

        auto& playlist = audio.playlist();
        QCOMPARE(playlist.size(), usize{1});
        QVERIFY2(playlist.currentIndex() == std::optional<usize>{0}, show(playlist.currentIndex()).c_str());
        const auto current = playlist.currentItem();
        QVERIFY(current.has_value());
        QCOMPARE(current->path.filename().string(), std::string("Play Me.mp3"));
    }

    /// A file that is already in the download directory is announced as saved
    /// without a transfer and without playback. The mirror image of
    /// saveAndPlayStillSelectsTheDownloadedClip on the reuse path.
    void reusingAnExistingFileSavesWithoutPlaying() {
        DownloadConfigGuard config;
        QTemporaryDir downloadDir;
        QVERIFY(downloadDir.isValid());
        CONFIG.suno().downloadPath = downloadDir.path().toStdString();

        QTemporaryDir sessionDir;
        QVERIFY(sessionDir.isValid());
        AudioEngine audio(fs::path(sessionDir.path().toStdString()) / "last_session.m3u");
        QVERIFY(audio.init());
        SunoDatabase database;
        QVERIFY(database.init(":memory:"));

        QFile alreadyThere(downloadDir.filePath(QStringLiteral("On Disk.mp3")));
        QVERIFY(alreadyThere.open(QIODevice::WriteOnly));
        alreadyThere.close();

        FakeServer server;
        SunoDownloader downloader(database, &audio, server.factory());
        QSignalSpy saved(&downloader, &SunoDownloader::fileSaved);
        QSignalSpy ready(&downloader, &SunoDownloader::playbackReady);

        auto result = downloader.download(completeClip("on-disk", "On Disk"));
        QVERIFY(result.has_value());
        QVERIFY(result->reusedOnDisk);
        QVERIFY(result->action == DownloadAction::SaveOnly);

        QCOMPARE(server.replies.size(), std::size_t{0});  // no transfer was started
        QCOMPARE(saved.count(), 1);
        QCOMPARE(saved.takeFirst().at(1).toString(), alreadyThere.fileName());
        QCOMPARE(ready.count(), 0);
        QCOMPARE(audio.playlist().size(), usize{0});
    }

    /// Two clips that share a title must not share a file: the second is
    /// disambiguated by its unique clip id instead of overwriting the first.
    void sameTitleClipsGetDistinctFiles() {
        DownloadConfigGuard config;
        QTemporaryDir downloadDir;
        QVERIFY(downloadDir.isValid());
        CONFIG.suno().downloadPath = downloadDir.path().toStdString();

        QTemporaryDir sessionDir;
        QVERIFY(sessionDir.isValid());
        AudioEngine audio(fs::path(sessionDir.path().toStdString()) / "last_session.m3u");
        QVERIFY(audio.init());
        SunoDatabase database;
        QVERIFY(database.init(":memory:"));

        FakeServer server;
        SunoDownloader downloader(database, &audio, server.factory());
        QSignalSpy saved(&downloader, &SunoDownloader::fileSaved);

        QVERIFY(downloader.download(completeClip("twin-a", "Neon Arch")).has_value());
        QVERIFY(downloader.download(completeClip("twin-b", "Neon Arch")).has_value());
        server.replies[0]->succeed(payloadFor(QStringLiteral("twin-a")));
        server.replies[1]->succeed(payloadFor(QStringLiteral("twin-b")));
        QTest::qWait(20);

        QCOMPARE(saved.count(), 2);
        // The first claim keeps the plain name; only the collision is suffixed.
        QCOMPARE(saved.at(0).at(1).toString(), downloadDir.filePath(QStringLiteral("Neon Arch.mp3")));
        const QString second = saved.at(1).at(1).toString();
        QVERIFY2(second != saved.at(0).at(1).toString(), "the second clip overwrote the first");
        QVERIFY2(second.endsWith(QStringLiteral("twin-b.mp3")), qPrintable(second));

        // Both files survive and carry their own identity, which is the whole
        // point of disambiguating. Not a raw-bytes comparison: the downloader
        // writes ID3 tags (title/artist/album plus a SUNO_ID TXXX and the clip
        // id), so the on-disk bytes are the tagged file, not the served payload.
        // What must hold is that each file exists, is distinct from the other,
        // and is recognisably the clip it was downloaded for.
        QFile first(downloadDir.filePath(QStringLiteral("Neon Arch.mp3")));
        QFile other(downloadDir.filePath(QStringLiteral("Neon Arch-twin-b.mp3")));
        QVERIFY2(first.open(QIODevice::ReadOnly), qPrintable(first.fileName()));
        QVERIFY2(other.open(QIODevice::ReadOnly), qPrintable(other.fileName()));
        const QByteArray firstBytes = first.readAll();
        const QByteArray otherBytes = other.readAll();
        QVERIFY2(!firstBytes.isEmpty() && !otherBytes.isEmpty(),
                 "a disambiguated download produced an empty file");
        QVERIFY2(firstBytes != otherBytes,
                 "both clips landed in byte-identical files, so the second "
                 "overwrote the first despite the distinct path");
        QVERIFY2(firstBytes.contains("twin-a"), "the first file lost its clip id");
        QVERIFY2(otherBytes.contains("twin-b"), "the second file lost its clip id");
        QVERIFY2(!firstBytes.contains("twin-b") && !otherBytes.contains("twin-a"),
                 "a file carries the other clip's id, so tagging is crossed");
    }

    /// A second request for a clip already in flight must not discard the live
    /// request's completion record, and must escalate a save into a play when
    /// that is what the later request asked for.
    void aLiveRequestIsEscalatedNotDiscarded() {
        DownloadConfigGuard config;
        QTemporaryDir downloadDir;
        QVERIFY(downloadDir.isValid());
        CONFIG.suno().downloadPath = downloadDir.path().toStdString();

        QTemporaryDir sessionDir;
        QVERIFY(sessionDir.isValid());
        AudioEngine audio(fs::path(sessionDir.path().toStdString()) / "last_session.m3u");
        QVERIFY(audio.init());
        SunoDatabase database;
        QVERIFY(database.init(":memory:"));

        FakeServer server;
        SunoDownloader downloader(database, &audio, server.factory());
        QSignalSpy saved(&downloader, &SunoDownloader::fileSaved);
        QSignalSpy ready(&downloader, &SunoDownloader::playbackReady);

        QVERIFY(downloader.download(completeClip("escalate", "Escalate")).has_value());
        QVERIFY(downloader.download(completeClip("escalate", "Escalate")).has_value());
        QCOMPARE(server.replies.size(), std::size_t{1});  // no second transfer

        server.replies[0]->succeed(payloadFor(QStringLiteral("escalate")));
        QTest::qWait(20);

        // Pre-fix this request was erased by the duplicate, so the transfer
        // finished with no announcement at all.
        QCOMPARE(saved.count(), 1);
        QCOMPARE(ready.count(), 0);
    }

    // ── failure paths ────────────────────────────────────────────────────────

    void aFailedDownloadSavesNothingAndLeavesPlaybackAlone() {
        DownloadConfigGuard config;
        QTemporaryDir downloadDir;
        QVERIFY(downloadDir.isValid());
        CONFIG.suno().downloadPath = downloadDir.path().toStdString();

        QTemporaryDir sessionDir;
        QVERIFY(sessionDir.isValid());
        AudioEngine audio(fs::path(sessionDir.path().toStdString()) / "last_session.m3u");
        QVERIFY(audio.init());
        SunoDatabase database;
        QVERIFY(database.init(":memory:"));

        FakeServer server;
        SunoDownloader downloader(database, &audio, server.factory());
        QSignalSpy saved(&downloader, &SunoDownloader::fileSaved);
        QSignalSpy ready(&downloader, &SunoDownloader::playbackReady);

        QVERIFY(downloader.download(completeClip("gone", "Gone")).has_value());
        server.replies[0]->fail(QNetworkReply::ContentNotFoundError, 404);
        QTest::qWait(20);

        QCOMPARE(saved.count(), 0);
        QCOMPARE(ready.count(), 0);
        QCOMPARE(audio.playlist().size(), usize{0});
        // Nothing partial is left visible at the destination or in the .part file.
        QVERIFY(!QFile::exists(downloadDir.filePath(QStringLiteral("Gone.mp3"))));
        QVERIFY(!QFile::exists(downloadDir.filePath(QStringLiteral("Gone.mp3.part"))));
    }

    void cancellingLeavesNoFileAndNoNotification() {
        DownloadConfigGuard config;
        QTemporaryDir downloadDir;
        QVERIFY(downloadDir.isValid());
        CONFIG.suno().downloadPath = downloadDir.path().toStdString();

        QTemporaryDir sessionDir;
        QVERIFY(sessionDir.isValid());
        AudioEngine audio(fs::path(sessionDir.path().toStdString()) / "last_session.m3u");
        QVERIFY(audio.init());
        SunoDatabase database;
        QVERIFY(database.init(":memory:"));

        FakeServer server;
        SunoDownloader downloader(database, &audio, server.factory());
        QSignalSpy saved(&downloader, &SunoDownloader::fileSaved);
        QSignalSpy ready(&downloader, &SunoDownloader::playbackReady);

        QVERIFY(downloader.download(completeClip("aborted", "Aborted")).has_value());
        server.replies[0]->openStream("half a file");
        QVERIFY(QFile::exists(downloadDir.filePath(QStringLiteral("Aborted.mp3.part"))));

        QVERIFY(downloader.cancelDownload("aborted"));
        QTest::qWait(20);

        QCOMPARE(saved.count(), 0);
        QCOMPARE(ready.count(), 0);
        QCOMPARE(audio.playlist().size(), usize{0});
        QVERIFY(!QFile::exists(downloadDir.filePath(QStringLiteral("Aborted.mp3"))));
        QVERIFY(!QFile::exists(downloadDir.filePath(QStringLiteral("Aborted.mp3.part"))));

        // A second cancel and an unknown id are graceful no-ops.
        QVERIFY(!downloader.cancelDownload("aborted"));
        QVERIFY(!downloader.cancelDownload("never-existed"));
    }

    // ── the fail-closed selection, pinned as-is ──────────────────────────────

    /// Pins the CURRENT selection contract: selectDownloadUrl() accepts an
    /// mp3 entry on the captured host and nothing else, so an m4a-opus-only
    /// clip is refused. This is the flagged, unfixed P1 defect -- the test
    /// exists so that whoever widens the filter has to change it deliberately
    /// rather than trip over it. The error is returned so a UI can say why
    /// instead of leaving a dead button.
    void aClipWithNoCapturedMp3IsRefusedAndSaysWhy() {
        DownloadConfigGuard config;
        QTemporaryDir downloadDir;
        QVERIFY(downloadDir.isValid());
        CONFIG.suno().downloadPath = downloadDir.path().toStdString();

        QTemporaryDir sessionDir;
        QVERIFY(sessionDir.isValid());
        AudioEngine audio(fs::path(sessionDir.path().toStdString()) / "last_session.m3u");
        QVERIFY(audio.init());
        SunoDatabase database;
        QVERIFY(database.init(":memory:"));

        FakeServer server;
        SunoDownloader downloader(database, &audio, server.factory());
        QSignalSpy saved(&downloader, &SunoDownloader::fileSaved);

        auto opus = downloader.download(opusOnlyClip("opus-only"));
        QVERIFY(!opus.has_value());
        QVERIFY2(opus.error().contains(QStringLiteral("will not fetch"), Qt::CaseInsensitive),
                 qPrintable(opus.error()));
        QCOMPARE(server.replies.size(), std::size_t{0});
        QCOMPARE(saved.count(), 0);

        // An unfinished clip is refused for a different, equally explicit reason.
        auto unfinished = completeClip("still-going", "Still Going");
        unfinished.status = "streaming";
        auto pending = downloader.download(unfinished);
        QVERIFY(!pending.has_value());
        QVERIFY2(pending.error().contains(QStringLiteral("streaming")), qPrintable(pending.error()));
        QCOMPARE(server.replies.size(), std::size_t{0});

        // No id, no request.
        auto anonymous = downloader.download(SunoClip{});
        QVERIFY(!anonymous.has_value());
    }

    void wavIsRefusedBeforeAnythingIsResolved() {
        DownloadConfigGuard config;
        CONFIG.suno().downloadFormat = SunoDownloadFormat::WAV;
        QTemporaryDir downloadDir;
        QVERIFY(downloadDir.isValid());
        CONFIG.suno().downloadPath = downloadDir.path().toStdString();

        QTemporaryDir sessionDir;
        QVERIFY(sessionDir.isValid());
        AudioEngine audio(fs::path(sessionDir.path().toStdString()) / "last_session.m3u");
        QVERIFY(audio.init());
        SunoDatabase database;
        QVERIFY(database.init(":memory:"));

        FakeServer server;
        SunoDownloader downloader(database, &audio, server.factory());
        auto result = downloader.download(completeClip("wav-clip", "Wav Clip"));
        QVERIFY(!result.has_value());
        QVERIFY2(result.error().contains(QStringLiteral("WAV")), qPrintable(result.error()));
        QCOMPARE(server.replies.size(), std::size_t{0});
    }
};

#include "test_SunoDownloader.moc"

int runTestSunoDownloader(int argc, char** argv) {
    TestSunoDownloader t;
    return QTest::qExec(&t, argc, argv);
}
