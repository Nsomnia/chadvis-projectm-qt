/**
 * @file LyricsPanel.hpp
 * @brief Lyrics panel canvas tab for displaying song lyrics.
 *
 * A scrollable panel that displays lyrics with the current line
 * highlighted. Supports click-to-seek and search within lyrics.
 *
 * @author ChadVis Agent
 * @version 11.0 (1337 Edition)
 */

#pragma once
#include <QWidget>
#include <QScrollArea>
#include <QLineEdit>
#include <memory>
#include "lyrics/LyricsData.hpp"
#include "lyrics/LyricsSync.hpp"

namespace vc {

// Forward declarations
class AudioEngine;
namespace suno {
class SunoController;
}

/**
 * @brief Lyrics display panel
 *
 * Provides a scrollable view of lyrics with:
 * - Current line highlighting
 * - Click to seek
 * - Search within lyrics
 * - Auto-scroll following playback
 */
class LyricsPanel : public QWidget {
    Q_OBJECT
    
public:
    explicit LyricsPanel(AudioEngine* audio, 
                        suno::SunoController* suno,
                        QWidget* parent = nullptr);
    ~LyricsPanel() override;
    
    /**
     * @brief Load lyrics for a song
     */
    void loadLyrics(const LyricsData& lyrics);
    
    /**
     * @brief Clear current lyrics
     */
    void clear();
    
    /**
     * @brief Set whether to follow playback position
     */
    void setFollowPlayback(bool follow);
    bool isFollowingPlayback() const { return followPlayback_; }
    
    /**
     * @brief Search within lyrics
     */
    void search(const QString& query);
    void clearSearch();
    
    /**
     * @brief Jump to specific line
     */
    void jumpToLine(int lineIndex);
    
    /**
     * @brief Get current lyrics
     */
    const LyricsData& getLyrics() const { return lyrics_; }
    
    /**
     * @brief Check if lyrics are loaded
     */
    bool hasLyrics() const { return !lyrics_.empty(); }

signals:
    void lineClicked(int lineIndex, const QString& text);
    void searchResultsFound(int count);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private slots:
    void onPositionChanged(const LyricsSyncPosition& pos);
    void onSearchTextChanged(const QString& text);
    void onAudioTrackChanged();

private:
    void setupUI();
    void updateLayout();
    void renderLyrics(QPainter& painter);
    int lineIndexAt(const QPoint& pos) const;
    void ensureLineVisible(int lineIndex);
    void highlightSearchResults(QPainter& painter);
    
protected:
    // Data
    LyricsData lyrics_;
    std::unique_ptr<LyricsSync> sync_;
    AudioEngine* audio_;
    suno::SunoController* suno_;
    
    // UI
    QScrollArea* scrollArea_;
    QWidget* contentWidget_;
    QLineEdit* searchEdit_;
    
    // Layout
    struct LineInfo {
        QRect rect;
        QString text;
        bool isActive{false};
        bool isSearchResult{false};
    };
    std::vector<LineInfo> lineInfo_;
    int lineHeight_{30};
    int contentHeight_{0};
    
    // State
    bool followPlayback_{true};
    int currentLine_{-1};
    QString searchQuery_;
    std::vector<size_t> searchResults_;
    int currentSearchResult_{-1};
    
    // Style
    QFont font_{"Arial", 14};
    QColor activeColor_{255, 255, 0};      // Yellow
    QColor inactiveColor_{200, 200, 200};  // Gray
    QColor bgColor_{30, 30, 30};           // Dark gray
    QColor highlightColor_{60, 60, 40};    // Brown highlight
    QColor searchHighlightColor_{0, 100, 200}; // Blue for search
};

/**
 * @brief Compact lyrics panel for tool windows
 *
 * Smaller version for embedding in side panels.
 */
class CompactLyricsPanel : public LyricsPanel {
    Q_OBJECT
    
public:
    explicit CompactLyricsPanel(AudioEngine* audio,
                               suno::SunoController* suno,
                               QWidget* parent = nullptr);
    
    void setCompactMode(bool compact);

protected:
    void paintEvent(QPaintEvent* event) override;
};

} // namespace vc
