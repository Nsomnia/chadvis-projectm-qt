/**
 * @file LyricsRenderer.hpp
 * @brief Base class and implementations for lyrics rendering.
 *
 * Provides multiple rendering strategies:
 * - KaraokeRenderer: Word-level highlighting with effects
 * - PanelRenderer: Scrolling text view
 * - OverlayRenderer: For video recording integration
 *
 * Pattern: Strategy + Template Method
 *
 * @author ChadVis Agent
 * @version 11.0 (Eye Candy Edition)
 */

#pragma once
#include <QFont>
#include <QColor>
#include <QPainter>
#include <memory>
#include "LyricsData.hpp"
#include "LyricsSync.hpp"

namespace vc {

/**
 * @brief Base class for all lyrics renderers
 *
 * Defines the interface and common functionality for rendering
 * lyrics in different contexts (karaoke, panel, overlay).
 */
class LyricsRenderer {
public:
    virtual ~LyricsRenderer() = default;
    
    /**
     * @brief Set lyrics data to render
     */
    virtual void setLyrics(const LyricsData& lyrics) { lyrics_ = lyrics; }
    
    /**
     * @brief Update current sync position
     */
    virtual void updatePosition(const LyricsSyncPosition& pos) { position_ = pos; }
    
    /**
     * @brief Render to painter
     * @param painter QPainter to render to
     * @param rect Target rectangle
     */
    virtual void render(QPainter& painter, const QRect& rect) = 0;
    
    /**
     * @brief Get preferred size (if applicable)
     */
    virtual QSize preferredSize() const { return QSize(800, 600); }
    
    /**
     * @brief Check if renderer needs repainting
     */
    virtual bool needsRepaint() { return true; }
    
    /**
     * @brief Set rendering configuration
     */
    struct Style {
        QFont font{"Arial", 24};
        QColor activeColor{255, 255, 0};       ///< Yellow for active
        QColor inactiveColor{200, 200, 200};   ///< Gray for inactive
        QColor backgroundColor{0, 0, 0, 180};  ///< Semi-transparent black
        QColor glowColor{255, 255, 0, 100};    ///< Glow effect
        QColor shadowColor{0, 0, 0, 200};      ///< Shadow
        
        int lineSpacing{10};                   ///< Pixels between lines
        int wordSpacing{4};                    ///< Pixels between words
        bool enableGlow{true};                 ///< Glow effect on active
        bool enableShadow{true};               ///< Shadow effect
        bool enableAnimations{true};           ///< Smooth transitions
        f32 animationSpeed{0.3f};              ///< Transition speed (0.0-1.0)
        
        // Karaoke-specific
        bool wordByWord{true};                 ///< Highlight individual words
        bool showUpcoming{true};               ///< Show upcoming lines
        int upcomingLines{2};                  ///< Number of upcoming lines
        int pastLines{1};                      ///< Number of past lines to show
    };
    virtual void setStyle(const Style& style) { style_ = style; }
    Style getStyle() const { return style_; }
    
protected:
    LyricsData lyrics_;
    LyricsSyncPosition position_;
    Style style_;
    
    // Utility functions for derived classes
    void drawTextWithShadow(QPainter& painter, const QString& text, 
                           const QPoint& pos, const QColor& color);
    void drawTextWithGlow(QPainter& painter, const QString& text,
                         const QPoint& pos, const QColor& color, f32 intensity);
    void drawProgressBar(QPainter& painter, const QRect& rect, f32 progress,
                        const QColor& color);
    
    // Animation helpers
    f32 smoothStep(f32 edge0, f32 edge1, f32 x) const;
    f32 lerp(f32 a, f32 b, f32 t) const;
};

/**
 * @brief Karaoke-style renderer with word highlighting
 *
 * Renders lyrics with active words highlighted, upcoming words
 * dimmed, and past words shown with reduced opacity.
 */
class KaraokeRenderer : public LyricsRenderer {
public:
    KaraokeRenderer();
    ~KaraokeRenderer() override = default;
    
    void render(QPainter& painter, const QRect& rect) override;
    QSize preferredSize() const override;
    
