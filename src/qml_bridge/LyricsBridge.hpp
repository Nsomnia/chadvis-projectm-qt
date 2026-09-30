#pragma once

#include <QObject>
#include <cstddef>
#include <optional>
#include <QtQml/qqml.h>
#include <QVariantList>
#include <QVariantMap>
#include "lyrics/LyricsData.hpp"
#include "lyrics/LyricsSync.hpp"
#include "QmlSingletonBridge.hpp"

namespace vc {
class AudioEngine;
}

namespace qml_bridge {

class LyricsBridge : public QObject,
                     public QmlSingletonBridge<LyricsBridge> {

// The CRTP mixin constructs this singleton via its private
// constructor; grant only the exact instantiation access.
friend class QmlSingletonBridge<LyricsBridge>;
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool hasLyrics READ hasLyrics NOTIFY lyricsChanged)
    Q_PROPERTY(QVariantList lines READ lines NOTIFY lyricsChanged)
    Q_PROPERTY(int currentLineIndex READ currentLineIndex NOTIFY positionChanged)
    Q_PROPERTY(int currentWordIndex READ currentWordIndex NOTIFY positionChanged)
    Q_PROPERTY(qreal lineProgress READ lineProgress NOTIFY positionChanged)
    Q_PROPERTY(qreal wordProgress READ wordProgress NOTIFY positionChanged)
    Q_PROPERTY(bool isInstrumental READ isInstrumental NOTIFY positionChanged)
    Q_PROPERTY(QString title READ title NOTIFY lyricsChanged)
    Q_PROPERTY(QString artist READ artist NOTIFY lyricsChanged)
    Q_PROPERTY(QString searchQuery READ searchQuery WRITE setSearchQuery NOTIFY searchQueryChanged)
    Q_PROPERTY(QVariantList searchResults READ searchResults NOTIFY searchResultsChanged)

public:
    explicit LyricsBridge(QObject* parent = nullptr);
    ~LyricsBridge() override;

    static void setLyricsSync(vc::LyricsSync* sync);
    static void setAudioEngine(vc::AudioEngine* engine);
    static void connectSignals();

    bool hasLyrics() const;
    QVariantList lines() const;
    int currentLineIndex() const;
    int currentWordIndex() const;
    qreal lineProgress() const;
    qreal wordProgress() const;
    bool isInstrumental() const;
    QString title() const;
    QString artist() const;
    QString searchQuery() const;
    QVariantList searchResults() const;

    void setSearchQuery(const QString& query);

public slots:
    Q_INVOKABLE void seekToLine(int lineIndex);
    Q_INVOKABLE void exportToSrt(const QString& path);
    Q_INVOKABLE void exportToLrc(const QString& path);
    /// Advanced SubStation Alpha, the karaoke subtitle format.
    ///
    /// One Dialogue event per LyricsLine, with every word tagged `\kf<cs>` so a
    /// player sweeps the line in time with the audio.
    ///
    /// The document itself is assembled by `vc::LyricsExport::toAssDocument`, one
    /// layer down, because the recorder muxes the same bytes into a subtitle
    /// stream; this method owns the *file* -- resolving the path and reporting
    /// the outcome -- and nothing more.
    Q_INVOKABLE void exportToAss(const QString& path);
    /// The same ASS document exportToAss writes, as a string.
    ///
    /// The muxer needs the content, not a filename to read back, so this is the
    /// seam it consumes. It is the *same* bytes by construction: both entry
    /// points call one `vc::LyricsExport::toAssDocument`, and there is no second
    /// assembly path to drift.
    ///
    /// Failure is reported through `error` rather than the exportFailed signal,
    /// on purpose. That signal means "a file could not be written" and is wired
    /// to the QML error surface; a caller that merely wants the bytes to mux has
    /// not failed at anything, so firing it would put a spurious error in front
    /// of the user. An empty return is unambiguous here because a successful
    /// document always carries the three section headers and so is never empty
    /// -- an invariant assDocumentIsNeverEmptyOnSuccess pins.
    Q_INVOKABLE QString assDocument(QString* error = nullptr) const;
    Q_INVOKABLE QVariantMap getLine(int index) const;
    /// The `count` lines strictly after the cached currentLineIndex_, ascending.
    ///
    /// `count` is an int because QML speaks int here, so it is signed and can
    /// be negative or near INT_MAX; a count <= 0 yields an empty list. The
    /// window is measured as a distance to the last line, so an oversized count
    /// returns the rest of the song rather than overflowing. With no active
    /// line the window starts at line 0 -- unlike getContextLines, which anchors
    /// there and includes it as the current line. See the comment in the .cpp.
    Q_INVOKABLE QVariantList getUpcomingLines(int count) const;
    /// Window of [center-before, center+after] inclusive, ascending.
    ///
    /// `before`/`after` are ints for the same reason `count` is, and each is
    /// clamped to zero independently; a window negative on both sides is refused
    /// outright. Deliberately NOT a thin delegate to LyricsSync::getContextLines:
    /// this anchors on the cached currentLineIndex_ and returns line 0 when there
    /// is no active line, while LyricsSync returns nothing. See the comment in the
    /// .cpp; the difference is pinned by a test, and unifying it changes what
    /// QML receives.
    Q_INVOKABLE QVariantList getContextLines(int before, int after) const;

signals:
    void lyricsChanged();
    void positionChanged();
    void searchQueryChanged();
    void searchResultsChanged();
    void exportFinished(const QString& path);
    void exportFailed(const QString& message);

private slots:
    void onPositionChanged(vc::LyricsSyncPosition pos);
    void onLineChanged(int lineIndex);
    void onWordChanged(int lineIndex, int wordIndex);
    void onStateChanged(vc::LyricsSyncState state);

private:
    QVariantMap lineToVariant(const vc::LyricsLine& line, int index) const;
    void updateSearchResults();

    static vc::LyricsSync* s_sync;
    static vc::AudioEngine* s_engine;
    static vc::LyricsSync* s_connectedSync;
    static std::optional<std::size_t> s_positionConnection;
    static std::optional<std::size_t> s_stateConnection;

    int currentLineIndex_{-1};
    int currentWordIndex_{-1};
    qreal lineProgress_{0.0};
    qreal wordProgress_{0.0};
    bool isInstrumental_{false};
    QString searchQuery_;
    QVariantList searchResults_;
};

} // namespace qml_bridge
