/*
 * ChadVis - ProjectM 4.0 Qt Frontend
 * Copyright (c) 2026 Nsomnia
 */

#pragma once

#include <QObject>
#include <QString>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "audio/AudioEngine.hpp"
#include "core/ConfigData.hpp"
#include "suno/DownloadQueue.hpp"
#include "suno/SunoClient.hpp"
#include "suno/SunoDatabase.hpp"
#include "suno/SunoModels.hpp"
#include "util/Result.hpp"

namespace fs = std::filesystem;

class QJsonDocument;

namespace vc::suno {

/// What the caller wants done once the bytes are on disk.
///
/// Saving and playing are separate operations because they are separate
/// decisions: a batch fetch must not hijack whatever the user is listening
/// to, while the single-clip "download and play it" gesture must keep
/// working exactly as it always has.
enum class DownloadAction : int {
    /// Save, tag and sidecar the file. Never touches the playlist or the
    /// transport. This is the default: saving is the useful primitive and
    /// playing is the exception.
    SaveOnly = 0,
    /// Save, then append the file to the playlist and select it.
    SaveAndPlay = 1,
};

/// A download request that was accepted. Bytes are moving unless
/// `reusedOnDisk` says the file was already there.
struct DownloadStarted {
    std::string clipId;
    DownloadAction action{DownloadAction::SaveOnly};
    bool reusedOnDisk{false};
};

/// High-level download orchestration: decides destinations, feeds the shared
/// DownloadQueue, then tags/sidecars finished files. All network transfer
/// mechanics (retry/resume/concurrency) live in DownloadQueue.
///
/// ── The save/play seam ───────────────────────────────────────────────────────
/// `download()` saves; `downloadAndPlay()` saves and then plays. The two used
/// to be the same function -- `processDownloadedFile()` was a one-line alias
/// for `addAndPlay()`, called unconditionally from the completion hook, so
/// fetching N clips to disk fired N `jumpTo()` calls and audibly played the
/// batch. The completion hook now reads the requested action out of the
/// pending record instead of assuming playback.
class SunoDownloader : public QObject {
    Q_OBJECT

public:
    /// Production constructor. The queue adopts the controller-provided
    /// manager; ad-hoc per-download managers are banned.
    explicit SunoDownloader(SunoClient* client,
                            SunoDatabase& db,
                            AudioEngine* audioEngine,
                            QNetworkAccessManager* networkManager,
                            QObject* parent = nullptr);

    /// Test seam: replies come from the injected factory only, so a transfer
    /// can be driven to completion without a network. Mirrors DownloadQueue's
    /// own two-constructor split, which the downloader previously hid.
    SunoDownloader(SunoDatabase& db, AudioEngine* audioEngine, ReplyFactory replyFactory);
    ~SunoDownloader() override;

    /// Save a clip to disk. Returns an error only for the fail-closed
    /// decisions made before any byte moves; a transfer that later fails is
    /// reported through downloadStateChanged, not through this return.
    [[nodiscard]] std::expected<DownloadStarted, QString>
    download(const SunoClip& clip, DownloadAction action = DownloadAction::SaveOnly);

    /// Unchanged contract: save (reusing an already-downloaded file) and then
    /// play it. Kept as its own entry point because SunoController and the
    /// single-clip "play" gesture both mean exactly this.
    void downloadAndPlay(const SunoClip& clip);

    /// Abort a live transfer and drop its queued work. The queue removes the
    /// .part file, so nothing partial is ever visible at the destination and
    /// no saved-file notification is emitted.
    [[nodiscard]] bool cancelDownload(const std::string& clipId);

    [[nodiscard]] static bool isSupportedMediaUrl(const QString& url);
    [[nodiscard]] static std::optional<std::string>
    selectDownloadUrl(const SunoClip& clip, vc::SunoDownloadFormat format);
    void saveLyricsSidecar(const std::string& clipId,
                           const std::string& json,
                           const QJsonDocument& doc,
                           const std::vector<SunoClip>& clips);
    void saveMetadataSidecar(const SunoClip& clip);

    // Embedded tagging functionality
    void tagAudioFile(const fs::path& path, const SunoClip& clip);

signals:
    /// Forwarded from the DownloadQueue for controller/bridge progress wiring.
    void downloadStateChanged(const QString& clipId, int state, int progressPercent);
    void downloadQueueIdle();
    /// The file exists on disk and its tags and sidecars are written. Emitted
    /// for every completed transfer and for a file that was already present,
    /// regardless of the requested DownloadAction. This is the per-clip
    /// result channel: a batch must not have to be read out of a single
    /// aggregate status string.
    void fileSaved(const QString& clipId, const QString& path);
    void playbackReady(const QString& clipId);

private:
    SunoDatabase& db_;
    AudioEngine* audioEngine_;
    std::unique_ptr<DownloadQueue> queue_;

    struct PendingDownload {
        SunoClip clip;
        fs::path destPath;
        DownloadAction action{DownloadAction::SaveOnly};
    };

    /// Clip payloads awaiting their queue completion hook, keyed by clip id.
    std::unordered_map<std::string, PendingDownload> pendingClips_;

    /// Destination handed to each clip, sticky for this process's lifetime so
    /// the transfer, the tagger, and both sidecar writers always agree on one
    /// path. Paired with destOwner_ to break title ties.
    std::unordered_map<std::string, fs::path> destByClip_;
    std::unordered_map<std::string, std::string> destOwner_;

    /// Sanitize a title and fall back to the clip id when the result is empty.
    [[nodiscard]] static std::string safeStem(std::string_view title, const std::string& clipId);

    void wireQueue();
    void enqueueAudio(const SunoClip& clip, const std::string& url, DownloadAction action);
    void handleItemState(const std::string& clipId, DownloadState state);
    [[nodiscard]] std::expected<DownloadStarted, QString>
    finishAlreadyOnDisk(const SunoClip& clip, const fs::path& path, DownloadAction action);
    bool addAndPlay(const fs::path& path, const std::string& clipId);

    /// Destination for a clip, claiming one on first use. Free of any
    /// filesystem probe, so it answers the same before and after the bytes
    /// land; see the .cpp for why that matters.
    [[nodiscard]] fs::path resolveDestPath(const SunoClip& clip);
    /// Where a clip is, or would be, saved. Claims nothing: used by the
    /// sidecar writers, which run for clips this downloader never fetched.
    [[nodiscard]] fs::path defaultDestPathFor(const std::string& clipId) const;
    [[nodiscard]] static QString noUsableMediaMessage(const SunoClip& clip);

    [[nodiscard]] fs::path getDownloadDir() const;
};

} // namespace vc::suno
