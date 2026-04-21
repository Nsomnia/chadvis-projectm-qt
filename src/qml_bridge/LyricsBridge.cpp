#include "LyricsBridge.hpp"
#include "audio/AudioEngine.hpp"
#include "lyrics/LyricsSync.hpp"
#include <QQmlEngine>

namespace qml_bridge {

vc::LyricsSync* LyricsBridge::s_sync = nullptr;
vc::AudioEngine* LyricsBridge::s_engine = nullptr;
LyricsBridge* LyricsBridge::s_instance = nullptr;

LyricsBridge::LyricsBridge(QObject* parent) : QObject(parent) {
    s_instance = this;
}

LyricsBridge* LyricsBridge::create(QQmlEngine*, QJSEngine*) {
    return new LyricsBridge();
}

void LyricsBridge::setLyricsSync(vc::LyricsSync* sync) {
    s_sync = sync;
}

void LyricsBridge::setAudioEngine(vc::AudioEngine* engine) {
    s_engine = engine;
}

void LyricsBridge::connectSignals() {
    if (s_sync && s_instance) {
        s_sync->positionChanged.connect([](vc::LyricsSyncPosition pos) {
            if (s_instance) s_instance->onPositionChanged(pos);
        });
        s_sync->stateChanged.connect([](vc::LyricsSyncState state) {
            if (s_instance) s_instance->onStateChanged(state);
        });
        // Note: LyricsSync doesn't have a lyricsChanged Signal, using state transitions
    }
}

bool LyricsBridge::hasLyrics() const {
    return s_sync && s_sync->hasLyrics();
}

QVariantList LyricsBridge::lines() const {
    QVariantList result;
    if (!s_sync) return result;
    
    const auto& lyricsLines = s_sync->getLyrics().lines;
    for (int i = 0; i < static_cast<int>(lyricsLines.size()); ++i) {
        result.append(lineToVariant(lyricsLines[i], i));
    }
    return result;
}

int LyricsBridge::currentLineIndex() const { return currentLineIndex_; }
int LyricsBridge::currentWordIndex() const { return currentWordIndex_; }
qreal LyricsBridge::lineProgress() const { return lineProgress_; }
qreal LyricsBridge::wordProgress() const { return wordProgress_; }
bool LyricsBridge::isInstrumental() const { return isInstrumental_; }

QString LyricsBridge::title() const {
    return s_sync ? QString::fromStdString(s_sync->getLyrics().title) : "";
}

QString LyricsBridge::artist() const {
    return s_sync ? QString::fromStdString(s_sync->getLyrics().artist) : "";
}

QVariantMap LyricsBridge::lineToVariant(const vc::LyricsLine& line, int index) const {
    QVariantMap map;
    map["index"] = index;
    map["text"] = QString::fromStdString(line.text);
    map["startTime"] = static_cast<qint64>(line.startTime * 1000.0f);
    map["isInstrumental"] = line.isInstrumental;
    return map;
}

QVariantMap LyricsBridge::getLine(int index) const {
    if (!s_sync || index < 0) return QVariantMap();
    const auto& lyricsLines = s_sync->getLyrics().lines;
    if (index >= static_cast<int>(lyricsLines.size())) return QVariantMap();
    return lineToVariant(lyricsLines[index], index);
}

void LyricsBridge::onPositionChanged(vc::LyricsSyncPosition pos) {
    currentLineIndex_ = pos.lineIndex;
    currentWordIndex_ = pos.wordIndex;
    lineProgress_ = pos.lineProgress;
    wordProgress_ = pos.wordProgress;
    isInstrumental_ = pos.isInstrumental;
    emit positionChanged();
}

void LyricsBridge::onLineChanged(int lineIndex) {
    currentLineIndex_ = lineIndex;
    emit positionChanged();
}

void LyricsBridge::onWordChanged(int lineIndex, int wordIndex) {
    currentLineIndex_ = lineIndex;
    currentWordIndex_ = wordIndex;
    emit positionChanged();
}

void LyricsBridge::onStateChanged(vc::LyricsSyncState) {
    emit lyricsChanged();
}

void LyricsBridge::seekToLine(int lineIndex) {
    if (s_sync) s_sync->jumpToLine(static_cast<size_t>(lineIndex));
}

void LyricsBridge::exportToSrt(const QString&) {}
void LyricsBridge::exportToLrc(const QString&) {}
void LyricsBridge::setSearchQuery(const QString&) {}
QString LyricsBridge::searchQuery() const { return ""; }
QVariantList LyricsBridge::searchResults() const { return QVariantList(); }
void LyricsBridge::updateSearchResults() {}
QVariantList LyricsBridge::getUpcomingLines(int) const { return QVariantList(); }
QVariantList LyricsBridge::getContextLines(int, int) const { return QVariantList(); }

} // namespace qml_bridge
