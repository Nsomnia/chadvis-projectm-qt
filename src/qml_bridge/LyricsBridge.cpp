#include "LyricsBridge.hpp"
#include "audio/AudioEngine.hpp"
#include "core/Logger.hpp"

#include <QSaveFile>

#include <algorithm>
#include <cmath>
#include "lyrics/LyricsSync.hpp"

namespace qml_bridge {

namespace {

QString formatSrtTime(const vc::f32 seconds) {
    const qint64 totalMs = std::max<qint64>(0, std::llround(seconds * 1000.0f));
    const qint64 hours = totalMs / 3600000;
    const qint64 minutes = (totalMs / 60000) % 60;
    const qint64 secs = (totalMs / 1000) % 60;
    const qint64 millis = totalMs % 1000;
    return QStringLiteral("%1:%2:%3,%4")
            .arg(hours, 2, 10, QLatin1Char('0'))
            .arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(secs, 2, 10, QLatin1Char('0'))
            .arg(millis, 3, 10, QLatin1Char('0'));
}

QString formatLrcTime(const vc::f32 seconds) {
    const qint64 totalCentiseconds = std::max<qint64>(0, std::llround(seconds * 100.0f));
    const qint64 minutes = totalCentiseconds / 6000;
    const qint64 secs = (totalCentiseconds / 100) % 60;
    const qint64 centis = totalCentiseconds % 100;
    return QStringLiteral("[%1:%2.%3]")
            .arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(secs, 2, 10, QLatin1Char('0'))
            .arg(centis, 2, 10, QLatin1Char('0'));
}

bool writeExportFile(const QString& path, const QByteArray& contents, QString& error) {
    if (path.trimmed().isEmpty()) {
        error = QStringLiteral("An export path is required.");
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        error = file.errorString();
        return false;
    }
    if (file.write(contents) != contents.size() || !file.commit()) {
        error = file.errorString();
        return false;
    }
    return true;
}

}

vc::LyricsSync* LyricsBridge::s_sync = nullptr;
vc::AudioEngine* LyricsBridge::s_engine = nullptr;
vc::LyricsSync* LyricsBridge::s_connectedSync = nullptr;
std::optional<std::size_t> LyricsBridge::s_positionConnection;
std::optional<std::size_t> LyricsBridge::s_stateConnection;

LyricsBridge::LyricsBridge(QObject* parent) : QObject(parent) {
    setInstance(this);
    connectSignals();
}

LyricsBridge::~LyricsBridge() {
    if (instance() != this) {
        return;
    }
    if (s_connectedSync && s_positionConnection) {
        s_connectedSync->positionChanged.disconnect(*s_positionConnection);
    }
    if (s_connectedSync && s_stateConnection) {
        s_connectedSync->stateChanged.disconnect(*s_stateConnection);
    }
    s_connectedSync = nullptr;
    s_positionConnection.reset();
    s_stateConnection.reset();
    s_sync = nullptr;
    s_engine = nullptr;
    setInstance(nullptr);
}

void LyricsBridge::setLyricsSync(vc::LyricsSync* sync) {
    s_sync = sync;
    connectSignals();
}

void LyricsBridge::setAudioEngine(vc::AudioEngine* engine) {
    s_engine = engine;
}

void LyricsBridge::connectSignals() {
    auto* bridge = instance();
    if (!bridge || !s_sync) {
        return;
    }

    if (s_connectedSync == s_sync && s_positionConnection && s_stateConnection) {
        return;
    }

    if (s_connectedSync && s_positionConnection) {
        s_connectedSync->positionChanged.disconnect(*s_positionConnection);
    }
    if (s_connectedSync && s_stateConnection) {
        s_connectedSync->stateChanged.disconnect(*s_stateConnection);
    }

    s_connectedSync = s_sync;
    s_positionConnection = s_sync->positionChanged.connect([](vc::LyricsSyncPosition pos) {
        if (auto* current = instance()) {
            current->onPositionChanged(pos);
        }
    });
    s_stateConnection = s_sync->stateChanged.connect([](vc::LyricsSyncState state) {
        if (auto* current = instance()) {
            current->onStateChanged(state);
        }
    });
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

void LyricsBridge::onStateChanged(vc::LyricsSyncState state) {
    if (state == vc::LyricsSyncState::Loading ||
        state == vc::LyricsSyncState::Idle ||
        state == vc::LyricsSyncState::Error) {
        currentLineIndex_ = -1;
        currentWordIndex_ = -1;
        lineProgress_ = 0.0;
        wordProgress_ = 0.0;
        isInstrumental_ = false;
        emit positionChanged();
    }
    emit lyricsChanged();
    updateSearchResults();
}

void LyricsBridge::seekToLine(int lineIndex) {
    if (s_sync) s_sync->jumpToLine(static_cast<size_t>(lineIndex));
}

void LyricsBridge::exportToSrt(const QString& path) {
    if (!s_sync || !s_sync->hasLyrics()) {
        emit exportFailed(QStringLiteral("No lyrics are loaded."));
        return;
    }

    QString output;
    int index = 1;
    for (const auto& line : s_sync->getLyrics().lines) {
        if (line.text.empty()) {
            continue;
        }
        const auto end = line.endTime > line.startTime ? line.endTime : line.startTime + 1.0f;
        output += QStringLiteral("%1\n%2 --> %3\n%4\n\n")
                .arg(index++)
                .arg(formatSrtTime(line.startTime), formatSrtTime(end),
                     QString::fromStdString(line.text));
    }

    QString error;
    if (!writeExportFile(path, output.toUtf8(), error)) {
        emit exportFailed(error);
        return;
    }
    emit exportFinished(path);
}

void LyricsBridge::exportToLrc(const QString& path) {
    if (!s_sync || !s_sync->hasLyrics()) {
        emit exportFailed(QStringLiteral("No lyrics are loaded."));
        return;
    }

    QString output;
    const auto& lyrics = s_sync->getLyrics();
    if (!lyrics.title.empty()) {
        output += QStringLiteral("[ti:%1]\n")
                .arg(QString::fromStdString(lyrics.title));
    }
    if (!lyrics.artist.empty()) {
        output += QStringLiteral("[ar:%1]\n")
                .arg(QString::fromStdString(lyrics.artist));
    }
    for (const auto& line : lyrics.lines) {
        if (line.text.empty()) {
            continue;
        }
        output += formatLrcTime(line.startTime) +
                  QString::fromStdString(line.text) + QLatin1Char('\n');
    }

    QString error;
    if (!writeExportFile(path, output.toUtf8(), error)) {
        emit exportFailed(error);
        return;
    }
    emit exportFinished(path);
}

void LyricsBridge::setSearchQuery(const QString& query) {
    if (searchQuery_ == query) {
        return;
    }
    searchQuery_ = query;
    emit searchQueryChanged();
    updateSearchResults();
}

QString LyricsBridge::searchQuery() const { return searchQuery_; }
QVariantList LyricsBridge::searchResults() const { return searchResults_; }

void LyricsBridge::updateSearchResults() {
    QVariantList results;
    if (s_sync && !searchQuery_.trimmed().isEmpty()) {
        const auto& lines = s_sync->getLyrics().lines;
        for (int index = 0; index < static_cast<int>(lines.size()); ++index) {
            if (QString::fromStdString(lines[static_cast<std::size_t>(index)].text)
                    .contains(searchQuery_, Qt::CaseInsensitive)) {
                results.append(lineToVariant(lines[static_cast<std::size_t>(index)], index));
            }
        }
    }
    searchResults_ = results;
    emit searchResultsChanged();
}

QVariantList LyricsBridge::getUpcomingLines(int count) const {
    QVariantList result;
    if (!s_sync || count <= 0) {
        return result;
    }
    const auto& lines = s_sync->getLyrics().lines;
    const int start = std::max(currentLineIndex_ + 1, 0);
    const int end = std::min(start + count, static_cast<int>(lines.size()));
    for (int index = start; index < end; ++index) {
        result.append(lineToVariant(lines[static_cast<std::size_t>(index)], index));
    }
    return result;
}

QVariantList LyricsBridge::getContextLines(int before, int after) const {
    QVariantList result;
    if (!s_sync || (before < 0 && after < 0)) {
        return result;
    }
    const auto& lines = s_sync->getLyrics().lines;
    if (lines.empty()) {
        return result;
    }
    const int center = std::clamp(currentLineIndex_, 0, static_cast<int>(lines.size()) - 1);
    const int start = std::max(0, center - std::max(0, before));
    const int end = std::min(static_cast<int>(lines.size()), center + std::max(0, after) + 1);
    for (int index = start; index < end; ++index) {
        result.append(lineToVariant(lines[static_cast<std::size_t>(index)], index));
    }
    return result;
}

} // namespace qml_bridge
