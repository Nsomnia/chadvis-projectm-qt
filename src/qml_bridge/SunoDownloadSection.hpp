#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

namespace vc {
namespace suno {
class SunoController;
class SunoDownloader;
} // namespace suno
} // namespace vc

namespace qml_bridge {

/// The download-and-play surface: the borrow of the one downloader, the
/// per-clip status, the save-vs-play latch and the batch refusal accounting.
///
/// It exists so `SunoBridge` keeps one job per feature. `vc::suno::SunoDownloader`
/// still owns every request and every byte written to disk, so there is still
/// exactly one owner of the transfer.
///
/// What this layer adds and nothing else:
///   * the borrow of the downloader, re-resolved when the controller changes;
///   * the latch that keeps a save-only batch from being reported as playback;
///   * the sentences the user reads while a transfer runs.
///
/// The clip title is asked of the bridge through a provider callback: the
/// library cache is the bridge's, and this class must not keep a second copy.
class SunoDownloadSection : public QObject {
    Q_OBJECT

public:
    explicit SunoDownloadSection(QObject* parent = nullptr);
    ~SunoDownloadSection() override;

    /// Points the section at a controller and re-resolves the downloader. The
    /// old controller is left first, so a swap cannot leave a borrow pointing
    /// at a destroyed controller.
    void attach(vc::suno::SunoController* controller);

    /// Drops the borrow and the latches. Nothing here may outlive the
    /// controller it was resolved from.
    void detach();

    /// The bridge answers with the clip's title. Without it the status
    /// sentences would say "clip", which is true and useless.
    void setTitleProvider(std::function<QString(const QString&)> provider);

    QString status() const;
    QString activeClipId() const;

    /// Requests playback. The controller owns the transport; this class owns
    /// the bookkeeping that says whether a finished transfer was a play or a
    /// save.
    void playClip(const QString& clipId);

    /// Save-only by construction: nothing in this path can enqueue, jump, or
    /// otherwise touch the transport, so a batch cannot hijack playback.
    void downloadClip(const QString& clipId);
    void downloadClips(const QStringList& clipIds);

signals:
    void statusChanged();
    /// The bridge owns the two message channels, so the section asks for a
    /// message instead of writing into another object's state.
    void statusMessageRequested(const QString& message);
    void errorMessageRequested(const QString& message);
    void clipSaved(const QString& clipId, const QString& savedPath);
    void clipSaveRefused(const QString& clipId, const QString& reason);
    void clipDownloadSettled(const QString& clipId, int state, const QString& reason);

private:
    vc::suno::SunoDownloader* ensureDownloader();
    void setStatusForClip(const QString& clipId, const QString& message);
    void onDownloadStateChanged(const QString& clipId, int state, int percent);
    void downloadInto(const QStringList& clipIds);
    [[nodiscard]] QString titleFor(const QString& clipId) const;

    vc::suno::SunoController* controller_{nullptr};
    vc::suno::SunoDownloader* downloader_{nullptr};
    std::function<QString(const QString&)> titleProvider_;
    QString status_;
    QString activeClipId_;
    /// The clip whose download was requested as "save and play". Kept apart
    /// from `activeClipId_`, which is the single status slot and is latched by
    /// any download event: only an explicit play request may suppress other
    /// clips' events or be reported as "now playing".
    QString playRequestClipId_;
};

} // namespace qml_bridge
