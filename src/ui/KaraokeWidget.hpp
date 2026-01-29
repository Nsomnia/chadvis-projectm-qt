/**
 * @file KaraokeWidget.hpp
 * @brief Karaoke display widget with word-level highlighting.
 *
 * Rewritten using the LyricsRenderer system for smooth 60fps
 * word-level karaoke highlighting with glow effects.
 *
 * @author ChadVis Agent
 * @version 11.0 (Silky Smooth Edition)
 */

#pragma once
#include <QWidget>
#include <memory>
#include "lyrics/LyricsData.hpp"
#include "lyrics/LyricsSync.hpp"
#include "lyrics/LyricsRenderer.hpp"

namespace vc {

// Forward declarations
namespace suno {
class SunoController;
}
class AudioEngine;

/**
 * @brief Karaoke display widget
 *
 * Provides eye-catching karaoke-style lyrics with:
 * - Word-level highlighting
 * - Glow effects on active words
 * - Smooth 60fps animations
 * - Context lines (past/upcoming)
 * - Instrumental break display
 */
class KaraokeWidget : public QWidget {
    Q_OBJECT
    
public:
    explicit KaraokeWidget(suno::SunoController* suno, 
                          AudioEngine* audio,
                          QWidget* parent = nullptr);
    ~KaraokeWidget() override;
    
    /**
     * @brief Load lyrics for display
     */
    void setLyrics(const LyricsData& lyrics);
    
    /**
     * @brief Clear current lyrics
     */
    void clear();
    
    /**
     * @brief Update current time position
     */
    void updateTime(f32 time);
    
    /**
     * @brief Set vertical position (0.0 = top, 0.5 = center, 1.0 = bottom)
     */
    void setVerticalPosition(f32 position);
    
    /**
     * @brief Get current lyrics
     */
    const LyricsData& getLyrics() const { return lyrics_; }
    
    /**
     * @brief Check if lyrics are loaded
     */
    bool hasLyrics() const { return !lyrics_.empty(); }

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void onPositionChanged(const LyricsSyncPosition& pos);
    void onAudioTrackChanged();

private:
    void setupConnections();
    
    suno::SunoController* sunoController_;
    AudioEngine* audioEngine_;
    
    // New lyrics system
    LyricsData lyrics_;
    std::unique_ptr<LyricsSync> sync_;
    std::unique_ptr<KaraokeRenderer> renderer_;
    
    // Animation timer for smooth updates
    QTimer* updateTimer_;
};

} // namespace vc
