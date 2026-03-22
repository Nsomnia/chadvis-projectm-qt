/**
 * @file GlowButton.hpp
 * @brief Modern button with glassmorphism and glow effects
 *
 * Custom QPushButton with modern aesthetics:
 * - Glassmorphism transparency effect
 * - Cyan border glow (#00bcd4)
 * - Rounded corners (8px radius)
 * - Press and hover animations
 *
 * Part of the Modern 1337 Chad GUI redesign (P3.005)
 *
 * @version 1.0.0
 */

#pragma once

#include <QPushButton>
#include <QPropertyAnimation>

namespace chadvis {

/**
 * @brief Modern button with glow effects
 *
 * Features glassmorphism styling with cyan accent glow.
 * Supports hover intensification and press feedback.
 */
class GlowButton : public QPushButton {
    Q_OBJECT
    Q_PROPERTY(qreal glowIntensity READ glowIntensity WRITE setGlowIntensity)

public:
    explicit GlowButton(QWidget* parent = nullptr);
    explicit GlowButton(const QString& text, QWidget* parent = nullptr);
    ~GlowButton() override = default;

    /**
     * Set the glow color (default: cyan #00bcd4)
     */
    void setGlowColor(const QColor& color);
    QColor glowColor() const { return glowColor_; }

    /**
     * Set background opacity for glassmorphism (0.0-1.0)
     */
    void setBackgroundOpacity(qreal opacity);
    qreal backgroundOpacity() const { return backgroundOpacity_; }

    /**
     * Set corner radius (default: 8px)
     */
    void setCornerRadius(int radius);
    int cornerRadius() const { return cornerRadius_; }

    /**
     * Enable/disable glassmorphism effect
     */
    void setGlassmorphismEnabled(bool enabled) { glassmorphismEnabled_ = enabled; }
    bool isGlassmorphismEnabled() const { return glassmorphismEnabled_; }

signals:
    void glowIntensityChanged(qreal intensity);

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    qreal glowIntensity() const { return glowIntensity_; }
    void setGlowIntensity(qreal intensity);
    void animateGlow(qreal targetIntensity);

    // Configuration
    QColor glowColor_ = QColor("#00bcd4");
    QColor backgroundColor_ = QColor("#2a2a2a");
    qreal backgroundOpacity_ = 0.8;
    int cornerRadius_ = 8;
    bool glassmorphismEnabled_ = true;

    // State
    qreal glowIntensity_ = 0.3;
    bool isHovered_ = false;
    bool isPressed_ = false;
    qreal scale_ = 1.0;

    // Animation
    QPropertyAnimation* glowAnimation_;
};

} // namespace chadvis
