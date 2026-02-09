/**
 * @file GlowButton.cpp
 * @brief Modern button implementation with glow effects
 *
 * Part of the Modern 1337 Chad GUI redesign (P3.005)
 */

#include "GlowButton.hpp"
#include <QPainter>
#include <QPainterPath>

namespace chadvis {

GlowButton::GlowButton(QWidget* parent)
    : QPushButton(parent) {
    setFixedHeight(36);
    glowAnimation_ = new QPropertyAnimation(this, "glowIntensity", this);
    glowAnimation_->setDuration(150);
    glowAnimation_->setEasingCurve(QEasingCurve::OutCubic);
}

GlowButton::GlowButton(const QString& text, QWidget* parent)
    : QPushButton(text, parent) {
    setFixedHeight(36);
    glowAnimation_ = new QPropertyAnimation(this, "glowIntensity", this);
    glowAnimation_->setDuration(150);
    glowAnimation_->setEasingCurve(QEasingCurve::OutCubic);
}

void GlowButton::setGlowColor(const QColor& color) {
    glowColor_ = color;
    update();
}

void GlowButton::setBackgroundOpacity(qreal opacity) {
    backgroundOpacity_ = qBound(0.0, opacity, 1.0);
    update();
}

void GlowButton::setCornerRadius(int radius) {
    cornerRadius_ = radius;
    update();
}

void GlowButton::setGlowIntensity(qreal intensity) {
    glowIntensity_ = intensity;
    update();
}

void GlowButton::animateGlow(qreal targetIntensity) {
    glowAnimation_->stop();
    glowAnimation_->setStartValue(glowIntensity_);
    glowAnimation_->setEndValue(targetIntensity);
    glowAnimation_->start();
}

void GlowButton::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    
    QRect rect = this->rect().adjusted(1, 1, -1, -1);
    QPainterPath path;
    path.addRoundedRect(rect, cornerRadius_, cornerRadius_);
    
    // Background with glassmorphism
    QColor bg = backgroundColor_;
    if (glassmorphismEnabled_) {
        bg.setAlphaF(backgroundOpacity_);
    }
    painter.fillPath(path, bg);
    
    // Glow border effect
    QColor glow = glowColor_;
    qreal intensity = glowIntensity_;
    if (isPressed_) {
        intensity *= 1.5;
    }
    glow.setAlphaF(qBound(0.0, intensity, 1.0));
    
    QPen pen(glow, 2);
    painter.setPen(pen);
    painter.drawPath(path);
    
    // Outer glow when hovered
    if (isHovered_ || isPressed_) {
        qreal glowSpread = isPressed_ ? 6.0 : 4.0;
        QColor outerGlow = glowColor_;
        outerGlow.setAlphaF(qBound(0.0, isPressed_ ? 0.4 : 0.2, 1.0));
        
        for (int i = 1; i <= (int)glowSpread; ++i) {
            QColor fade = outerGlow;
            fade.setAlphaF(qBound(0.0, outerGlow.alphaF() * (1.0 - i / glowSpread), 1.0));
            QPen glowPen(fade, i);
            painter.setPen(glowPen);
            QPainterPath glowPath;
            glowPath.addRoundedRect(rect.adjusted(-i/2, -i/2, i/2, i/2), 
                                    cornerRadius_ + i/2, cornerRadius_ + i/2);
            painter.drawPath(glowPath);
        }
    }
    
    // Button text
    painter.setPen(isEnabled() ? QColor("#ffffff") : QColor("#808080"));
    painter.drawText(rect, Qt::AlignCenter, text());
}

void GlowButton::enterEvent(QEnterEvent* /*event*/) {
    isHovered_ = true;
    animateGlow(0.8);
}

void GlowButton::leaveEvent(QEvent* /*event*/) {
    isHovered_ = false;
    animateGlow(0.3);
}

void GlowButton::mousePressEvent(QMouseEvent* event) {
    isPressed_ = true;
    scale_ = 0.95;
    update();
    QPushButton::mousePressEvent(event);
}

void GlowButton::mouseReleaseEvent(QMouseEvent* event) {
    isPressed_ = false;
    scale_ = 1.0;
    update();
    QPushButton::mouseReleaseEvent(event);
}

} // namespace chadvis
