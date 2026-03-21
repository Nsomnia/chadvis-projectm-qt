#pragma once

#include <QObject>
#include <QtQml/qqml.h>
#include <QVariantList>
#include <QVariantMap>
#include "lyrics/LyricsData.hpp"
#include "lyrics/LyricsSync.hpp"

namespace vc {
class AudioEngine;
}

namespace qml_bridge {

class LyricsBridge : public QObject {
    Q_OBJECT

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
    ~LyricsBridge() override = default;

    static LyricsBridge* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);
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
    Q_INVOKABLE QVariantMap getLine(int index) const;
    Q_INVOKABLE QVariantList getUpcomingLines(int count) const;
    Q_INVOKABLE QVariantList getContextLines(int before, int after) const;

signals:
    void lyricsChanged();
    void positionChanged();
    void searchQueryChanged();
    void searchResultsChanged();

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
    static LyricsBridge* s_instance;

    int currentLineIndex_{-1};
    int currentWordIndex_{-1};
    qreal lineProgress_{0.0};
    qreal wordProgress_{0.0};
    bool isInstrumental_{false};
    QString searchQuery_;
    QVariantList searchResults_;
};

} // namespace qml_bridge
