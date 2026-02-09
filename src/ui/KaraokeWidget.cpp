/**
 * @file KaraokeWidget.cpp
 * @brief Karaoke widget implementation using LyricsRenderer.
 */

#include "KaraokeWidget.hpp"
#include "ui/controllers/SunoController.hpp"
#include "audio/AudioEngine.hpp"
#include "core/Config.hpp"
#include "core/Logger.hpp"
#include <QPainter>
#include <QTimer>
#include <QPalette>

namespace vc {

KaraokeWidget::KaraokeWidget(suno::SunoController* suno, 
                             AudioEngine* audio,
                             QWidget* parent)
    : QWidget(parent), 
      sunoController_(suno), 
      audioEngine_(audio),
      renderer_(std::make_unique<KaraokeRenderer>()),
      updateTimer_(new QTimer(this)) {
    
    // Set background
    QPalette pal = palette();
    pal.setColor(QPalette::Window, Qt::black);
    setPalette(pal);
    setAutoFillBackground(true);
    
    // Setup sync engine
    sync_ = std::make_unique<LyricsSync>(audioEngine_, this);
    sync_->positionChanged.connect([this](const LyricsSyncPosition& pos) {
        onPositionChanged(pos);
    });
    
    // Setup update timer for smooth rendering
    updateTimer_->setInterval(16); // ~60fps
    connect(updateTimer_, &QTimer::timeout, this, [this]() {
        if (sync_->getState() == LyricsSyncState::Syncing) {
            update();
        }
    });
    
    setupConnections();
    
    // Apply config
    const auto& cfg = CONFIG.karaoke();
    renderer_->setVerticalPosition(cfg.yPosition);
    
    auto style = renderer_->getStyle();
    style.font = QFont(QString::fromStdString(cfg.fontFamily), cfg.fontSize);
    style.font.setBold(cfg.bold);
    style.activeColor = QColor(cfg.activeColor.r, cfg.activeColor.g, cfg.activeColor.b);
    style.inactiveColor = QColor(cfg.inactiveColor.r, cfg.inactiveColor.g, cfg.inactiveColor.b);
    style.shadowColor = QColor(cfg.shadowColor.r, cfg.shadowColor.g, cfg.shadowColor.b);
    renderer_->setStyle(style);
}

KaraokeWidget::~KaraokeWidget() = default;

void KaraokeWidget::setupConnections() {
    if (!audioEngine_) return;
    
    // Track changes
    audioEngine_->trackChanged.connect([this]() {
        onAudioTrackChanged();
    });
    
    // Position updates (for smoother than timer)
    audioEngine_->positionChanged.connect([this](Duration pos) {
        updateTime(static_cast<f32>(pos.count()) / 1000.0f);
    });
    
    // Suno clip updates
    if (sunoController_) {
        sunoController_->clipUpdated.connect([this](const std::string& id) {
            if (auto item = audioEngine_->playlist().currentItem()) {
                if (item->metadata.sunoClipId == id) {
                    // Reload lyrics
                    auto res = sunoController_->getLyrics(id);
                    if (res.isOk()) {
                        // Convert AlignedLyrics to LyricsData
                        // For now, clear and let sync handle it
                        clear();
                    }
                }
            }
        });
    }
}

void KaraokeWidget::setLyrics(const LyricsData& lyrics) {
    lyrics_ = lyrics;
    sync_->loadLyrics(lyrics);
    renderer_->setLyrics(lyrics);
    
    if (!lyrics.empty()) {
        sync_->start();
        updateTimer_->start();
        LOG_INFO("KaraokeWidget: Loaded {} lines", lyrics.lineCount());
    }
    
    update();
}

void KaraokeWidget::clear() {
    lyrics_ = LyricsData();
    sync_->clear();
    renderer_->setLyrics(LyricsData());
    updateTimer_->stop();
    update();
}

void KaraokeWidget::updateTime(f32 time) {
    // Time updates handled by LyricsSync
    // This is called for external time updates
}

void KaraokeWidget::setVerticalPosition(f32 position) {
    renderer_->setVerticalPosition(position);
    update();
}

void KaraokeWidget::paintEvent(QPaintEvent* event) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    
    renderer_->render(painter, rect());
}

void KaraokeWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    update();
}

void KaraokeWidget::onPositionChanged(const LyricsSyncPosition& pos) {
    renderer_->updatePosition(pos);
    // Update is triggered by timer for smooth 60fps
}

void KaraokeWidget::onAudioTrackChanged() {
    clear();
    
    // Try to load lyrics for new track
    if (!sunoController_ || !audioEngine_) return;
    
    auto item = audioEngine_->playlist().currentItem();
    if (!item) return;
    
    if (!item->metadata.sunoClipId.empty()) {
        auto res = sunoController_->getLyrics(item->metadata.sunoClipId);
        if (res.isOk()) {
            // Note: Need to convert AlignedLyrics to LyricsData
            // This requires a conversion function
            LOG_INFO("KaraokeWidget: Loaded lyrics for {}", item->metadata.sunoClipId);
        }
    }
}

} // namespace vc
