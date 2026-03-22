/**
 * @file ToggleSwitch.hpp
 * @brief Modern toggle switch widget
 *
 * Custom toggle switch with modern aesthetics:
 * - Rounded track with smooth thumb animation
 * - Cyan (#00bcd4) when ON, dark gray when OFF
 * - Smooth sliding animation
 * - Optional ON/OFF labels
 *
 * Part of the Modern 1337 Chad GUI redesign (P3.005)
 *
 * @version 1.0.0
 */

#pragma once

#include <QAbstractButton>
#include <QPropertyAnimation>

namespace chadvis {

/**
 * @brief Modern toggle switch with animation
 *
 * Features smooth sliding animation and modern styling.
 * Can display optional text labels for ON/OFF states.
 */
class ToggleSwitch : public QAbstractButton {
    Q_OBJECT
    Q_PROPERTY(qreal thumbPosition READ thumbPosition WRITE setThumbPosition)

public:
    explicit ToggleSwitch(QWidget* parent = nullptr);
    ~ToggleSwitch() override = default;

    /**
     * Set the ON color (default: cyan #00bcd4)
     */
    void setOnColor(const QColor& color);
    QColor onColor() const { return onColor_; }

    /**
     * Set the OFF color (default: dark gray #5a5a5a)
     */
    void setOffColor(const QColor& color);
    QColor offColor() const { return offColor_; }

    /**
     * Set thumb color (default: white)
     */
    void setThumbColor(const QColor& color);
    QColor thumbColor() const { return thumbColor_; }

    /**
     * Enable/disable ON/OFF text labels
     */
    void setLabelsEnabled(bool enabled) { labelsEnabled_ = enabled; }
    bool isLabelsEnabled() const { return labelsEnabled_; }

    /**
     * Set the size of the switch
     */
    void setSwitchSize(int width, int height);

    QSize sizeHint() const override;

signals:
    /**
     * Emitted when toggle state changes
     */
    void toggled(bool checked);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    qreal thumbPosition() const { return thumbPosition_; }
    void setThumbPosition(qreal position);
    void animateToggle(bool checked);
    QRect thumbRect() const;

    // Configuration
    int trackWidth_ = 48;
    int trackHeight_ = 24;
    int thumbSize_ = 20;
    int thumbMargin_ = 2;
    QColor onColor_ = QColor("#00bcd4");
    QColor offColor_ = QColor("#5a5a5a");
    QColor thumbColor_ = QColor("#ffffff");
    bool labelsEnabled_ = false;

    // State
    qreal thumbPosition_ = 0.0;  // 0.0 = OFF, 1.0 = ON
    bool isPressed_ = false;

    // Animation
    QPropertyAnimation* positionAnimation_;
};

} // namespace chadvis
