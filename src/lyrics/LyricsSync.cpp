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

// Inline lerp helper for early use
static inline f32 lerp_f32(f32 a, f32 b, f32 t) {
    return a + (b - a) * t;
}

LyricsSync::LyricsSync(AudioEngine* audio, QObject* parent)
    : QObject(parent), audio_(audio), updateTimer_(new QTimer(this)) {
    
    updateTimer_->setInterval(config_.updateIntervalMs);
    connect(updateTimer_, &QTimer::timeout, this, [this]() {
        if (audio_) {
            updatePosition(static_cast<f32>(audio_->position().count()) / 1000.0f);
        }
    });
    
	if (audio_) {
		connect(audio_, &AudioEngine::positionChanged,
			this, [this](Duration pos) {
				updatePosition(static_cast<f32>(pos.count()) / 1000.0f);
			}, Qt::DirectConnection);

		connect(audio_, &AudioEngine::stateChanged,
			this, [this](PlaybackState state) {
				onAudioStateChanged(state);
			}, Qt::QueuedConnection);

		connect(audio_, &AudioEngine::trackChanged,
			this, [this]() {
				onAudioTrackChanged();
			}, Qt::QueuedConnection);
	}
}

LyricsSync::~LyricsSync() = default;

void LyricsSync::loadLyrics(const LyricsData& lyrics) {
    setState(LyricsSyncState::Loading);
    lyrics_ = lyrics;
    currentPos_ = LyricsSyncPosition();
    smoothedTime_ = 0.0f;
    
    if (!lyrics.empty()) {
        setState(LyricsSyncState::Ready);
        LOG_INFO("LyricsSync: Loaded {} lines", lyrics.lineCount());
    } else {
        setState(LyricsSyncState::Error);
        LOG_WARN("LyricsSync: Loaded empty lyrics");
    }
}

void LyricsSync::clear() {
    lyrics_ = LyricsData();
    currentPos_ = LyricsSyncPosition();
    smoothedTime_ = 0.0f;
    setState(LyricsSyncState::Idle);
    updateTimer_->stop();
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
    setState(LyricsSyncState::Ready);
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
    setState(LyricsSyncState::Seeking);
    smoothedTime_ = time;
    updatePosition(time);
    
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
    
    // Apply smoothing
    smoothedTime_ = lerp_f32(smoothedTime_, time, config_.smoothingFactor);
    
    LyricsSyncPosition newPos;
    newPos.time = smoothedTime_;
    
    // Find active line
    int lineIdx = lyrics_.findLineIndex(smoothedTime_);
    newPos.lineIndex = lineIdx;
    
    if (lineIdx >= 0 && lineIdx < static_cast<int>(lyrics_.lines.size())) {
        const auto& line = lyrics_.lines[lineIdx];
        newPos.isInstrumental = line.isInstrumental;
        
        if (line.isSynced && line.endTime > line.startTime) {
            newPos.lineProgress = (smoothedTime_ - line.startTime) / (line.endTime - line.startTime);
        }
        
        // Find active word
        if (config_.emitWordChanges && !line.words.empty()) {
            int wordIdx = line.getActiveWordIndex(smoothedTime_);
            newPos.wordIndex = wordIdx;
            
            if (wordIdx >= 0 && wordIdx < static_cast<int>(line.words.size())) {
                const auto& word = line.words[wordIdx];
                newPos.wordProgress = word.getProgress(smoothedTime_);
            }
        }
    }
    
    // Detect and emit changes
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
    
    int currentIdx = currentPos_.lineIndex;
    if (currentIdx < 0) return result;
    
    // Add lines before
    for (int i = static_cast<int>(before); i > 0; --i) {
        int idx = currentIdx - i;
        if (idx >= 0) {
            result.push_back(&lyrics_.lines[idx]);
        }
    }
    
    // Add current line
    result.push_back(&lyrics_.lines[currentIdx]);
    
    // Add lines after
    for (size_t i = 1; i <= after; ++i) {
        size_t idx = currentIdx + i;
        if (idx < lyrics_.lines.size()) {
            result.push_back(&lyrics_.lines[idx]);
        }
    }
    
    return result;
}

void LyricsSync::onAudioPositionChanged(Duration pos) {
    // Handled by timer for consistent updates
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

// Animation helpers
f32 LyricsSync::lerp(f32 a, f32 b, f32 t) const {
    return a + (b - a) * t;
}

// LyricsSyncInterpolator implementations

LyricsSyncPosition LyricsSyncInterpolator::lerp(const LyricsSyncPosition& a,
                                               const LyricsSyncPosition& b,
                                               f32 t) {
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
            predicted.lineProgress = (predicted.time - line.startTime) / 
                                     (line.endTime - line.startTime);
        }
        
        // Predict word
        int wordIdx = line.getActiveWordIndex(predicted.time);
        predicted.wordIndex = wordIdx;
        
        if (wordIdx >= 0 && wordIdx < static_cast<int>(line.words.size())) {
            const auto& word = line.words[wordIdx];
            predicted.wordProgress = word.getProgress(predicted.time);
        }
    }
    
    return predicted;
}

} // namespace vc
