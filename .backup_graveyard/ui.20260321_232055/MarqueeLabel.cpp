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
    // Use the stylesheet/color from QLabel
    painter.setPen(palette().color(QPalette::WindowText));
    painter.setFont(font());

    QFontMetrics fm(font());
    int textWidth = fm.horizontalAdvance(text());
    int y = (height() + fm.ascent() - fm.descent()) / 2;

    // Draw text with offset
    painter.drawText(offset_, y, text());

    // Draw second copy for continuous loop
    int gap = 80;
    if (offset_ < 0) {
        painter.drawText(offset_ + textWidth + gap, y, text());
    }
}

void MarqueeLabel::resizeEvent(QResizeEvent* event) {
    QLabel::resizeEvent(event);
    // Use the current text to re-evaluate scrolling
    QString currentText = text();
    setText(currentText);
}

void MarqueeLabel::onTimeout() {
    if (!scrolling_)
        return;

    QFontMetrics fm(font());
    int textWidth = fm.horizontalAdvance(text());
    int gap = 80;

    offset_ -= 1;
    if (offset_ <= -(textWidth + gap)) {
        offset_ = 0;
    }
    update();
}

} // namespace vc
