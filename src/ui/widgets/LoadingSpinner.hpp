/**
 * @file LoadingSpinner.hpp
 * @brief Animated loading spinner widget
 *
 * Modern circular spinner with gradient animation.
 */

#pragma once

#include <QWidget>
#include <QColor>
#include <QTimer>
#include <QPropertyAnimation>

namespace vc::ui {

class LoadingSpinner : public QWidget {
    Q_OBJECT
    Q_PROPERTY(int rotation READ rotation WRITE setRotation)

public:
    explicit LoadingSpinner(QWidget* parent = nullptr);
    ~LoadingSpinner() override;

    void setColor(const QColor& color);
    void setThickness(int thickness);
    void setSize(int size);

    void start();
    void stop();
    bool isSpinning() const { return spinning_; }

    int rotation() const { return rotation_; }
    void setRotation(int angle);

protected:
    void paintEvent(QPaintEvent* event) override;
    QSize sizeHint() const override;

private:
    void setupAnimation();

    QColor color_{0x00, 0xff, 0x88};  // Chad green
    int thickness_{3};
    int size_{32};
    int rotation_{0};
    bool spinning_{false};

    QTimer* animationTimer_{nullptr};
};

} // namespace vc::ui
