/**
 * @file LyricsSync.cpp
 * @brief Time synchronization engine implementation.
 */

#include "LyricsSync.hpp"
#include "audio/AudioEngine.hpp"
#include "core/Logger.hpp"
#include <QThread>
#include <QTimer>
#include <algorithm>

namespace vc {

LyricsSync::LyricsSync(AudioEngine* audio, QObject* parent)
    : QObject(parent), audio_(audio), updateTimer_(new QTimer(this)) {
    
    updateTimer_->setInterval(config_.updateIntervalMs);
    // The timer is the only thing that drives the position. syncNow() is the
    // one place a position is applied, whether it came from a seed or from
    // the transport, so no caller can update the state behind the timer's
    // back and have the next tick smooth the same instant a second time.
    connect(updateTimer_, &QTimer::timeout, this, &LyricsSync::syncNow);
    
	if (audio_) {
		connect(audio_, &AudioEngine::stateChanged,
			this, [this](PlaybackState state) {
				onAudioStateChanged(state);
			}, Qt::QueuedConnection);

		// Queued, like stateChanged: this runs clear(), which replaces lyrics_
		// and resets the position. A direct call performs that reset in the
		// middle of the transport's own emission, outside the ordering the
		// event loop gives everything else in this class.
		connect(audio_, &AudioEngine::trackChanged,
			this, [this]() {
				onAudioTrackChanged();
			}, Qt::QueuedConnection);
	}
}

LyricsSync::~LyricsSync() = default;

void LyricsSync::loadLyrics(const LyricsData& lyrics) {
    Q_ASSERT(QThread::currentThread() == thread());
    setState(LyricsSyncState::Loading);
    lyrics_ = lyrics;
    // Invariant: every assignment to lyrics_ resets currentPos_ on the next
    // line, so the cached lineIndex can never outlive the lines it indexes.
    // Any new lyrics_ assignment must repeat this.
    currentPos_ = LyricsSyncPosition();
    smoothedTime_ = 0.0f;
    // A seed belongs to the song that asked for it. A leftover from the
    // previous track would otherwise be drained against these lyrics, which
    // is exactly the clear-then-load ordering the playback handoff uses.
    clearPendingPosition();

    if (lyrics_.empty()) {
        updateTimer_->stop();
        setState(LyricsSyncState::Error);
        LOG_WARN("LyricsSync: Loaded empty lyrics");
        return;
    }

    setState(LyricsSyncState::Ready);
    LOG_INFO("LyricsSync: Loaded {} lines", lyrics_.lineCount());

    if (audio_ && audio_->isPlaying()) {
        // Seeded, not applied: the drain lands on this value exactly and the
        // tick after it goes back to the transport.
        seedPendingPosition(audioPositionSeconds());
        start();
    }
}

void LyricsSync::clear() {
    Q_ASSERT(QThread::currentThread() == thread());
    updateTimer_->stop();
    lyrics_ = LyricsData();
    // Second and last site that replaces lyrics_; see loadLyrics for why the
    // reset is unconditional and adjacent.
    currentPos_ = LyricsSyncPosition();
    smoothedTime_ = 0.0f;
    // Third part of that invariant: a seed outliving clear() would be drained
    // against the next track loaded after it.
    clearPendingPosition();
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
    Q_ASSERT(QThread::currentThread() == thread());
    if (lyrics_.empty()) {
        setState(LyricsSyncState::Error);
        return;
    }

    setState(LyricsSyncState::Seeking);
    // Seeded, not applied. Applying here would smooth this instant once for
    // the caller and again on the next tick, and the tick could win with a
    // transport position the seek has not reached yet, dragging the highlight
    // straight back.
    seedPendingPosition(time);
    
    // Return to appropriate state
    if (audio_ && audio_->isPlaying()) {
        setState(LyricsSyncState::Syncing);
    } else {
        setState(LyricsSyncState::Paused);
    }
    
    LOG_DEBUG("LyricsSync: Seek to {:.2f}s", time);
}

void LyricsSync::syncNow() {
    Q_ASSERT(QThread::currentThread() == thread());

    // One shot: consumed here so the tick after a seek goes back to the
    // transport instead of re-applying the same target.
    const bool seeded = hasPendingTime_;
    const f32 seed = pendingTime_;
    clearPendingPosition();

    if (lyrics_.empty()) return;

    if (seeded) {
        // A seed is a position, not a target. Presetting the smoother makes
        // the lerp in updatePosition a no-op, so one drain lands exactly on
        // the seeded time -- the snap a seek is supposed to be, without the
        // second application that used to smooth it again on the next tick.
        smoothedTime_ = seed;
    } else if (!audio_) {
        // Nothing seeded and no transport to sample.
        return;
    }

    updatePosition(seeded ? smoothedTime_ : audioPositionSeconds());
}

f32 LyricsSync::audioPositionSeconds() const {
    return audio_ ? static_cast<f32>(audio_->position().count()) / 1000.0f : 0.0f;
}

void LyricsSync::seedPendingPosition(f32 time) {
    pendingTime_ = std::max(0.0f, time);
    hasPendingTime_ = true;
}

void LyricsSync::clearPendingPosition() noexcept {
    pendingTime_ = 0.0f;
    hasPendingTime_ = false;
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
