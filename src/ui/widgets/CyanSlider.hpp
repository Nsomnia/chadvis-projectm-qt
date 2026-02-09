/**
 * @file CyanSlider.hpp
 * @brief Modern styled slider with cyan accent
 *
 * Custom QSlider implementation with modern aesthetics:
 * - Cyan accent (#00bcd4) for filled portion
 * - Dark track (#3d3d3d) for unfilled portion
 * - Rounded handle with glow effect on hover
 * - Smooth animations and transitions
 *
 * Part of the Modern 1337 Chad GUI redesign (P3.005)
 *
 * @version 1.0.0
 * @author ChadVis Team
 */

#pragma once

#include <QSlider>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPropertyAnimation>

namespace chadvis {

/**
 * @brief Modern styled horizontal slider with cyan accent
 *
 * Features:
 * - Custom paint event for full control over appearance
 * - Glow effect on handle hover
 * - Smooth value transitions
 * - Modern rounded aesthetics
 */
class CyanSlider : public QSlider {
    Q_OBJECT
    Q_PROPERTY(int handleSize READ handleSize WRITE setHandleSize)
    Q_PROPERTY(QColor accentColor READ accentColor WRITE setAccentColor)
    Q_PROPERTY(qreal glowOpacity READ glowOpacity WRITE setGlowOpacity)

public:
    explicit CyanSlider(QWidget* parent = nullptr);
    ~CyanSlider() override = default;

    /**
     * @brief Set the handle size (diameter)
     */
    void setHandleSize(int size);
    int handleSize() const { return handleSize_; }

    /**
     * @brief Set the accent color (filled portion)
     */
    void setAccentColor(const QColor& color);
    QColor accentColor() const { return accentColor_; }

    /**
     * @brief Set whether to show glow effect
     */
    void setGlowEnabled(bool enabled) { glowEnabled_ = enabled; }
    bool isGlowEnabled() const { return glowEnabled_; }

signals:
    /**
     * @brief Emitted when hover state changes
     */
    void hoverStateChanged(bool hovered);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    void drawTrack(QPainter& painter, const QRect& groove);
    void drawHandle(QPainter& painter, const QRect& handleRect);
    QRect handleRect() const;
    int pixelPosToValue(int pos) const;
    int valueToPixelPos(int value) const;
    
    qreal glowOpacity() const { return glowOpacity_; }
    void setGlowOpacity(qreal opacity);
    void animateGlow(qreal targetOpacity);

    // Configuration
    int handleSize_ = 16;
    int trackHeight_ = 6;
    QColor accentColor_ = QColor("#00bcd4");
    QColor trackColor_ = QColor("#3d3d3d");
    QColor handleColor_ = QColor("#ffffff");

    // State
    bool isHovered_ = false;
    bool isPressed_ = false;
    bool glowEnabled_ = true;
    qreal glowOpacity_ = 0.0;

    // Animation
    QPropertyAnimation* glowAnimation_;
};

} // namespace chadvis
