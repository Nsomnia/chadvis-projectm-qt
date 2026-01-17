#pragma once

#include "suno/SunoLyrics.hpp"
#include "util/Types.hpp"
#include <QWidget>
#include <memory>

namespace vc {

namespace suno {
class SunoController;
}

class AudioEngine;

class KaraokeWidget : public QWidget {
    Q_OBJECT

public:
    explicit KaraokeWidget(suno::SunoController* suno, AudioEngine* audio, QWidget* parent = nullptr);
    ~KaraokeWidget() override;

    void setLyrics(const suno::AlignedLyrics& lyrics);
    void updateTime(f32 time);
    void clear();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void drawLyrics(QPainter& painter);

    suno::SunoController* sunoController_;
    AudioEngine* audioEngine_;
    suno::AlignedLyrics currentLyrics_;
    f32 currentTime_{0.0f};
    
    struct Style {
        QFont font;
        QColor activeColor;
        QColor inactiveColor;
        QColor shadowColor;
        f32 yPosition;
    } style_;

    void updateStyle();
};

} // namespace vc
