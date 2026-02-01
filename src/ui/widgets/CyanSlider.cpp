/**
 * @file CyanSlider.cpp
 * @brief Modern styled slider implementation
 *
 * Part of the Modern 1337 Chad GUI redesign (P3.005)
 */

#include "CyanSlider.hpp"
#include <QPainter>
#include <QPainterPath>

namespace chadvis {

CyanSlider::CyanSlider(QWidget* parent)
    : QSlider(Qt::Horizontal, parent) {
    setFixedHeight(24);
    
    glowAnimation_ = new QPropertyAnimation(this, "glowOpacity", this);
    glowAnimation_->setDuration(150);
}

void CyanSlider::setHandleSize(int size) {
    handleSize_ = size;
    update();
}

void CyanSlider::setAccentColor(const QColor& color) {
    accentColor_ = color;
    update();
}

void CyanSlider::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    QRect groove = rect().adjusted(4, (height() - trackHeight_) / 2, -4, -(height() - trackHeight_) / 2);
    drawTrack(painter, groove);

    QRect handle = handleRect();
    drawHandle(painter, handle);
}

void CyanSlider::drawTrack(QPainter& painter, const QRect& groove) {
    QPainterPath path;
    path.addRoundedRect(groove, trackHeight_ / 2, trackHeight_ / 2);
    
    // Background track
    painter.fillPath(path, trackColor_);
    
    // Filled portion
    int fillWidth = valueToPixelPos(value()) - groove.left();
    if (fillWidth > 0) {
        QRect fillRect(groove.left(), groove.top(), fillWidth, groove.height());
        QPainterPath fillPath;
        fillPath.addRoundedRect(fillRect, trackHeight_ / 2, trackHeight_ / 2);
        painter.fillPath(fillPath, accentColor_);
    }
}

void CyanSlider::drawHandle(QPainter& painter, const QRect& handleRect) {
    QPainterPath path;
    path.addEllipse(handleRect);
    
    // Glow effect when hovered or pressed
    if ((isHovered_ || isPressed_) && glowEnabled_) {
        qreal glowSize = isPressed_ ? 12.0 : 8.0;
        QColor glowColor = accentColor_;
        glowColor.setAlphaF(isPressed_ ? 0.5 : 0.3);
        
        QRadialGradient gradient(handleRect.center(), handleRect.width() / 2 + glowSize);
        gradient.setColorAt(0, glowColor);
        gradient.setColorAt(1, QColor(0, 0, 0, 0));
        painter.fillRect(handleRect.adjusted(-glowSize, -glowSize, glowSize, glowSize), gradient);
    }
    
    // Handle shadow
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 60));
    painter.drawEllipse(handleRect.translated(0, 2));
    
    // Handle
    painter.setBrush(handleColor_);
    painter.setPen(QPen(accentColor_, 2));
    painter.drawPath(path);
}

QRect CyanSlider::handleRect() const {
    int pos = valueToPixelPos(value());
    int y = (height() - handleSize_) / 2;
    return QRect(pos - handleSize_ / 2, y, handleSize_, handleSize_);
}

int CyanSlider::pixelPosToValue(int pos) const {
    int available = width() - handleSize_ - 8;
    qreal ratio = static_cast<qreal>(pos - 4 - handleSize_ / 2) / available;
    return minimum() + static_cast<int>(ratio * (maximum() - minimum()));
}

int CyanSlider::valueToPixelPos(int value) const {
    int available = width() - handleSize_ - 8;
    qreal ratio = static_cast<qreal>(value - minimum()) / (maximum() - minimum());
    return 4 + handleSize_ / 2 + static_cast<int>(ratio * available);
}

void CyanSlider::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        int newValue = pixelPosToValue(event->pos().x());
        setValue(qBound(minimum(), newValue, maximum()));
        isPressed_ = true;
        update();
    }
    QSlider::mousePressEvent(event);
}

void CyanSlider::mouseReleaseEvent(QMouseEvent* event) {
    isPressed_ = false;
    update();
    QSlider::mouseReleaseEvent(event);
}

void CyanSlider::enterEvent(QEnterEvent* /*event*/) {
    isHovered_ = true;
    emit hoverStateChanged(true);
    if (glowEnabled_) {
        update();
    }
}

void CyanSlider::leaveEvent(QEvent* /*event*/) {
    isHovered_ = false;
    emit hoverStateChanged(false);
    if (glowEnabled_) {
        update();
    }
}

} // namespace chadvis
