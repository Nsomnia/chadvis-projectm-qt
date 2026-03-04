/**
 * @file WaveformSeekBar.hpp
 * @brief Waveform visualization with seek functionality
 *
 * Displays audio waveform with playable regions and current position.
 */

#pragma once

#include <QWidget>
#include <QColor>
#include <vector>

namespace vc::ui {

class WaveformSeekBar : public QWidget {
    Q_OBJECT

public:
    explicit WaveformSeekBar(QWidget* parent = nullptr);

    void setWaveformData(const std::vector<float>& data);
    void setDuration(double seconds);
    void setPosition(double seconds);
    void setLoadedRegion(double start, double end);
    void clearWaveform();

    double position() const { return position_; }
    double duration() const { return duration_; }

signals:
    void seekRequested(double seconds);
    void seekBegin();
    void seekEnd();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    void drawWaveform(QPainter& painter, const QRect& rect);
    void drawPositionIndicator(QPainter& painter, const QRect& rect);
    void drawLoadedRegion(QPainter& painter, const QRect& rect);
    double pixelToTime(int x, int width) const;
    int timeToPixel(double time, int width) const;

    std::vector<float> waveformData_;
    double duration_{0.0};
    double position_{0.0};
    double loadedStart_{0.0};
    double loadedEnd_{0.0};

    bool isDragging_{false};
    bool isHovering_{false};
    int hoverPosition_{-1};

    // Colors
    QColor bgColor_{0x1e, 0x1e, 0x1e};
    QColor waveformColor_{0x00, 0xff, 0x88, 180};
    QColor waveformPlayedColor_{0x00, 0xff, 0x88};
    QColor positionColor_{0xff, 0xff, 0xff};
    QColor loadedColor_{0x00, 0xff, 0x88, 50};
};

} // namespace vc::ui
