#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace vc {
namespace suno {
class SunoClient;
class SunoController;
class SunoLibraryMutations;
} // namespace suno
} // namespace vc

namespace qml_bridge {

/// The library-and-playlist mutation surface: one service's lifetime, the
/// eleven captured verbs, and the routing of a settled result.
///
/// It exists so `SunoBridge` keeps one job per feature. The service itself
/// (`vc::suno::SunoLibraryMutations`) still owns every request body, so this
/// class cannot drift from the captured wire format — and there is still
/// exactly one owner of that format.
///
/// What this layer adds and nothing else:
///   * service lifetime, torn down on sign-out;
///   * routing a settled result to the clip-scoped or playlist-scoped signal;
///   * turning "nothing is attached" into a sentence instead of silence.
///
/// It deliberately does NOT assemble JSON, validate enum members that were never
/// captured, or write anything to the local database.
class SunoMutationsSection : public QObject {
    Q_OBJECT

public:
    explicit SunoMutationsSection(QObject* parent = nullptr);
    ~SunoMutationsSection() override;

    /// Points the section at a controller and its client. The gate resolver is
    /// re-pointed rather than re-created: it lives inside `SunoAccountManager`,
    /// which is replaced on every sign-in, so a pointer captured at construction
    /// would dangle.
    void attach(vc::suno::SunoController* controller, vc::suno::SunoClient* client);

    /// Drops the service: an object that outlives the account must not keep a
    /// resolver pointer into a destroyed `SunoAccountManager`.
    void detach();

    /// The resolver's verdict moved. The availability pair changed; the busy
    /// flag did not, so `busyChanged` is deliberately not emitted — a NOTIFY
    /// with no value change would make QML bindings re-read for nothing.
    void onGatesChanged();

    bool available() const;
    QString unavailableReason() const;
    bool busy() const;

    void setClipVisibility(const QString& clipId, bool isPublic);
    void setClipRemixPermission(const QString& clipId, bool canRemix);
    void setClipShowRemixes(const QString& clipId, bool showRemix);
    void setClipFeedback(const QString& clipId, const QString& reason);
    void shareClip(const QString& clipId, const QString& contentType);
    void setPlaylistMetadata(const QString& playlistId, const QString& name,
                             const QString& description);
    void updatePlaylistClips(const QString& playlistId, const QString& updateType,
                             const QVariantMap& metadata);
    void addClipsToPlaylist(const QString& playlistId, const QStringList& clipIds);
    void removeClipsFromPlaylist(const QString& playlistId, const QStringList& clipIds);
    void reorderPlaylistTracks(const QString& playlistId, const QVariantList& positions);
    void setPlaylistCoverImage(const QString& playlistId, const QString& imageId);

signals:
    void availabilityChanged();
    void busyChanged();
    void clipSettled(const QString& clipId, const QString& verb, const QString& outcome,
                     const QString& reason);
    void playlistSettled(const QString& playlistId, const QString& verb, const QString& outcome,
                         const QString& reason);
    /// One `accepted` result: the library must be re-read, because nothing was
    /// parsed out of the response and nothing was written locally.
    void libraryRefreshRecommended();
    /// A refusal or rejection that deserves a visible home, or the control
    /// just silently does nothing.
    void failure(const QString& reason);

private:
    vc::suno::SunoLibraryMutations* ensureService();
    void ensureServiceForController();
    void destroyService();
    void settle(int verb, const QString& subjectId, int outcome, const QString& reason);

    vc::suno::SunoController* controller_{nullptr};
    vc::suno::SunoClient* client_{nullptr};
    vc::suno::SunoLibraryMutations* service_{nullptr};
};

} // namespace qml_bridge
