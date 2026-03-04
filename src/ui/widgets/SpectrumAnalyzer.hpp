/**
 * @file SpectrumAnalyzer.hpp
 * @brief Real-time FFT spectrum display widget
 *
 * Visualizes audio frequency spectrum with configurable bands and style.
 */

#pragma once

#include <QWidget>
#include <QColor>
#include <QTimer>
#include <vector>

namespace vc::ui {

enum class SpectrumStyle {
    Bars,       // Classic bar graph
    Line,       // Line graph
    Mirror,     // Mirrored bars (center symmetry)
    Gradient    // Gradient fill with smooth curves
};

class SpectrumAnalyzer : public QWidget {
    Q_OBJECT

public:
    explicit SpectrumAnalyzer(QWidget* parent = nullptr);

    void setFFTData(const float* magnitude, int size);
    void setBandCount(int count);
    void setStyle(SpectrumStyle style);
    void setFramerate(int fps);
    void setColors(const QColor& low, const QColor& mid, const QColor& high);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

public slots:
    void clear();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void drawBars(QPainter& painter, const QRect& rect);
    void drawLine(QPainter& painter, const QRect& rect);
    void drawMirror(QPainter& painter, const QRect& rect);
    void drawGradient(QPainter& painter, const QRect& rect);
    QColor getBarColor(float position) const;
    void interpolateBands();

    std::vector<float> fftData_;
    std::vector<float> bandData_;
    std::vector<float> smoothedBands_;

    int bandCount_{64};
    SpectrumStyle style_{SpectrumStyle::Bars};
    int framerate_{60};

    QColor colorLow_{0x00, 0xff, 0x88};    // Green (bass)
    QColor colorMid_{0xff, 0xaa, 0x00};    // Yellow (mids)
    QColor colorHigh_{0xff, 0x44, 0x44};   // Red (highs)
    QColor colorBg_{0x1a, 0x1a, 0x1a};

    float smoothing_{0.7f};  // Smoothing factor (0-1)
    float decay_{0.95f};     // Decay factor per frame
};

} // namespace vc::ui
