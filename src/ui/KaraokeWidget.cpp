#include "KaraokeWidget.hpp"
#include "ui/controllers/SunoController.hpp"
#include "audio/AudioEngine.hpp"
#include "core/Config.hpp"
#include "core/Logger.hpp"

#include <QPainter>
#include <QPainterPath>
#include <algorithm>

namespace vc {

KaraokeWidget::KaraokeWidget(suno::SunoController* suno, AudioEngine* audio, QWidget* parent)
    : QWidget(parent), sunoController_(suno), audioEngine_(audio) {
    
    QPalette pal = palette();
    pal.setColor(QPalette::Window, Qt::black);
    setPalette(pal);
    setAutoFillBackground(true);

    updateStyle();

    if (audioEngine_) {
        audioEngine_->positionChanged.connect([this](Duration pos) {
            updateTime(static_cast<f32>(pos.count()) / 1000.0f);
        });
        
        audioEngine_->trackChanged.connect([this]() {
            if (auto item = audioEngine_->playlist().currentItem()) {
                if (!item->metadata.sunoClipId.empty()) {
                    auto res = sunoController_->getLyrics(item->metadata.sunoClipId);
                    if (res.isOk()) {
                        setLyrics(res.value());
                    } else {
                        clear();
                    }
                } else {
                    clear();
                }
            }
        });
    }

    if (sunoController_) {
        sunoController_->clipUpdated.connect([this](const std::string& id) {
            if (auto item = audioEngine_->playlist().currentItem()) {
                if (item->metadata.sunoClipId == id) {
                    auto res = sunoController_->getLyrics(id);
                    if (res.isOk()) {
                        setLyrics(res.value());
                    }
                }
            }
        });
    }
}

KaraokeWidget::~KaraokeWidget() = default;

void KaraokeWidget::setLyrics(const suno::AlignedLyrics& lyrics) {
    currentLyrics_ = lyrics;
    update();
}

void KaraokeWidget::clear() {
    currentLyrics_ = suno::AlignedLyrics();
    update();
}

void KaraokeWidget::updateTime(f32 time) {
    currentTime_ = time;
    update();
}

void KaraokeWidget::updateStyle() {
    const auto& cfg = CONFIG.karaoke();
    style_.font = QFont(QString::fromStdString(cfg.fontFamily));
    style_.font.setPixelSize(cfg.fontSize);
    style_.font.setBold(cfg.bold);
    
    style_.activeColor = QColor(cfg.activeColor.r, cfg.activeColor.g, cfg.activeColor.b, cfg.activeColor.a);
    style_.inactiveColor = QColor(cfg.inactiveColor.r, cfg.inactiveColor.g, cfg.inactiveColor.b, cfg.inactiveColor.a);
    style_.shadowColor = QColor(cfg.shadowColor.r, cfg.shadowColor.g, cfg.shadowColor.b, cfg.shadowColor.a);
    style_.yPosition = cfg.yPosition;
}

void KaraokeWidget::paintEvent(QPaintEvent* event) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    
    if (currentLyrics_.empty()) {
        painter.setPen(Qt::gray);
        painter.drawText(rect(), Qt::AlignCenter, "No synchronized lyrics");
        return;
    }

    drawLyrics(painter);
}

void KaraokeWidget::drawLyrics(QPainter& painter) {
    painter.setFont(style_.font);
    
    int activeLineIdx = -1;
    for (int i = 0; i < (int)currentLyrics_.lines.size(); ++i) {
        if (currentTime_ >= currentLyrics_.lines[i].start_s &&
            currentTime_ <= currentLyrics_.lines[i].end_s) {
            activeLineIdx = i;
            break;
        }
    }
    
    int lineHeight = painter.fontMetrics().height() * 1.5;
    int centerY = height() / 2;
    
    if (activeLineIdx == -1) {
        for (int i = 0; i < (int)currentLyrics_.lines.size(); ++i) {
            if (currentLyrics_.lines[i].start_s > currentTime_) {
                activeLineIdx = i;
                break;
            }
        }
        if (activeLineIdx == -1 && currentTime_ > 0) {
            activeLineIdx = currentLyrics_.lines.size(); 
        } else if (activeLineIdx == -1) {
            activeLineIdx = 0;
        }
    }
    
    for (int i = 0; i < (int)currentLyrics_.lines.size(); ++i) {
        const auto& line = currentLyrics_.lines[i];
        
        int yPos = centerY + (i - activeLineIdx) * lineHeight;
        
        if (yPos < -lineHeight || yPos > height() + lineHeight) continue;
        
        bool isActiveLine = (i == activeLineIdx);
        
        QString lineStr = QString::fromStdString(line.text);
        int textWidth = painter.fontMetrics().horizontalAdvance(lineStr);
        int xPos = (width() - textWidth) / 2;
        
        if (isActiveLine) {
            int currentX = xPos;
            for (const auto& w : line.words) {
                QString wordStr = QString::fromStdString(w.word + " ");
                bool isPast = w.end_s < currentTime_;
                bool isCurrent = currentTime_ >= w.start_s && currentTime_ <= w.end_s;
                
                QColor color;
                if (isPast) color = style_.activeColor;
                else if (isCurrent) color = style_.activeColor;
                else color = style_.inactiveColor;
                
                painter.setPen(style_.shadowColor);
                painter.drawText(currentX + 2, yPos + 2, wordStr);
                
                painter.setPen(color);
                painter.drawText(currentX, yPos, wordStr);
                
                currentX += painter.fontMetrics().horizontalAdvance(wordStr);
            }
        } else {
            QColor color = style_.inactiveColor;
            color.setAlpha(128);
            
            painter.setPen(color);
            painter.drawText(xPos, yPos, lineStr);
        }
    }
}

} // namespace vc
