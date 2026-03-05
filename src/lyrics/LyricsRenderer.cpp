/**
 * @file LyricsRenderer.cpp
 * @brief Implementation of lyrics renderers.
 */

#include "LyricsRenderer.hpp"
#include "core/Logger.hpp"
#include <QPainterPath>
#include <chrono>

namespace vc {

// Base class utilities

void LyricsRenderer::drawTextWithShadow(QPainter& painter, const QString& text, 
                                       const QPoint& pos, const QColor& color) {
    if (style_.enableShadow) {
        painter.setPen(style_.shadowColor);
        painter.drawText(pos.x() + 2, pos.y() + 2, text);
    }
    painter.setPen(color);
    painter.drawText(pos.x(), pos.y(), text);
}

void LyricsRenderer::drawTextWithGlow(QPainter& painter, const QString& text,
                                     const QPoint& pos, const QColor& color, f32 intensity) {
    if (style_.enableGlow && intensity > 0.01f) {
        QColor glow = style_.glowColor;
        glow.setAlpha(static_cast<int>(glow.alpha() * intensity));
        
        // Draw multiple layers for glow effect
        for (int i = 3; i > 0; --i) {
            painter.setPen(QPen(glow, i * 2));
            painter.drawText(pos.x(), pos.y(), text);
        }
    }
    
    drawTextWithShadow(painter, text, pos, color);
}

void LyricsRenderer::drawProgressBar(QPainter& painter, const QRect& rect, f32 progress,
                                    const QColor& color) {
    if (progress <= 0.0f) return;
    
    int width = static_cast<int>(rect.width() * std::clamp(progress, 0.0f, 1.0f));
    QRect fillRect(rect.left(), rect.top(), width, rect.height());
    
    painter.fillRect(fillRect, color);
}

f32 LyricsRenderer::smoothStep(f32 edge0, f32 edge1, f32 x) const {
    f32 t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

f32 LyricsRenderer::lerp(f32 a, f32 b, f32 t) const {
    return a + (b - a) * t;
}

// KaraokeRenderer implementation

KaraokeRenderer::KaraokeRenderer() {
    style_.font.setPointSize(28);
    style_.font.setBold(true);
    style_.activeColor = QColor(255, 255, 100);  // Yellow
    style_.inactiveColor = QColor(180, 180, 180); // Gray
    style_.enableGlow = true;
    style_.wordByWord = true;
    style_.showUpcoming = true;
    style_.upcomingLines = 2;
    style_.pastLines = 1;
}

QSize KaraokeRenderer::preferredSize() const {
    return QSize(1280, 200);
}

void KaraokeRenderer::render(QPainter& painter, const QRect& rect) {
    if (lyrics_.empty()) {
        painter.setPen(Qt::gray);
        painter.drawText(rect, Qt::AlignCenter, "♪ No lyrics available ♪");
        return;
    }
    
    painter.setFont(style_.font);
    
    if (position_.isInstrumental && showInstrumental_) {
        renderInstrumental(painter, rect);
    } else if (position_.hasLine()) {
        renderActiveLine(painter, rect);
        if (style_.showUpcoming) {
            renderContextLines(painter, rect);
        }
    } else {
        // Before start or after end
        painter.setPen(style_.inactiveColor);
        painter.drawText(rect, Qt::AlignCenter, "♪");
    }
}

void KaraokeRenderer::renderActiveLine(QPainter& painter, const QRect& rect) {
    if (position_.lineIndex < 0 || position_.lineIndex >= static_cast<int>(lyrics_.lines.size())) {
        return;
    }
    
    const auto& line = lyrics_.lines[position_.lineIndex];
    
    // Calculate vertical position
    int centerY = rect.top() + static_cast<int>(rect.height() * verticalPos_);
    
    if (style_.wordByWord && !line.words.empty()) {
        // Word-by-word karaoke rendering
        renderWord(painter, line, rect, centerY);
    } else {
        // Full line rendering with progress bar
        QString text = QString::fromStdString(line.text);
        QFontMetrics fm(style_.font);
        int textWidth = fm.horizontalAdvance(text);
        int x = rect.left() + (rect.width() - textWidth) / 2;
        
        // Draw inactive text first
        painter.setPen(style_.inactiveColor);
        painter.drawText(x, centerY, text);
        
        // Draw progress overlay
        if (line.isSynced && position_.lineProgress > 0.0f) {
            int progressWidth = static_cast<int>(textWidth * position_.lineProgress);
            QRect clipRect(x, centerY - fm.height(), progressWidth, fm.height() * 2);
            
            painter.save();
            painter.setClipRect(clipRect);
            drawTextWithGlow(painter, text, QPoint(x, centerY), style_.activeColor, 
                           position_.lineProgress);
            painter.restore();
        }
    }
}

void KaraokeRenderer::renderWord(QPainter& painter, const LyricsLine& line, 
                                const QRect& rect, int centerY) {
    QFontMetrics fm(style_.font);
    int totalWidth = 0;
    
    // Calculate total width
    for (const auto& word : line.words) {
        totalWidth += fm.horizontalAdvance(QString::fromStdString(word.text + " "));
    }
    
    int startX = rect.left() + (rect.width() - totalWidth) / 2;
    int currentX = startX;
    
    for (size_t i = 0; i < line.words.size(); ++i) {
        const auto& word = line.words[i];
        QString wordText = QString::fromStdString(word.text + " ");
int wordWidth = fm.horizontalAdvance(wordText);

    bool isPast = word.endTime < position_.time;
    bool isCurrent = word.containsTime(position_.time);
        
        QColor color;
        f32 glowIntensity = 0.0f;
        
        if (isPast) {
            color = style_.activeColor;
            color.setAlpha(200);
        } else if (isCurrent) {
            color = style_.activeColor;
            glowIntensity = 1.0f;
            
            // Add progress-based intensity variation
            f32 wordProgress = word.getProgress(position_.time);
            glowIntensity = 0.5f + wordProgress * 0.5f;
        } else {
            color = style_.inactiveColor;
            // Fade in upcoming words
            f32 timeUntil = word.startTime - position_.time;
            if (timeUntil < 1.0f) {
                f32 fadeIn = 1.0f - timeUntil;
                color = lerpColor(style_.inactiveColor, style_.activeColor, fadeIn * 0.3f);
            }
        }
        
        QPoint pos(currentX, centerY);
        
        if (isCurrent && style_.enableGlow) {
            drawTextWithGlow(painter, wordText, pos, color, glowIntensity);
        } else {
            drawTextWithShadow(painter, wordText, pos, color);
        }
        
        currentX += wordWidth;
    }
}

void KaraokeRenderer::renderContextLines(QPainter& painter, const QRect& rect) {
    if (position_.lineIndex < 0) return;
    
    QFontMetrics fm(style_.font);
    int lineHeight = fm.height() + style_.lineSpacing;
    int centerY = rect.top() + static_cast<int>(rect.height() * verticalPos_);
    
    // Render past lines
    for (int i = 1; i <= style_.pastLines; ++i) {
        int lineIdx = position_.lineIndex - i;
        if (lineIdx < 0) break;
        
        const auto& line = lyrics_.lines[lineIdx];
        QString text = QString::fromStdString(line.text);
        int textWidth = fm.horizontalAdvance(text);
        int x = rect.left() + (rect.width() - textWidth) / 2;
        int y = centerY - (i * lineHeight);
        
        // Fade out based on distance
        f32 fade = 1.0f - (static_cast<f32>(i) / (style_.pastLines + 1));
        QColor color = style_.inactiveColor;
        color.setAlpha(static_cast<int>(color.alpha() * fade * 0.5f));
        
        painter.setPen(color);
        painter.drawText(x, y, text);
    }
    
    // Render upcoming lines
    for (int i = 1; i <= style_.upcomingLines; ++i) {
        int lineIdx = position_.lineIndex + i;
        if (lineIdx >= static_cast<int>(lyrics_.lines.size())) break;
        
        const auto& line = lyrics_.lines[lineIdx];
        QString text = QString::fromStdString(line.text);
        int textWidth = fm.horizontalAdvance(text);
        int x = rect.left() + (rect.width() - textWidth) / 2;
        int y = centerY + (i * lineHeight);
        
        // Fade in based on proximity
        f32 timeUntil = line.startTime - position_.time;
        f32 alpha = 1.0f;
        if (timeUntil > 2.0f) {
            alpha = 0.3f;
        } else if (timeUntil > 0.0f) {
            alpha = 0.3f + (2.0f - timeUntil) / 2.0f * 0.7f;
        }
        
        QColor color = style_.inactiveColor;
        color.setAlpha(static_cast<int>(color.alpha() * alpha));
        
        painter.setPen(color);
        painter.drawText(x, y, text);
    }
}

void KaraokeRenderer::renderInstrumental(QPainter& painter, const QRect& rect) {
    // Draw pulsing "♪ Instrumental ♪"
    auto now = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    
    // Pulse effect: 1 second cycle
    f32 pulse = static_cast<f32>(ms % 1000) / 1000.0f;
    f32 intensity = 0.5f + 0.5f * std::sin(pulse * 2.0f * 3.14159f);
    
    QString text = "♪ Instrumental ♪";
    QFontMetrics fm(style_.font);
    int textWidth = fm.horizontalAdvance(text);
    int x = rect.left() + (rect.width() - textWidth) / 2;
    int y = rect.top() + rect.height() / 2;
    
    QColor color = style_.activeColor;
    color.setAlpha(static_cast<int>(150 + 105 * intensity));
    
    drawTextWithGlow(painter, text, QPoint(x, y), color, intensity);
}

QColor KaraokeRenderer::lerpColor(const QColor& a, const QColor& b, f32 t) {
    return QColor(
        static_cast<int>(a.red() + (b.red() - a.red()) * t),
        static_cast<int>(a.green() + (b.green() - a.green()) * t),
        static_cast<int>(a.blue() + (b.blue() - a.blue()) * t),
        static_cast<int>(a.alpha() + (b.alpha() - a.alpha()) * t)
    );
}

// PanelRenderer implementation

PanelRenderer::PanelRenderer() {
    style_.font.setPointSize(16);
    style_.activeColor = QColor(255, 255, 0);
    style_.inactiveColor = QColor(200, 200, 200);
    style_.backgroundColor = QColor(30, 30, 30);
}

QSize PanelRenderer::preferredSize() const {
    return QSize(400, 600);
}

void PanelRenderer::render(QPainter& painter, const QRect& rect) {
    // Background
    painter.fillRect(rect, style_.backgroundColor);
    
    if (lyrics_.empty()) {
        painter.setPen(Qt::gray);
        painter.drawText(rect, Qt::AlignCenter, "No lyrics loaded");
        return;
    }
    
    painter.setFont(style_.font);
    QFontMetrics fm(style_.font);
    lineHeight_ = fm.height() + style_.lineSpacing;
    
    // Auto-scroll
    if (autoScroll_ && position_.hasLine()) {
        updateScrollPosition();
    }
    
    // Smooth scroll interpolation
    scrollOffset_ = static_cast<int>(lerp(static_cast<f32>(scrollOffset_), 
                                          static_cast<f32>(targetScroll_), 
                                          style_.animationSpeed));
    
    // Calculate visible range
    int startY = rect.top() + 20 - scrollOffset_;
    lineRects_.clear();
    lineRects_.reserve(lyrics_.lines.size());
    
    for (size_t i = 0; i < lyrics_.lines.size(); ++i) {
        int y = startY + static_cast<int>(i) * lineHeight_;
        
        // Skip if outside visible area
        if (y + lineHeight_ < rect.top() || y > rect.bottom()) {
            lineRects_.emplace_back();
            continue;
        }
        
        bool isActive = (static_cast<int>(i) == position_.lineIndex);
        QRect lineRect(rect.left() + 10, y, rect.width() - 20, lineHeight_);
        lineRects_.push_back(lineRect);
        
        renderLine(painter, i, lineRect, isActive);
    }
}

void PanelRenderer::renderLine(QPainter& painter, size_t lineIndex, 
                              const QRect& rect, bool isActive) {
    const auto& line = lyrics_.lines[lineIndex];
    QString text = QString::fromStdString(line.text);
    
    if (isActive) {
        // Highlight background
        painter.fillRect(rect.adjusted(-5, -2, 5, 2), QColor(60, 60, 40));
        
        // Active text with glow
        drawTextWithGlow(painter, text, rect.topLeft(), style_.activeColor, 0.8f);
        
        // Progress indicator
        if (line.isSynced) {
            int progressWidth = static_cast<int>(rect.width() * position_.lineProgress);
            painter.fillRect(rect.left(), rect.bottom() - 2, progressWidth, 2, 
                           style_.activeColor);
        }
    } else {
        // Inactive text
        painter.setPen(style_.inactiveColor);
        painter.drawText(rect.topLeft(), text);
    }
}

void PanelRenderer::updateScrollPosition() {
    if (position_.lineIndex < 0) return;
    
    // Center the active line
    int targetY = position_.lineIndex * lineHeight_;
    targetScroll_ = targetY - 100; // Offset to center
    
    if (targetScroll_ < 0) targetScroll_ = 0;
}

QRect PanelRenderer::getLineRect(size_t lineIndex) const {
    if (lineIndex < lineRects_.size()) {
        return lineRects_[lineIndex];
    }
    return QRect();
}

int PanelRenderer::handleClick(const QPoint& pos) const {
    for (size_t i = 0; i < lineRects_.size(); ++i) {
        if (lineRects_[i].contains(pos)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// LyricsOverlayRenderer implementation

LyricsOverlayRenderer::LyricsOverlayRenderer() {
    style_.font.setPointSize(20);
    style_.enableGlow = false;
    style_.enableShadow = true;
    style_.enableAnimations = false; // No animations for video
}

QSize LyricsOverlayRenderer::preferredSize() const {
    return QSize(640, 60);
}

bool LyricsOverlayRenderer::needsRepaint() {
    // Only repaint on line/word changes, not every frame
    bool needsUpdate = (position_.lineIndex != lastLine_) || 
                       (position_.wordIndex != lastWord_);
    
    lastLine_ = position_.lineIndex;
    lastWord_ = position_.wordIndex;
    
    return needsUpdate;
}

void LyricsOverlayRenderer::render(QPainter& painter, const QRect& rect) {
    if (lyrics_.empty() || position_.lineIndex < 0) return;
    
    const auto& line = lyrics_.lines[position_.lineIndex];
    QString text = QString::fromStdString(line.text);
    
    // Calculate position
    int x = rect.left() + static_cast<int>(rect.width() * posX_);
    int y = rect.top() + static_cast<int>(rect.height() * posY_);
    
    QFontMetrics fm(style_.font);
    int textWidth = fm.horizontalAdvance(text);
    x -= textWidth / 2; // Center horizontally
    
    // Semi-transparent background for readability
    QRect bgRect(x - 10, y - fm.height(), textWidth + 20, fm.height() + 10);
    QColor bg = style_.backgroundColor;
    bg.setAlpha(static_cast<int>(bg.alpha() * opacity_));
    painter.fillRect(bgRect, bg);
    
    // Draw text
    QColor textColor = style_.activeColor;
    textColor.setAlpha(static_cast<int>(255 * opacity_));
    
    drawTextWithShadow(painter, text, QPoint(x, y), textColor);
}

// Factory implementation

std::unique_ptr<LyricsRenderer> LyricsRendererFactory::create(Type type) {
    switch (type) {
    case Type::Karaoke:
        return createKaraoke();
    case Type::Panel:
        return createPanel();
    case Type::Overlay:
        return createOverlay();
    }
    return nullptr;
}

std::unique_ptr<LyricsRenderer> LyricsRendererFactory::createKaraoke() {
    return std::make_unique<KaraokeRenderer>();
}

std::unique_ptr<LyricsRenderer> LyricsRendererFactory::createPanel() {
    return std::make_unique<PanelRenderer>();
}

std::unique_ptr<LyricsRenderer> LyricsRendererFactory::createOverlay() {
    return std::make_unique<LyricsOverlayRenderer>();
}

} // namespace vc
