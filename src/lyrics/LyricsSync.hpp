/**
 * @file LyricsSync.hpp
 * @brief Time synchronization engine for lyrics display.
 *
 * Provides smooth, accurate synchronization between audio playback
 * and lyrics display. Handles seeking, buffering, and pre-fetching.
 *
 * Pattern: Observer + State Machine
 *
 * @author ChadVis Agent  
 * @version 11.0 (Silky Smooth Edition)
 */

#pragma once
#include <QObject>
#include <QTimer>
#include <functional>
#include <deque>
#include "LyricsData.hpp"
#include "util/Signal.hpp"
#include "util/Types.hpp"

namespace vc {

// Forward declarations
enum class PlaybackState;

// Forward declaration
class AudioEngine;

/**
 * @brief Synchronization state
 */
enum class LyricsSyncState {
    Idle,           ///< No lyrics loaded
    Loading,        ///< Loading in progress
    Ready,          ///< Ready to sync
    Syncing,        ///< Actively syncing
    Paused,         ///< Playback paused
    Seeking,        ///< Seek in progress
    Error           ///< Sync error
};

/**
 * @brief Sync event types
 */
enum class LyricsSyncEvent {
    LineChanged,    ///< Active line changed
    WordChanged,    ///< Active word changed (karaoke mode)
    SectionChanged, ///< Major section changed (verse/chorus)
    Instrumental,   ///< Entered instrumental section
    EndOfLyrics     ///< Reached end
};

/**
 * @brief Sync position info
 */
struct LyricsSyncPosition {
    f32 time{0.0f};                 ///< Current time in seconds
    int lineIndex{-1};              ///< Current line index (-1 = none)
    int wordIndex{-1};              ///< Current word index (-1 = none)
    f32 lineProgress{0.0f};         ///< Progress through current line (0.0-1.0)
    f32 wordProgress{0.0f};         ///< Progress through current word (0.0-1.0)
    bool isInstrumental{false};     ///< In instrumental section
    
    bool hasLine() const { return lineIndex >= 0; }
    bool hasWord() const { return wordIndex >= 0; }
};

/**
 * @brief Lyrics synchronization engine
 *
 * Connects to AudioEngine and provides smooth lyrics synchronization.
 * Emits signals for line/word changes that renderers can connect to.
 *
 * Features:
 * - 60fps-ready position updates
 * - Smooth seeking without jumps
 * - Pre-fetch next lines for transitions
 * - Instrumental section detection
 *
 * Threading: this object lives in the thread that created it and every
 * mutator must be called from that thread. The position state has exactly
 * one writer, syncNow(), which the update timer calls on every tick; seek()
 * and loadLyrics() only install a seed that the next tick drains. A lock
 * would not help: updatePosition() is a read-modify-write over lyrics_ and
 * the position, and two serialized callers would still double-smooth one
 * audio instant. Event-loop serialization is the invariant, not mutual
 * exclusion.
 */
class LyricsSync : public QObject {
    Q_OBJECT
    
public:
    explicit LyricsSync(AudioEngine* audio, QObject* parent = nullptr);
    ~LyricsSync() override;
    
    /**
     * @brief Load lyrics data for synchronization
     */
    void loadLyrics(const LyricsData& lyrics);
    
    /**
     * @brief Clear current lyrics
     */
    void clear();
    
    /**
     * @brief Start synchronization
     */
    void start();
    
    /**
     * @brief Stop synchronization
     */
    void stop();
    
    /**
     * @brief Pause (maintains position)
     */
    void pause();
    
    /**
     * @brief Resume from pause
     */
    void resume();
    
    /**
     * @brief Seek to specific time
     *
     * Installs a seed that the next syncNow() drains; it does not move the
     * position by itself.
     */
    void seek(f32 time);
    
    /**
     * @brief Apply one position update now
     *
     * The single writer for the position state. The update timer calls this
     * every tick; it is public so a caller can force a synchronous refresh
     * after a seek, or drive the sync by hand with no AudioEngine attached
     * (a pending seed is still drained, and with no transport to sample the
     * call is a no-op).
     */
    void syncNow();
    
