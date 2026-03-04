/**
 * @file SpectrumAnalyzer.cpp
 * @brief Spectrum analyzer implementation
 */

#include "SpectrumAnalyzer.hpp"

#include <QPainter>
#include <QLinearGradient>
#include <QPainterPath>
#include <QtMath>

namespace vc::ui {

SpectrumAnalyzer::SpectrumAnalyzer(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(60);
    bandData_.resize(bandCount_);
    smoothedBands_.resize(bandCount_, 0.0f);
}

void SpectrumAnalyzer::setFFTData(const float* magnitude, int size) {
    fftData_.assign(magnitude, magnitude + size);
    interpolateBands();
    update();
}

void SpectrumAnalyzer::setBandCount(int count) {
    bandCount_ = std::clamp(count, 8, 256);
    bandData_.resize(bandCount_);
    smoothedBands_.resize(bandCount_, 0.0f);
}

void SpectrumAnalyzer::setStyle(SpectrumStyle style) {
    style_ = style;
    update();
}

void SpectrumAnalyzer::setFramerate(int fps) {
    framerate_ = std::clamp(fps, 15, 120);
}

void SpectrumAnalyzer::setColors(const QColor& low, const QColor& mid, const QColor& high) {
    colorLow_ = low;
    colorMid_ = mid;
    colorHigh_ = high;
}

QSize SpectrumAnalyzer::sizeHint() const {
    return QSize(300, 100);
}

QSize SpectrumAnalyzer::minimumSizeHint() const {
    return QSize(100, 40);
}

void SpectrumAnalyzer::clear() {
    fftData_.clear();
    std::fill(bandData_.begin(), bandData_.end(), 0.0f);
    std::fill(smoothedBands_.begin(), smoothedBands_.end(), 0.0f);
    update();
}

void SpectrumAnalyzer::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    auto rect = this->rect();
    painter.fillRect(rect, colorBg_);

    switch (style_) {
        case SpectrumStyle::Bars:
            drawBars(painter, rect);
            break;
        case SpectrumStyle::Line:
            drawLine(painter, rect);
            break;
        case SpectrumStyle::Mirror:
            drawMirror(painter, rect);
            break;
        case SpectrumStyle::Gradient:
            drawGradient(painter, rect);
            break;
    }
}

void SpectrumAnalyzer::drawBars(QPainter& painter, const QRect& rect) {
    int gap = 2;
    int barWidth = (rect.width() - gap * (bandCount_ - 1)) / bandCount_;
    int maxHeight = rect.height() - 4;

    for (int i = 0; i < bandCount_; ++i) {
        float value = smoothedBands_[i];
        int barHeight = static_cast<int>(value * maxHeight);
        int x = rect.x() + i * (barWidth + gap);
        int y = rect.y() + maxHeight - barHeight + 2;

        QColor color = getBarColor(static_cast<float>(i) / bandCount_);
        painter.fillRect(x, y, barWidth, barHeight, color);
    }
}

void SpectrumAnalyzer::drawLine(QPainter& painter, const QRect& rect) {
    QPainterPath path;
    int maxHeight = rect.height() - 4;
    float step = static_cast<float>(rect.width()) / (bandCount_ - 1);

    path.moveTo(rect.x(), rect.y() + maxHeight - smoothedBands_[0] * maxHeight);

    for (int i = 1; i < bandCount_; ++i) {
        float x = rect.x() + i * step;
        float y = rect.y() + maxHeight - smoothedBands_[i] * maxHeight;
        path.lineTo(x, y);
    }

    // Gradient stroke
    QLinearGradient gradient(rect.topLeft(), rect.topRight());
    gradient.setColorAt(0.0, colorLow_);
    gradient.setColorAt(0.5, colorMid_);
    gradient.setColorAt(1.0, colorHigh_);

    QPen pen(QBrush(gradient), 2);
    painter.setPen(pen);
    painter.drawPath(path);
}

