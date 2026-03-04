/**
 * @file VolumeMeter.hpp
 * @brief Real-time volume level meter widget
 *
 * Displays stereo audio levels with peak indicators and clipping warning.
 */

#pragma once

#include <QWidget>
#include <QColor>
#include <QTimer>
#include <array>

namespace vc::ui {

class VolumeMeter : public QWidget {
    Q_OBJECT

public:
    explicit VolumeMeter(QWidget* parent = nullptr);

    void setLevels(float left, float right);
    void setPeakHoldTime(int ms);
    void setStereo(bool stereo);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

public slots:
    void resetPeaks();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void drawChannel(QPainter& painter, int x, int width, float level, float peak);
    void updatePeakDecay();

    float leftLevel_{0.0f};
    float rightLevel_{0.0f};
    float leftPeak_{0.0f};
    float rightPeak_{0.0f};

    int peakHoldTime_{2000};  // ms
    bool stereo_{true};

    QColor colorGreen_{0x00, 0xff, 0x88};
    QColor colorYellow_{0xff, 0xaa, 0x00};
    QColor colorRed_{0xff, 0x44, 0x44};
    QColor colorBg_{0x1a, 0x1a, 0x1a};
    QColor colorPeak_{0xff, 0xff, 0xff};

    QTimer* decayTimer_{nullptr};
};

} // namespace vc::ui
