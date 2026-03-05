/**
 * @file VolumeMeter.cpp
 * @brief Volume meter implementation
 */

#include "VolumeMeter.hpp"

#include <QPainter>
#include <QTimer>

namespace vc::ui {

VolumeMeter::VolumeMeter(QWidget* parent)
    : QWidget(parent)
{
    setMinimumWidth(20);
    setMaximumWidth(60);
    decayTimer_ = new QTimer(this);
    connect(decayTimer_, &QTimer::timeout, this, &VolumeMeter::updatePeakDecay);
    decayTimer_->start(50);  // 20fps decay
}

void VolumeMeter::setLevels(float left, float right) {
    leftLevel_ = std::clamp(left, 0.0f, 1.0f);
    rightLevel_ = std::clamp(right, 0.0f, 1.0f);

    // Update peaks
    if (leftLevel_ > leftPeak_) {
        leftPeak_ = leftLevel_;
    }
    if (rightLevel_ > rightPeak_) {
        rightPeak_ = rightLevel_;
    }

    update();
}

void VolumeMeter::setPeakHoldTime(int ms) {
    peakHoldTime_ = ms;
}

void VolumeMeter::setStereo(bool stereo) {
    stereo_ = stereo;
    update();
}

QSize VolumeMeter::sizeHint() const {
    return stereo_ ? QSize(40, 100) : QSize(20, 100);
}

QSize VolumeMeter::minimumSizeHint() const {
    return stereo_ ? QSize(30, 60) : QSize(15, 60);
}

void VolumeMeter::resetPeaks() {
    leftPeak_ = 0.0f;
    rightPeak_ = 0.0f;
    update();
}

void VolumeMeter::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

	auto rect = this->rect();
	int meterWidth = stereo_ ? (width() - 6) / 2 : width() - 4;

	// Background
	painter.fillRect(rect, colorBg_);

    if (stereo_) {
        // Left channel
        drawChannel(painter, 2, meterWidth, leftLevel_, leftPeak_);
        // Right channel
        drawChannel(painter, 4 + meterWidth, meterWidth, rightLevel_, rightPeak_);
    } else {
        drawChannel(painter, 2, meterWidth, leftLevel_, leftPeak_);
    }
}

void VolumeMeter::drawChannel(QPainter& painter, int x, int width, float level, float peak) {
    int height = this->height() - 4;

	// Background gradient segments
	int greenZone = static_cast<int>(height * 0.6);
	int yellowZone = static_cast<int>(height * 0.25);

	// Draw level bar from bottom up
    int levelHeight = static_cast<int>(level * height);

    // Green zone (bottom 60%)
    int greenLevel = std::min(levelHeight, greenZone);
    if (greenLevel > 0) {
        painter.fillRect(x, height - greenLevel + 2, width, greenLevel, colorGreen_);
    }

    // Yellow zone (next 25%)
    if (levelHeight > greenZone) {
        int yellowLevel = std::min(levelHeight - greenZone, yellowZone);
        if (yellowLevel > 0) {
            painter.fillRect(x, height - greenZone - yellowLevel + 2, width, yellowLevel, colorYellow_);
        }
    }

    // Red zone (top 15%)
    if (levelHeight > greenZone + yellowZone) {
        int redLevel = levelHeight - greenZone - yellowZone;
        if (redLevel > 0) {
            painter.fillRect(x, 2, width, redLevel, colorRed_);
        }
    }

    // Peak indicator
    if (peak > 0.0f) {
        int peakY = height - static_cast<int>(peak * height) + 2;
        painter.setPen(QPen(colorPeak_, 2));
        painter.drawLine(x, peakY, x + width, peakY);
    }
}

void VolumeMeter::updatePeakDecay() {
    // Slowly decay peaks
    const float decayRate = 0.02f;
    leftPeak_ = std::max(0.0f, leftPeak_ - decayRate);
    rightPeak_ = std::max(0.0f, rightPeak_ - decayRate);
    update();
}

} // namespace vc::ui
