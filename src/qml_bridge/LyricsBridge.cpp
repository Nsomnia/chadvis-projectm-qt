#include "LyricsBridge.hpp"
#include "lyrics/LyricsSync.hpp"
#include "lyrics/LyricsData.hpp"
#include "audio/AudioEngine.hpp"
#include <QQmlEngine>
#include <QFile>

namespace qml_bridge {

vc::LyricsSync* LyricsBridge::s_sync = nullptr;
vc::AudioEngine* LyricsBridge::s_engine = nullptr;
LyricsBridge* LyricsBridge::s_instance = nullptr;

LyricsBridge::LyricsBridge(QObject* parent)
    : QObject(parent)
{
}

LyricsBridge* LyricsBridge::create(QQmlEngine* qmlEngine, QJSEngine* jsEngine)
{
    Q_UNUSED(jsEngine)
    if (!s_instance) {
        s_instance = new LyricsBridge(qmlEngine);
    }
    return s_instance;
}

void LyricsBridge::setLyricsSync(vc::LyricsSync* sync)
{
    s_sync = sync;
}

void LyricsBridge::setAudioEngine(vc::AudioEngine* engine)
{
    s_engine = engine;
}

void LyricsBridge::connectSignals()
{
    if (s_sync && s_instance) {
        s_sync->positionChanged.connect([s = s_instance](vc::LyricsSyncPosition pos) {
            s->onPositionChanged(pos);
        });
        s_sync->lineChanged.connect([s = s_instance](int lineIndex) {
            s->onLineChanged(lineIndex);
        });
        s_sync->wordChanged.connect([s = s_instance](int lineIndex, int wordIndex) {
            s->onWordChanged(lineIndex, wordIndex);
        });
        s_sync->stateChanged.connect([s = s_instance](vc::LyricsSyncState state) {
            s->onStateChanged(state);
        });
    }
}

bool LyricsBridge::hasLyrics() const
{
    return s_sync && s_sync->hasLyrics();
}

QVariantList LyricsBridge::lines() const
{
    if (!s_sync) return {};

    QVariantList result;
    const auto& lyrics = s_sync->getLyrics();
    for (size_t i = 0; i < lyrics.lines.size(); ++i) {
        result.append(lineToVariant(lyrics.lines[i], static_cast<int>(i)));
    }
    return result;
}

int LyricsBridge::currentLineIndex() const
{
    return currentLineIndex_;
}

int LyricsBridge::currentWordIndex() const
{
    return currentWordIndex_;
}

qreal LyricsBridge::lineProgress() const
{
    return lineProgress_;
}

qreal LyricsBridge::wordProgress() const
{
    return wordProgress_;
}

bool LyricsBridge::isInstrumental() const
{
    return isInstrumental_;
}

QString LyricsBridge::title() const
{
    if (!s_sync) return {};
    return QString::fromStdString(s_sync->getLyrics().title);
}

QString LyricsBridge::artist() const
{
    if (!s_sync) return {};
    return QString::fromStdString(s_sync->getLyrics().artist);
}

QString LyricsBridge::searchQuery() const
{
    return searchQuery_;
}

QVariantList LyricsBridge::searchResults() const
{
    return searchResults_;
}

void LyricsBridge::setSearchQuery(const QString& query)
{
    if (searchQuery_ != query) {
        searchQuery_ = query;
        updateSearchResults();
        emit searchQueryChanged();
    }
}

void LyricsBridge::seekToLine(int lineIndex)
{
    if (!s_sync || !s_engine) return;

    const auto& lyrics = s_sync->getLyrics();
    if (lineIndex >= 0 && static_cast<size_t>(lineIndex) < lyrics.lines.size()) {
        auto startTime = lyrics.lines[lineIndex].startTime;
        s_engine->seek(std::chrono::milliseconds(static_cast<long long>(startTime * 1000)));
    }
}

void LyricsBridge::exportToSrt(const QString& path)
{
    if (!s_sync) return;

    QString content = QString::fromStdString(
        vc::LyricsExport::toSrt(s_sync->getLyrics()));

    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        file.write(content.toUtf8());
        file.close();
    }
}

