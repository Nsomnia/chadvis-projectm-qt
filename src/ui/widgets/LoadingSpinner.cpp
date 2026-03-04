/**
 * @file LoadingSpinner.cpp
 * @brief Animated loading spinner widget implementation
 */

#include "LoadingSpinner.hpp"

#include <QPainter>
#include <QPainterPath>
#include <QConicalGradient>
#include <QTimer>

namespace vc::ui {

LoadingSpinner::LoadingSpinner(QWidget* parent)
    : QWidget(parent)
{
    setupAnimation();
}

LoadingSpinner::~LoadingSpinner() {
    if (animationTimer_) {
        animationTimer_->stop();
    }
}

void LoadingSpinner::setColor(const QColor& color) {
    color_ = color;
    update();
}

void LoadingSpinner::setThickness(int thickness) {
    thickness_ = thickness;
    update();
}

void LoadingSpinner::setSize(int size) {
    size_ = size;
    setFixedSize(size, size);
    update();
}

void LoadingSpinner::start() {
    spinning_ = true;
    if (animationTimer_) {
        animationTimer_->start(16);  // ~60fps
    }
    show();
}

void LoadingSpinner::stop() {
    spinning_ = false;
    if (animationTimer_) {
        animationTimer_->stop();
    }
    hide();
}

void LoadingSpinner::setRotation(int angle) {
    rotation_ = angle % 360;
    update();
}

void LoadingSpinner::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Center
    qreal cx = width() / 2.0;
    qreal cy = height() / 2.0;
    qreal radius = (std::min(width(), height()) - thickness_) / 2.0 - 2;

    // Conical gradient for spinner effect
    QConicalGradient gradient(cx, cy, rotation_);
    gradient.setColorAt(0.0, color_);
    gradient.setColorAt(0.5, color_.lighter(150));
    gradient.setColorAt(1.0, QColor(color_.red(), color_.green(), color_.blue(), 0));

    QPen pen(QBrush(gradient), thickness_, Qt::SolidLine, Qt::RoundCap);
    painter.setPen(pen);

    // Draw arc (3/4 of circle)
    QRectF rect(cx - radius, cy - radius, radius * 2, radius * 2);
    painter.drawArc(rect, 0, 270 * 16);  // 270 degrees in 1/16th degree units
}

QSize LoadingSpinner::sizeHint() const {
    return QSize(size_, size_);
}

void LoadingSpinner::setupAnimation() {
    animationTimer_ = new QTimer(this);
    connect(animationTimer_, &QTimer::timeout, this, [this] {
        setRotation(rotation() + 6);  // 6 degrees per frame = smooth rotation
    });
    setFixedSize(size_, size_);
}

} // namespace vc::ui
