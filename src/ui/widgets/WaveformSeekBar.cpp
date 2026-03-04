/**
 * @file WaveformSeekBar.cpp
 * @brief Waveform visualization implementation
 */

#include "WaveformSeekBar.hpp"

#include <QPainter>
#include <QMouseEvent>
#include <QStyleOption>

namespace vc::ui {

WaveformSeekBar::WaveformSeekBar(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(48);
    setMaximumHeight(80);
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
}

void WaveformSeekBar::setWaveformData(const std::vector<float>& data) {
    waveformData_ = data;
    update();
}

void WaveformSeekBar::setDuration(double seconds) {
    duration_ = seconds;
    update();
}

void WaveformSeekBar::setPosition(double seconds) {
    position_ = std::clamp(seconds, 0.0, duration_);
    update();
}

void WaveformSeekBar::setLoadedRegion(double start, double end) {
    loadedStart_ = start;
    loadedEnd_ = end;
    update();
}

void WaveformSeekBar::clearWaveform() {
    waveformData_.clear();
    duration_ = 0.0;
    position_ = 0.0;
    update();
}

void WaveformSeekBar::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    auto rect = this->rect();
    
    // Background
    painter.fillRect(rect, bgColor_);
    
    // Loaded region (buffered area)
    drawLoadedRegion(painter, rect);
    
    // Waveform
    drawWaveform(painter, rect);
    
    // Position indicator
    drawPositionIndicator(painter, rect);
    
    // Hover preview
    if (isHovering_ && hoverPosition_ >= 0 && duration_ > 0) {
        double hoverTime = pixelToTime(hoverPosition_, rect.width());
        painter.setPen(QColor(255, 255, 255, 100));
        painter.drawLine(hoverPosition_, 0, hoverPosition_, rect.height());
        
        // Time tooltip
        int secs = static_cast<int>(hoverTime);
        QString timeText = QString("%1:%2")
            .arg(secs / 60, 2, 10, QChar('0'))
            .arg(secs % 60, 2, 10, QChar('0'));
        
        painter.setPen(Qt::white);
        QFont font = painter.font();
        font.setPointSize(9);
        painter.setFont(font);
        painter.drawText(hoverPosition_ + 4, 14, timeText);
    }
}

void WaveformSeekBar::drawWaveform(QPainter& painter, const QRect& rect) {
    if (waveformData_.empty()) {
        // Draw placeholder bars
        painter.setPen(waveformColor_);
        int barWidth = 3;
        int gap = 2;
        int numBars = rect.width() / (barWidth + gap);
        int centerY = rect.height() / 2;
        
        for (int i = 0; i < numBars; ++i) {
            int x = i * (barWidth + gap);
            float height = (std::sin(i * 0.3f) * 0.5f + 0.5f) * rect.height() * 0.6f;
            painter.fillRect(x, centerY - height/2, barWidth, height, waveformColor_);
        }
        return;
    }
    
    int samplesPerPixel = static_cast<int>(waveformData_.size() / rect.width());
    int centerY = rect.height() / 2;
    int positionPixel = timeToPixel(position_, rect.width());
    
    for (int x = 0; x < rect.width(); ++x) {
        int sampleIndex = x * samplesPerPixel;
        if (sampleIndex >= static_cast<int>(waveformData_.size())) break;
        
        // Get max amplitude for this pixel
        float maxAmp = 0.0f;
        for (int i = 0; i < samplesPerPixel && sampleIndex + i < static_cast<int>(waveformData_.size()); ++i) {
            maxAmp = std::max(maxAmp, std::abs(waveformData_[sampleIndex + i]));
        }
        
        int height = static_cast<int>(maxAmp * rect.height() * 0.9f);
        
        // Color based on played state
        QColor color = x <= positionPixel ? waveformPlayedColor_ : waveformColor_;
        painter.fillRect(x, centerY - height/2, 1, height, color);
    }
}

void WaveformSeekBar::drawPositionIndicator(QPainter& painter, const QRect& rect) {
    if (duration_ <= 0) return;
    
    int x = timeToPixel(position_, rect.width());
    
    // Line
    painter.setPen(positionColor_);
    painter.drawLine(x, 0, x, rect.height());
    
    // Triangle marker at top
    QPolygonF triangle;
    triangle << QPointF(x - 6, 0) 
             << QPointF(x + 6, 0) 
             << QPointF(x, 8);
    painter.setBrush(positionColor_);
    painter.drawPolygon(triangle);
}

void WaveformSeekBar::drawLoadedRegion(QPainter& painter, const QRect& rect) {
    if (loadedEnd_ <= loadedStart_) return;
    
    int startX = timeToPixel(loadedStart_, rect.width());
    int endX = timeToPixel(loadedEnd_, rect.width());
    
    painter.fillRect(startX, 0, endX - startX, rect.height(), loadedColor_);
}

double WaveformSeekBar::pixelToTime(int x, int width) const {
    return (static_cast<double>(x) / width) * duration_;
}

int WaveformSeekBar::timeToPixel(double time, int width) const {
    return static_cast<int>((time / duration_) * width);
}

void WaveformSeekBar::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && duration_ > 0) {
        isDragging_ = true;
        emit seekBegin();
        double time = pixelToTime(event->position().x(), width());
        setPosition(time);
        emit seekRequested(time);
    }
}

void WaveformSeekBar::mouseMoveEvent(QMouseEvent* event) {
    hoverPosition_ = static_cast<int>(event->position().x());
    
    if (isDragging_ && duration_ > 0) {
        double time = pixelToTime(event->position().x(), width());
        setPosition(time);
        emit seekRequested(time);
    }
    
    update();
}

void WaveformSeekBar::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && isDragging_) {
        isDragging_ = false;
        emit seekEnd();
    }
}

void WaveformSeekBar::enterEvent(QEnterEvent*) {
    isHovering_ = true;
    update();
}

void WaveformSeekBar::leaveEvent(QEvent*) {
    isHovering_ = false;
    hoverPosition_ = -1;
    update();
}

} // namespace vc::ui
