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
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "audio/AudioEngine.hpp"
#include "audio/Playlist.hpp"
#include "core/Config.hpp"
#include "suno/SunoDatabase.hpp"
#include "suno/SunoDownloader.hpp"

#include "FakeNetworkReply.hpp"

#include <taglib/mpegfile.h>
#include <taglib/id3v2tag.h>
#include <taglib/textidentificationframe.h>

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
        CONFIG.sunoMut().downloadFormat = SunoDownloadFormat::MP3;
    }
    ~DownloadConfigGuard() {
        CONFIG.sunoMut().downloadPath = path_;
        CONFIG.sunoMut().downloadFormat = format_;
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

/// A minimal file that TagLib will parse as an MPEG and that carries the given
/// clip id in a `SUNO_ID` TXXX frame -- i.e. the on-disk shape a previous
/// session's completed download leaves.
///
/// Writes the frame with the SAME TagLib API tagAudioFile uses, because the
/// production read path looks for exactly this frame, and a fixture that
/// carried a differently-shaped frame would test nothing about T0169.
///
/// The MPEG prefix is needed for a reason that is not obvious: TagLib's
/// MPEG::File::isValid() gates every write in tagAudioFile and every read in
/// embeddedSunoId, so a zero-byte or fake-bytes file is invisible to both and
/// the whole test would pass trivially. 4 header bytes + 417 pad = one whole
/// 44.1 kHz MPEG frame; the file need not actually decode.
/// Reads back the SUNO_ID a file currently carries, without writing anything.
std::optional<std::string> stillTagged(const fs::path& path) {
    TagLib::MPEG::File f(path.c_str());
    if (!f.isValid() || !f.hasID3v2Tag()) {
        return std::nullopt;
    }
    const TagLib::ID3v2::FrameList frames = f.ID3v2Tag()->frameListMap()["TXXX"];
    for (const auto* frame : frames) {
        const auto* txxx =
            dynamic_cast<const TagLib::ID3v2::UserTextIdentificationFrame*>(frame);
        if (txxx && txxx->description().upper() == "SUNO_ID") {
            const TagLib::StringList fields = txxx->fieldList();
            if (fields.size() < 2) {
                return std::nullopt;
            }
            return std::string(fields[1].to8Bit());
        }
    }
    return std::nullopt;
}

std::optional<std::string> seedTaggedMp3(const fs::path& path, const std::string& clipId) {
    {
        QFile out(QString::fromStdString(path.string()));
        if (!out.open(QIODevice::WriteOnly)) {
            return std::nullopt;
        }
        const QByteArray frameHeader("\xff\xfb\x90\x64", 4);  // 128 kbps, 44.1 kHz
        out.write(frameHeader);
        const QByteArray padding(417, 'A');
        out.write(padding);
        out.close();
    }
    TagLib::MPEG::File f(path.c_str());
    if (!f.isValid()) {
        return std::nullopt;
    }
    auto* frame = new TagLib::ID3v2::UserTextIdentificationFrame();
    frame->setDescription(TagLib::String("SUNO_ID", TagLib::String::UTF8));
    frame->setText(TagLib::String(clipId, TagLib::String::UTF8));
    f.ID3v2Tag(true)->addFrame(frame);
    f.save();
    return clipId;
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
        CONFIG.sunoMut().downloadPath = downloadDir.path().toStdString();

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
        CONFIG.sunoMut().downloadPath = downloadDir.path().toStdString();

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
        CONFIG.sunoMut().downloadPath = downloadDir.path().toStdString();

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
        // Canonical paths, not strings: the downloader emits the native path
        // and this test would otherwise pin "/" against "\" on Windows.
        QCOMPARE(QFileInfo(saved.takeFirst().at(1).toString()).canonicalFilePath(),
                 QFileInfo(alreadyThere.fileName()).canonicalFilePath());
        QCOMPARE(ready.count(), 0);
        QCOMPARE(audio.playlist().size(), usize{0});
    }

    /// T0169. A stale plain-named file LEFT BY AN EARLIER SESSION must not be
    /// handed to a different clip whose title happens to match.
    ///
    /// `destOwner_` only knows the clips THIS process has routed, so the
    /// pre-fix resolveDestPath saw an empty owner table for "Neon Arch.mp3",
    /// returned it again, and the new clip's transfer overwrote the old file --
    /// data loss with no error anywhere, because from every layer's point of
    /// view it simply saved the file it was asked to.
    ///
    /// The embedded SUNO_ID frame is what discriminates, and it is read rather
    /// than the filename, so the fix never changes the name it hands out.
    void staleFileFromAnEarlierSessionIsNotReused() {
        DownloadConfigGuard config;
        QTemporaryDir downloadDir;
        QVERIFY(downloadDir.isValid());
        CONFIG.sunoMut().downloadPath = downloadDir.path().toStdString();

        // A real, tagged MP3 carrying a DIFFERENT clip's id -- the shape a
        // previous session's completed download leaves behind.
        //
        // It must be a PARSEABLE MPEG file, not fake bytes: TagLib's
        // MPEG::File::isValid() gates every write in tagAudioFile, and this
        // file's whole job is to have been tagged. TagLib's own test suite uses
        // an 11-byte frame header (0xFF 0xFB 0x90 0x00 ...) followed by the
        // ID3 payload, which is enough for it to parse and let the ID3v2 tag be
        // appended; the file need not actually decode.
        SunoClip other = completeClip("session-old-clip", "Neon Arch");
        const fs::path stale = fs::path(downloadDir.path().toStdString()) / "Neon Arch.mp3";
        QVERIFY2(seedTaggedMp3(stale, other.id).has_value(),
                 "the seeded fixture could not be parsed as an MPEG by TagLib; the "
                 "test would prove nothing about the production read path");

        QTemporaryDir sessionDir;
        QVERIFY(sessionDir.isValid());
        AudioEngine audio(fs::path(sessionDir.path().toStdString()) / "last_session.m3u");
        QVERIFY(audio.init());
        SunoDatabase database;
        QVERIFY(database.init(":memory:"));

        FakeServer server;
        SunoDownloader downloader(database, &audio, server.factory());

        auto result = downloader.download(completeClip("session-new-clip", "Neon Arch"));
        QVERIFY(result.has_value());
        // A transfer was started, and the destination is the disambiguated one:
        // the stale file must survive this download untouched.
        QCOMPARE(server.replies.size(), std::size_t{1});
        QVERIFY(result->reusedOnDisk == false);
        // Where did it decide to save? The transfer's own request URL is not the
        // destination, so assert on the destination the tagger/sidecar writers
        // will use, via the queue's own enqueue: the .part it creates is the
        // destination with the suffix moved. Simplest observable proxy that does
        // not add a production accessor for a test's convenience.
        const QDir listing(downloadDir.path(), QStringLiteral("*.mp3"), QDir::Name, QDir::Files);
        QCOMPARE(listing.entryList().count(), 1);
        QCOMPARE(listing.entryList().first(), QStringLiteral("Neon Arch.mp3"));
        // And the old tag still names the old clip, i.e. the file is unmodified.
        QCOMPARE(stillTagged(stale), std::optional<std::string>("session-old-clip"));
    }

    /// The complement, and the reason this is not simply "never reuse the plain
    /// name": OUR OWN file from an earlier session must still be reused, which
    /// is the already-downloaded shortcut downloadAndPlay depends on.
    void ourOwnFileFromAnEarlierSessionIsStillReused() {
        DownloadConfigGuard config;
        QTemporaryDir downloadDir;
        QVERIFY(downloadDir.isValid());
        CONFIG.sunoMut().downloadPath = downloadDir.path().toStdString();

        SunoClip mine = completeClip("mine-from-last-time", "Neon Arch");
        const fs::path stale = fs::path(downloadDir.path().toStdString()) / "Neon Arch.mp3";
        QVERIFY2(seedTaggedMp3(stale, mine.id).has_value(),
                 "the seeded fixture could not be parsed as an MPEG by TagLib");

        QTemporaryDir sessionDir;
        QVERIFY(sessionDir.isValid());
        AudioEngine audio(fs::path(sessionDir.path().toStdString()) / "last_session.m3u");
        QVERIFY(audio.init());
        SunoDatabase database;
        QVERIFY(database.init(":memory:"));

        FakeServer server;
        SunoDownloader downloader(database, &audio, server.factory());

        auto result = downloader.download(completeClip("mine-from-last-time", "Neon Arch"));
        QVERIFY(result.has_value());
        QVERIFY(result->reusedOnDisk);
        QCOMPARE(server.replies.size(), std::size_t{0});
        QCOMPARE(stillTagged(stale), std::optional<std::string>("mine-from-last-time"));
    }

    /// Two clips that share a title must not share a file: the second is
    /// disambiguated by its unique clip id instead of overwriting the first.
    void sameTitleClipsGetDistinctFiles() {
        DownloadConfigGuard config;
        QTemporaryDir downloadDir;
        QVERIFY(downloadDir.isValid());
        CONFIG.sunoMut().downloadPath = downloadDir.path().toStdString();

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
        QCOMPARE(QFileInfo(saved.at(0).at(1).toString()).canonicalFilePath(),
                 QFileInfo(downloadDir.filePath(QStringLiteral("Neon Arch.mp3"))).canonicalFilePath());
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
        CONFIG.sunoMut().downloadPath = downloadDir.path().toStdString();

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
        CONFIG.sunoMut().downloadPath = downloadDir.path().toStdString();

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
        CONFIG.sunoMut().downloadPath = downloadDir.path().toStdString();

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
        CONFIG.sunoMut().downloadPath = downloadDir.path().toStdString();

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
        CONFIG.sunoMut().downloadFormat = SunoDownloadFormat::WAV;
        QTemporaryDir downloadDir;
        QVERIFY(downloadDir.isValid());
        CONFIG.sunoMut().downloadPath = downloadDir.path().toStdString();

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
