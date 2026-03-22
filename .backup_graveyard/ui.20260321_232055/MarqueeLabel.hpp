#pragma once
#include <QLabel>
#include <QTimer>

namespace vc {

class MarqueeLabel : public QLabel {
    Q_OBJECT
public:
    explicit MarqueeLabel(QWidget* parent = nullptr);

    void setText(const QString& text);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void onTimeout();

private:
    int offset_{0};
    QTimer timer_;
    bool scrolling_{false};
};

} // namespace vc
