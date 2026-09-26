/**
 * @file LyricsSync.cpp
 * @brief Time synchronization engine implementation.
 */

#include "LyricsSync.hpp"
#include "audio/AudioEngine.hpp"
#include "core/Logger.hpp"
#include <QTimer>
#include <algorithm>

namespace vc {

LyricsSync::LyricsSync(AudioEngine* audio, QObject* parent)
    : QObject(parent), audio_(audio), updateTimer_(new QTimer(this)) {
    
    updateTimer_->setInterval(config_.updateIntervalMs);
    connect(updateTimer_, &QTimer::timeout, this, [this]() {
        if (audio_) {
            updatePosition(static_cast<f32>(audio_->position().count()) / 1000.0f);
        }
    });
    
	if (audio_) {
		connect(audio_, &AudioEngine::stateChanged,
			this, [this](PlaybackState state) {
				onAudioStateChanged(state);
			}, Qt::QueuedConnection);

		connect(audio_, &AudioEngine::trackChanged,
			this, [this]() {
				onAudioTrackChanged();
			}, Qt::DirectConnection);
	}
}

LyricsSync::~LyricsSync() = default;

void LyricsSync::loadLyrics(const LyricsData& lyrics) {
    setState(LyricsSyncState::Loading);
    lyrics_ = lyrics;
    // Invariant: every assignment to lyrics_ resets currentPos_ on the next
    // line, so the cached lineIndex can never outlive the lines it indexes.
    // Any new lyrics_ assignment must repeat this.
    currentPos_ = LyricsSyncPosition();
    smoothedTime_ = 0.0f;

    if (lyrics_.empty()) {
        updateTimer_->stop();
        setState(LyricsSyncState::Error);
        LOG_WARN("LyricsSync: Loaded empty lyrics");
        return;
    }

    setState(LyricsSyncState::Ready);
    LOG_INFO("LyricsSync: Loaded {} lines", lyrics_.lineCount());

    if (audio_ && audio_->isPlaying()) {
        smoothedTime_ = std::max(
            0.0f, static_cast<f32>(audio_->position().count()) / 1000.0f);
        updatePosition(smoothedTime_);
        start();
    }
}

void LyricsSync::clear() {
    updateTimer_->stop();
    lyrics_ = LyricsData();
    // Second and last site that replaces lyrics_; see loadLyrics for why the
    // reset is unconditional and adjacent.
    currentPos_ = LyricsSyncPosition();
    smoothedTime_ = 0.0f;
    setState(LyricsSyncState::Idle);
}

void LyricsSync::start() {
    if (state_ == LyricsSyncState::Ready || state_ == LyricsSyncState::Paused) {
        setState(LyricsSyncState::Syncing);
        updateTimer_->start();
        LOG_DEBUG("LyricsSync: Started");
    }
}

void LyricsSync::stop() {
    updateTimer_->stop();
    if (hasLyrics()) {
        setState(LyricsSyncState::Ready);
    }
    LOG_DEBUG("LyricsSync: Stopped");
}

void LyricsSync::pause() {
    if (state_ == LyricsSyncState::Syncing) {
        updateTimer_->stop();
        setState(LyricsSyncState::Paused);
        LOG_DEBUG("LyricsSync: Paused");
    }
}

void LyricsSync::resume() {
    if (state_ == LyricsSyncState::Paused) {
        setState(LyricsSyncState::Syncing);
        updateTimer_->start();
        LOG_DEBUG("LyricsSync: Resumed");
    }
}

void LyricsSync::seek(f32 time) {
    if (lyrics_.empty()) {
        setState(LyricsSyncState::Error);
        return;
    }

    setState(LyricsSyncState::Seeking);
    smoothedTime_ = std::max(0.0f, time);
    updatePosition(smoothedTime_);
    
    // Return to appropriate state
    if (audio_ && audio_->isPlaying()) {
        setState(LyricsSyncState::Syncing);
    } else {
        setState(LyricsSyncState::Paused);
    }
    
    LOG_DEBUG("LyricsSync: Seek to {:.2f}s", time);
}

LyricsSyncPosition LyricsSync::getPosition() const {
    return currentPos_;
}

void LyricsSync::updatePosition(f32 time) {
    if (lyrics_.empty()) return;

    time = std::max(0.0f, time);
    smoothedTime_ = vc::lerp(smoothedTime_, time, config_.smoothingFactor);

    LyricsSyncPosition newPos;
    newPos.time = smoothedTime_;

    int lineIdx = lyrics_.findLineIndex(smoothedTime_);
    newPos.lineIndex = lineIdx;

    if (lineIdx >= 0 && lineIdx < static_cast<int>(lyrics_.lines.size())) {
        const auto& line = lyrics_.lines[lineIdx];
        newPos.isInstrumental = line.isInstrumental;

        if (line.isSynced && line.endTime > line.startTime) {
            newPos.lineProgress = std::clamp(
                (smoothedTime_ - line.startTime) / (line.endTime - line.startTime),
                0.0f, 1.0f);
        }

        if (config_.emitWordChanges && !line.words.empty()) {
            int wordIdx = line.getActiveWordIndex(smoothedTime_);
            newPos.wordIndex = wordIdx;

            if (wordIdx >= 0 && wordIdx < static_cast<int>(line.words.size())) {
                const auto& word = line.words[wordIdx];
                newPos.wordProgress = std::clamp(word.getProgress(smoothedTime_),
                                                0.0f, 1.0f);
            }
        }
    }

    detectChanges(currentPos_, newPos);

    currentPos_ = newPos;
    positionChanged.emitSignal(currentPos_);
}

void LyricsSync::detectChanges(const LyricsSyncPosition& oldPos, 
                               const LyricsSyncPosition& newPos) {
    // Line change
    if (newPos.lineIndex != oldPos.lineIndex) {
        lineChanged.emitSignal(newPos.lineIndex);
        
        // Check for section changes (instrumental)
        if (newPos.isInstrumental && config_.instrumentalDetection) {
            eventOccurred.emitSignal(LyricsSyncEvent::Instrumental);
        }
        
        // Check for end of lyrics
        if (newPos.lineIndex < 0 && oldPos.lineIndex >= 0) {
            if (oldPos.lineIndex == static_cast<int>(lyrics_.lines.size()) - 1) {
                eventOccurred.emitSignal(LyricsSyncEvent::EndOfLyrics);
            }
        }
    }
    
    // Word change
    if (config_.emitWordChanges) {
        if (newPos.wordIndex != oldPos.wordIndex || 
            newPos.lineIndex != oldPos.lineIndex) {
            wordChanged.emitSignal(newPos.lineIndex, newPos.wordIndex);
        }
    }
}

void LyricsSync::setState(LyricsSyncState newState) {
    if (state_ != newState) {
        state_ = newState;
        stateChanged.emitSignal(state_);
    }
}

void LyricsSync::setConfig(const Config& config) {
    config_ = config;
    updateTimer_->setInterval(config_.updateIntervalMs);
}

LyricsSync::Config LyricsSync::getConfig() const {
    return config_;
}

void LyricsSync::jumpToLine(size_t lineIndex) {
    if (lineIndex < lyrics_.lines.size()) {
        f32 targetTime = lyrics_.lines[lineIndex].startTime;
        seek(targetTime);
        
        if (audio_) {
            audio_->seek(Duration(static_cast<i64>(targetTime * 1000)));
        }
    }
}

std::vector<const LyricsLine*> LyricsSync::getUpcomingLines(size_t count) const {
    std::vector<const LyricsLine*> result;
    
    int currentIdx = currentPos_.lineIndex;
    if (currentIdx < 0) currentIdx = 0;
    
    for (size_t i = 0; i < count; ++i) {
        size_t idx = currentIdx + i + 1;
        if (idx < lyrics_.lines.size()) {
            result.push_back(&lyrics_.lines[idx]);
        }
    }
    
    return result;
}

std::vector<const LyricsLine*> LyricsSync::getContextLines(size_t before, 
                                                           size_t after) const {
    std::vector<const LyricsLine*> result;
    
    // currentPos_ is a cache, not a view of the live vector: once lyrics_ is
    // replaced the cached lineIndex can name a line the new song does not have,
    // so the lower bound below is not enough on its own. Every subscript in
    // this function goes through checkedIndex, which applies the same upper
    // bound as the `idx < lyrics_.lines.size()` guard the loop below used to
    // spell out by hand.
    const auto current = checkedIndex(lyrics_.lines, currentPos_.lineIndex);
    if (!current) return result;
    const int currentIdx = static_cast<int>(*current);
    
    // Add lines before
    for (int i = static_cast<int>(before); i > 0; --i) {
        if (auto idx = checkedIndex(lyrics_.lines, currentIdx - i)) {
            result.push_back(&lyrics_.lines[*idx]);
        }
    }
    
    // Add current line
    result.push_back(&lyrics_.lines[*current]);
    
    // Add lines after
    for (size_t i = 1; i <= after; ++i) {
        if (auto idx = checkedIndex(lyrics_.lines, currentIdx + static_cast<int>(i))) {
            result.push_back(&lyrics_.lines[*idx]);
        }
    }
    
    return result;
}

void LyricsSync::onAudioStateChanged(PlaybackState state) {
    switch (state) {
    case PlaybackState::Playing:
        if (state_ == LyricsSyncState::Ready || state_ == LyricsSyncState::Paused) {
            start();
        }
        break;
    case PlaybackState::Paused:
        pause();
        break;
    case PlaybackState::Stopped:
        stop();
        break;
    }
}

void LyricsSync::onAudioTrackChanged() {
    // Clear current lyrics - new track will load new lyrics
    clear();
}

// LyricsSyncInterpolator implementations

LyricsSyncPosition LyricsSyncInterpolator::lerp(const LyricsSyncPosition& a,
                                               const LyricsSyncPosition& b,
                                               f32 t) {
    t = std::clamp(t, 0.0f, 1.0f);
    LyricsSyncPosition result;
    result.time = a.time + (b.time - a.time) * t;
    result.lineProgress = a.lineProgress + (b.lineProgress - a.lineProgress) * t;
    result.wordProgress = a.wordProgress + (b.wordProgress - a.wordProgress) * t;
    
    // Use B's indices if we're past halfway
    if (t > 0.5f) {
        result.lineIndex = b.lineIndex;
        result.wordIndex = b.wordIndex;
        result.isInstrumental = b.isInstrumental;
    } else {
        result.lineIndex = a.lineIndex;
        result.wordIndex = a.wordIndex;
        result.isInstrumental = a.isInstrumental;
    }
    
    return result;
}

LyricsSyncPosition LyricsSyncInterpolator::smoothStep(const LyricsSyncPosition& a,
                                                     const LyricsSyncPosition& b,
                                                     f32 t) {
    // Smooth step: 3t^2 - 2t^3
    f32 smoothT = t * t * (3.0f - 2.0f * t);
    return lerp(a, b, smoothT);
}

LyricsSyncPosition LyricsSyncInterpolator::predict(const LyricsSyncPosition& current,
                                                  f32 deltaTime,
                                                  const LyricsData& lyrics) {
    LyricsSyncPosition predicted = current;
    predicted.time += deltaTime;
    
    // Find predicted line
    int lineIdx = lyrics.findLineIndex(predicted.time);
    predicted.lineIndex = lineIdx;
    
    if (lineIdx >= 0 && lineIdx < static_cast<int>(lyrics.lines.size())) {
        const auto& line = lyrics.lines[lineIdx];
        predicted.isInstrumental = line.isInstrumental;
        
        if (line.isSynced && line.endTime > line.startTime) {
            predicted.lineProgress = std::clamp(
                (predicted.time - line.startTime) /
                    (line.endTime - line.startTime),
                0.0f, 1.0f);
        }
        
        // Predict word
        int wordIdx = line.getActiveWordIndex(predicted.time);
        predicted.wordIndex = wordIdx;
        
        if (wordIdx >= 0 && wordIdx < static_cast<int>(line.words.size())) {
            const auto& word = line.words[wordIdx];
            predicted.wordProgress = std::clamp(word.getProgress(predicted.time),
                                                0.0f, 1.0f);
        }
    }
    
    return predicted;
}

} // namespace vc