void SpectrumAnalyzer::drawMirror(QPainter& painter, const QRect& rect) {
    int gap = 1;
    int barWidth = (rect.width() / 2 - gap * (bandCount_ / 2 - 1)) / (bandCount_ / 2);
    int maxHeight = rect.height() / 2 - 2;
    int centerY = rect.y() + rect.height() / 2;

    // Left side (low frequencies)
    for (int i = 0; i < bandCount_ / 2; ++i) {
        float value = smoothedBands_[i];
        int barHeight = static_cast<int>(value * maxHeight);
        int x = rect.x() + rect.width() / 2 - (i + 1) * (barWidth + gap);
        QColor color = getBarColor(static_cast<float>(i) / bandCount_);

        // Top
        painter.fillRect(x, centerY - barHeight, barWidth, barHeight, color);
        // Bottom (mirror)
        painter.fillRect(x, centerY, barWidth, barHeight, color.darker(150));
    }

    // Right side (high frequencies)
    for (int i = bandCount_ / 2; i < bandCount_; ++i) {
        float value = smoothedBands_[i];
        int barHeight = static_cast<int>(value * maxHeight);
        int x = rect.x() + rect.width() / 2 + (i - bandCount_ / 2) * (barWidth + gap);
        QColor color = getBarColor(static_cast<float>(i) / bandCount_);

        // Top
        painter.fillRect(x, centerY - barHeight, barWidth, barHeight, color);
        // Bottom (mirror)
        painter.fillRect(x, centerY, barWidth, barHeight, color.darker(150));
    }
}

void SpectrumAnalyzer::drawGradient(QPainter& painter, const QRect& rect) {
    QPainterPath path;
    int maxHeight = rect.height() - 4;

    // Create smooth curve using catmull-rom interpolation
    path.moveTo(rect.x(), rect.y() + maxHeight);
    path.lineTo(rect.x(), rect.y() + maxHeight - smoothedBands_[0] * maxHeight);

    for (int i = 1; i < bandCount_; ++i) {
        float x = rect.x() + i * static_cast<float>(rect.width()) / (bandCount_ - 1);
        float y = rect.y() + maxHeight - smoothedBands_[i] * maxHeight;
        path.lineTo(x, y);
    }

    path.lineTo(rect.right(), rect.y() + maxHeight);
    path.closeSubpath();

    // Gradient fill
    QLinearGradient gradient(rect.topLeft(), rect.bottomLeft());
    gradient.setColorAt(0.0, QColor(colorHigh_.red(), colorHigh_.green(), colorHigh_.blue(), 200));
    gradient.setColorAt(0.5, QColor(colorMid_.red(), colorMid_.green(), colorMid_.blue(), 150));
    gradient.setColorAt(1.0, QColor(colorLow_.red(), colorLow_.green(), colorLow_.blue(), 100));

    painter.fillPath(path, gradient);
}

QColor SpectrumAnalyzer::getBarColor(float position) const {
    // Position 0 = low freq (green), 1 = high freq (red)
    if (position < 0.33f) {
        float t = position / 0.33f;
        return QColor(
            colorLow_.red() + t * (colorMid_.red() - colorLow_.red()),
            colorLow_.green() + t * (colorMid_.green() - colorLow_.green()),
            colorLow_.blue() + t * (colorMid_.blue() - colorLow_.blue())
        );
    } else if (position < 0.66f) {
        float t = (position - 0.33f) / 0.33f;
        return QColor(
            colorMid_.red() + t * (colorHigh_.red() - colorMid_.red()),
            colorMid_.green() + t * (colorHigh_.green() - colorMid_.green()),
            colorMid_.blue() + t * (colorHigh_.blue() - colorMid_.blue())
        );
    }
    return colorHigh_;
}

void SpectrumAnalyzer::interpolateBands() {
    if (fftData_.empty()) return;

    int fftSize = fftData_.size();
    float ratio = static_cast<float>(fftSize) / bandCount_;

    for (int i = 0; i < bandCount_; ++i) {
        int start = static_cast<int>(i * ratio);
        int end = static_cast<int>((i + 1) * ratio);
        end = std::min(end, fftSize - 1);

        float sum = 0.0f;
        int count = 0;
        for (int j = start; j <= end; ++j) {
            sum += fftData_[j];
            ++count;
        }
        bandData_[i] = count > 0 ? sum / count : 0.0f;

        // Apply smoothing
        smoothedBands_[i] = smoothedBands_[i] * smoothing_ + bandData_[i] * (1.0f - smoothing_);

        // Apply decay (bands fall slowly)
        if (bandData_[i] < smoothedBands_[i]) {
            smoothedBands_[i] *= decay_;
        }
    }
}

} // namespace vc::ui