void LyricsBridge::exportToLrc(const QString& path)
{
    if (!s_sync) return;

    QString content = QString::fromStdString(
        vc::LyricsExport::toLrc(s_sync->getLyrics()));

    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        file.write(content.toUtf8());
        file.close();
    }
}

QVariantMap LyricsBridge::getLine(int index) const
{
    if (!s_sync) return {};

    const auto& lyrics = s_sync->getLyrics();
    if (index >= 0 && static_cast<size_t>(index) < lyrics.lines.size()) {
        return lineToVariant(lyrics.lines[index], index);
    }
    return {};
}

QVariantList LyricsBridge::getUpcomingLines(int count) const
{
    if (!s_sync) return {};

    QVariantList result;
    auto lines = s_sync->getUpcomingLines(static_cast<size_t>(count));
    for (const auto* line : lines) {
        int idx = static_cast<int>(line - &s_sync->getLyrics().lines[0]);
        result.append(lineToVariant(*line, idx));
    }
    return result;
}

QVariantList LyricsBridge::getContextLines(int before, int after) const
{
    if (!s_sync) return {};

    QVariantList result;
    auto lines = s_sync->getContextLines(static_cast<size_t>(before),
                                          static_cast<size_t>(after));
    for (const auto* line : lines) {
        int idx = static_cast<int>(line - &s_sync->getLyrics().lines[0]);
        result.append(lineToVariant(*line, idx));
    }
    return result;
}

void LyricsBridge::onPositionChanged(vc::LyricsSyncPosition pos)
{
    currentLineIndex_ = pos.lineIndex;
    currentWordIndex_ = pos.wordIndex;
    lineProgress_ = pos.lineProgress;
    wordProgress_ = pos.wordProgress;
    isInstrumental_ = pos.isInstrumental;
    emit positionChanged();
}

void LyricsBridge::onLineChanged(int lineIndex)
{
    currentLineIndex_ = lineIndex;
    emit positionChanged();
}

void LyricsBridge::onWordChanged(int lineIndex, int wordIndex)
{
    currentLineIndex_ = lineIndex;
    currentWordIndex_ = wordIndex;
    emit positionChanged();
}

void LyricsBridge::onStateChanged(vc::LyricsSyncState state)
{
    Q_UNUSED(state)
    emit lyricsChanged();
}

QVariantMap LyricsBridge::lineToVariant(const vc::LyricsLine& line, int index) const
{
    QVariantMap map;
    map[QStringLiteral("text")] = QString::fromStdString(line.text);
    map[QStringLiteral("startTime")] = line.startTime;
    map[QStringLiteral("endTime")] = line.endTime;
    map[QStringLiteral("isInstrumental")] = line.isInstrumental;
    map[QStringLiteral("isSynced")] = line.isSynced;
    map[QStringLiteral("index")] = index;

    QVariantList words;
    for (const auto& word : line.words) {
        QVariantMap wordMap;
        wordMap[QStringLiteral("text")] = QString::fromStdString(word.text);
        wordMap[QStringLiteral("startTime")] = word.startTime;
        wordMap[QStringLiteral("endTime")] = word.endTime;
        words.append(wordMap);
    }
    map[QStringLiteral("words")] = words;

    return map;
}

void LyricsBridge::updateSearchResults()
{
    if (!s_sync || searchQuery_.isEmpty()) {
        searchResults_.clear();
        emit searchResultsChanged();
        return;
    }

    auto indices = s_sync->getLyrics().search(searchQuery_.toStdString());
    searchResults_.clear();
    for (size_t idx : indices) {
        if (idx < s_sync->getLyrics().lines.size()) {
            searchResults_.append(lineToVariant(s_sync->getLyrics().lines[idx],
                                                 static_cast<int>(idx)));
        }
    }
    emit searchResultsChanged();
}

} // namespace qml_bridge