    /**
     * @brief Set vertical position (0.0 = top, 0.5 = center, 1.0 = bottom)
     */
    void setVerticalPosition(f32 position) { verticalPos_ = position; }
    
    /**
     * @brief Enable/disable instrumental break display
     */
    void setShowInstrumental(bool show) { showInstrumental_ = show; }

private:
    void renderActiveLine(QPainter& painter, const QRect& rect);
    void renderContextLines(QPainter& painter, const QRect& rect);
    void renderInstrumental(QPainter& painter, const QRect& rect);
    void renderWord(QPainter& painter, const LyricsLine& line, 
                   const QRect& rect, int centerY);
    
    QColor lerpColor(const QColor& a, const QColor& b, f32 t);
    
    f32 verticalPos_{0.5f};     ///< Vertical position (0.0-1.0)
    bool showInstrumental_{true};
    
    // Animation state
    struct AnimationState {
        int lastLine{-1};
        int lastWord{-1};
        f32 wordProgress{0.0f};
        std::chrono::steady_clock::time_point lastUpdate;
    };
    AnimationState animState_;
};

/**
 * @brief Panel-style renderer for scrollable lyrics
 *
 * Renders lyrics in a scrollable view with current line highlighted.
 * Suitable for a dedicated lyrics panel or tool window.
 */
class PanelRenderer : public LyricsRenderer {
public:
    PanelRenderer();
    ~PanelRenderer() override = default;
    
    void render(QPainter& painter, const QRect& rect) override;
    QSize preferredSize() const override;
    
    /**
     * @brief Set scroll offset (for manual scrolling)
     */
    void setScrollOffset(int offset) { scrollOffset_ = offset; }
    int getScrollOffset() const { return scrollOffset_; }
    
    /**
     * @brief Auto-scroll to keep current line visible
     */
    void setAutoScroll(bool autoScroll) { autoScroll_ = autoScroll; }
    
    /**
     * @brief Get line rectangle (for click handling)
     */
    QRect getLineRect(size_t lineIndex) const;
    
    /**
     * @brief Handle mouse click at position
     * @return Line index clicked, or -1 if none
     */
    int handleClick(const QPoint& pos) const;

private:
    void renderLine(QPainter& painter, size_t lineIndex, 
                   const QRect& rect, bool isActive);
    void updateScrollPosition();
    
    int scrollOffset_{0};       ///< Manual scroll offset
    int targetScroll_{0};       ///< Target for smooth scrolling
    bool autoScroll_{true};     ///< Auto-scroll to current line
    int lineHeight_{30};        ///< Height of each line
    
    // Layout cache
    std::vector<QRect> lineRects_;
};

/**
 * @brief Overlay renderer for video recording
 *
 * Minimal, clean rendering suitable for overlay on video.
 * No animations, simple text rendering for encoding performance.
 */
class OverlayRenderer : public LyricsRenderer {
public:
    OverlayRenderer();
    ~OverlayRenderer() override = default;
    
    void render(QPainter& painter, const QRect& rect) override;
    QSize preferredSize() const override;
    bool needsRepaint() override;
    
    /**
     * @brief Set opacity (0.0-1.0)
     */
    void setOpacity(f32 opacity) { opacity_ = opacity; }
    
    /**
     * @brief Set position (0.0-1.0 for both axes)
     */
    void setPosition(f32 x, f32 y) { posX_ = x; posY_ = y; }

private:
    f32 opacity_{1.0f};
    f32 posX_{0.5f};    ///< Horizontal position (0.0 = left, 1.0 = right)
    f32 posY_{0.85f};   ///< Vertical position (0.0 = top, 1.0 = bottom)
    
    // Last rendered state (for needsRepaint optimization)
    int lastLine_{-1};
    int lastWord_{-1};
};

/**
 * @brief Renderer factory
 */
class LyricsRendererFactory {
public:
    enum class Type {
        Karaoke,
        Panel,
        Overlay
    };
    
    static std::unique_ptr<LyricsRenderer> create(Type type);
    
    static std::unique_ptr<LyricsRenderer> createKaraoke();
    static std::unique_ptr<LyricsRenderer> createPanel();
    static std::unique_ptr<LyricsRenderer> createOverlay();
};

} // namespace vc
