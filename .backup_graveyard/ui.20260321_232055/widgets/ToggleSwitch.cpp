/**
 * @file ToggleSwitch.cpp
 * @brief Modern toggle switch implementation
 *
 * Part of the Modern 1337 Chad GUI redesign (P3.005)
 */

#include "ToggleSwitch.hpp"
#include <QPainter>
#include <QPainterPath>
#include <QKeyEvent>

namespace chadvis {

ToggleSwitch::ToggleSwitch(QWidget* parent)
    : QAbstractButton(parent) {
    setCheckable(true);
    setFixedSize(trackWidth_ + 4, trackHeight_ + 4);
    
    positionAnimation_ = new QPropertyAnimation(this, "thumbPosition", this);
    positionAnimation_->setDuration(150);
    positionAnimation_->setEasingCurve(QEasingCurve::InOutCubic);
    
    connect(this, &QAbstractButton::toggled, this, &ToggleSwitch::animateToggle);
}

void ToggleSwitch::setOnColor(const QColor& color) {
    onColor_ = color;
    update();
}

void ToggleSwitch::setOffColor(const QColor& color) {
    offColor_ = color;
    update();
}

void ToggleSwitch::setThumbColor(const QColor& color) {
    thumbColor_ = color;
    update();
}

void ToggleSwitch::setSwitchSize(int width, int height) {
    trackWidth_ = width;
    trackHeight_ = height;
    thumbSize_ = height - 2 * thumbMargin_;
    setFixedSize(width + 4, height + 4);
    update();
}

QSize ToggleSwitch::sizeHint() const {
    return QSize(trackWidth_ + 4, trackHeight_ + 4);
}

void ToggleSwitch::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    
    // Calculate centered rect
    int x = (width() - trackWidth_) / 2;
    int y = (height() - trackHeight_) / 2;
    QRect trackRect(x, y, trackWidth_, trackHeight_);
    
    // Draw track background
    QPainterPath trackPath;
    trackPath.addRoundedRect(trackRect, trackHeight_ / 2, trackHeight_ / 2);
    
    // Interpolate color based on thumb position
    QColor trackColor;
    if (thumbPosition_ < 0.5) {
        trackColor = offColor_;
    } else {
        int r = offColor_.red() + (onColor_.red() - offColor_.red()) * (thumbPosition_ - 0.5) * 2;
        int g = offColor_.green() + (onColor_.green() - offColor_.green()) * (thumbPosition_ - 0.5) * 2;
        int b = offColor_.blue() + (onColor_.blue() - offColor_.blue()) * (thumbPosition_ - 0.5) * 2;
        trackColor = QColor(r, g, b);
    }
    painter.fillPath(trackPath, trackColor);
    
    // Draw track border
    QPen borderPen(QColor(255, 255, 255, 40));
    borderPen.setWidth(1);
    painter.setPen(borderPen);
    painter.drawPath(trackPath);
    
    // Draw labels if enabled
    if (labelsEnabled_) {
        painter.setPen(QColor("#ffffff"));
        QFont font = painter.font();
        font.setPointSize(8);
        font.setBold(true);
        painter.setFont(font);
        
        // ON label
        painter.drawText(trackRect.left() + 6, trackRect.top(), 
                        trackWidth_ / 2 - 6, trackHeight_, 
                        Qt::AlignLeft | Qt::AlignVCenter, "ON");
        
        // OFF label
        painter.drawText(trackRect.left() + trackWidth_ / 2, trackRect.top(), 
                        trackWidth_ / 2 - 6, trackHeight_, 
                        Qt::AlignRight | Qt::AlignVCenter, "OFF");
    }
    
    // Draw thumb
    QRect thumb = thumbRect();
    QPainterPath thumbPath;
    thumbPath.addEllipse(thumb);
    
    // Thumb shadow
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 60));
    painter.drawEllipse(thumb.translated(0, 2));
    
    // Thumb
    painter.setBrush(thumbColor_);
    painter.setPen(QPen(QColor(0, 0, 0, 30), 1));
    painter.drawPath(thumbPath);
}

QRect ToggleSwitch::thumbRect() const {
    int x = (width() - trackWidth_) / 2 + thumbMargin_;
    int y = (height() - thumbSize_) / 2;
    int travel = trackWidth_ - thumbSize_ - 2 * thumbMargin_;
    int thumbX = x + static_cast<int>(thumbPosition_ * travel);
    return QRect(thumbX, y, thumbSize_, thumbSize_);
}

void ToggleSwitch::setThumbPosition(qreal position) {
    thumbPosition_ = position;
    update();
}

void ToggleSwitch::animateToggle(bool checked) {
    positionAnimation_->stop();
    positionAnimation_->setStartValue(thumbPosition_);
    positionAnimation_->setEndValue(checked ? 1.0 : 0.0);
    positionAnimation_->start();
}

void ToggleSwitch::mousePressEvent(QMouseEvent* event) {
    isPressed_ = true;
    QAbstractButton::mousePressEvent(event);
}

void ToggleSwitch::mouseReleaseEvent(QMouseEvent* event) {
    if (isPressed_) {
        isPressed_ = false;
        setChecked(!isChecked());
    }
    QAbstractButton::mouseReleaseEvent(event);
}

void ToggleSwitch::resizeEvent(QResizeEvent* /*event*/) {
    thumbPosition_ = isChecked() ? 1.0 : 0.0;
}

void ToggleSwitch::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Space || event->key() == Qt::Key_Return) {
        setChecked(!isChecked());
    } else {
        QAbstractButton::keyPressEvent(event);
    }
}

} // namespace chadvis
