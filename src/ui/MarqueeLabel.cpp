#include "MarqueeLabel.hpp"
#include <QPainter>
#include <QTimer>

namespace vc {

MarqueeLabel::MarqueeLabel(QWidget* parent) : QLabel(parent) {
    timer_.setInterval(30);
    connect(&timer_, &QTimer::timeout, this, &MarqueeLabel::onTimeout);
}

void MarqueeLabel::setText(const QString& text) {
    QLabel::setText(text);
    offset_ = 0;

    // Check if text needs scrolling
    QFontMetrics fm(font());
    if (fm.horizontalAdvance(text) > width()) {
        if (!scrolling_) {
            scrolling_ = true;
            timer_.start();
        }
    } else {
        scrolling_ = false;
        timer_.stop();
        offset_ = 0;
        update();
    }
}

void MarqueeLabel::paintEvent(QPaintEvent* event) {
    if (!scrolling_) {
        QLabel::paintEvent(event);
        return;
    }

    QPainter painter(this);
    QFontMetrics fm(font());
    int textWidth = fm.horizontalAdvance(text());
    int y = (height() + fm.ascent() - fm.descent()) / 2;

    // Draw text with offset
    painter.drawText(offset_, y, text());

    // Draw second copy for continuous loop if needed
    if (offset_ < 0) {
        painter.drawText(offset_ + textWidth + 40, y, text());
    }
}

void MarqueeLabel::resizeEvent(QResizeEvent* event) {
    QLabel::resizeEvent(event);
    setText(text()); // Re-evaluate scrolling
}

void MarqueeLabel::onTimeout() {
    if (!scrolling_)
        return;

    QFontMetrics fm(font());
    int textWidth = fm.horizontalAdvance(text());

    offset_ -= 1;
    if (offset_ < -(textWidth + 40)) {
        offset_ = 0;
    }
    update();
}

} // namespace vc