    /**
     * @brief Get current sync position
     */
    LyricsSyncPosition getPosition() const;
    
    /**
     * @brief Get current state
     */
    LyricsSyncState getState() const { return state_; }
    
    /**
     * @brief Check if lyrics are loaded
     */
    bool hasLyrics() const { return !lyrics_.empty(); }
    
    /**
     * @brief Get loaded lyrics
     */
    const LyricsData& getLyrics() const { return lyrics_; }
    
    // Signals
    Signal<LyricsSyncPosition> positionChanged;     ///< Position updated (60fps)
    Signal<int> lineChanged;                        ///< Line index changed
    Signal<int, int> wordChanged;                   ///< Line and word index changed
    Signal<LyricsSyncEvent> eventOccurred;          ///< Special events
    Signal<LyricsSyncState> stateChanged;           ///< State machine changes
    
    /**
     * @brief Configure sync behavior
     */
    struct Config {
        int updateIntervalMs{16};       ///< Update interval (~60fps)
        f32 lookaheadTime{2.0f};        ///< How far ahead to pre-fetch (seconds)
        f32 smoothingFactor{0.3f};      ///< Position smoothing (0.0-1.0)
        bool emitWordChanges{true};     ///< Emit word-level changes
        bool instrumentalDetection{true}; ///< Detect instrumental sections
    };
    void setConfig(const Config& config);
    Config getConfig() const;
    
    /**
     * @brief Jump to specific line
     */
    void jumpToLine(size_t lineIndex);
    
    /**
     * @brief Get upcoming lines (for pre-rendering)
     */
    std::vector<const LyricsLine*> getUpcomingLines(size_t count = 3) const;
    
    /**
     * @brief Get context lines around current position
     */
    std::vector<const LyricsLine*> getContextLines(size_t before = 2, 
                                                    size_t after = 2) const;

private slots:
    void onAudioStateChanged(PlaybackState state);
    void onAudioTrackChanged();
    
private:
    /// Sample the transport in seconds; 0 when there is no engine attached.
    [[nodiscard]] f32 audioPositionSeconds() const;
    /// Queue a position for the next syncNow(); last write wins.
    void seedPendingPosition(f32 time);
    /// Drop any queued position (track change, new lyrics, clear).
    void clearPendingPosition() noexcept;
    void updatePosition(f32 time);
    void detectChanges(const LyricsSyncPosition& oldPos, 
                       const LyricsSyncPosition& newPos);
    void setState(LyricsSyncState newState);
    
    AudioEngine* audio_;
    LyricsData lyrics_;
    LyricsSyncPosition currentPos_;
    LyricsSyncState state_{LyricsSyncState::Idle};
    Config config_;
    
    // Smoothing
    f32 smoothedTime_{0.0f};
    
    // Update timer
    QTimer* updateTimer_;

    // Position seed, consumed by the next syncNow(). seek() and loadLyrics()
    // write here instead of calling updatePosition(), which is what keeps the
    // timer the only writer and stops a seed from being lerped a second time
    // on the following tick.
    f32 pendingTime_{0.0f};
    bool hasPendingTime_{false};
};

/**
 * @brief Utility for interpolating between sync positions
 */
class LyricsSyncInterpolator {
public:
    /**
     * @brief Linear interpolation between positions
     */
    static LyricsSyncPosition lerp(const LyricsSyncPosition& a,
                                   const LyricsSyncPosition& b,
                                   f32 t);
    
    /**
     * @brief Smooth step interpolation (ease in/out)
     */
    static LyricsSyncPosition smoothStep(const LyricsSyncPosition& a,
                                         const LyricsSyncPosition& b,
                                         f32 t);
    
    /**
     * @brief Predict position at future time
     */
    static LyricsSyncPosition predict(const LyricsSyncPosition& current,
                                      f32 deltaTime,
                                      const LyricsData& lyrics);
};

} // namespace vc
