/**
 * @file LyricsPanel.cpp
 * @brief Lyrics panel implementation.
 */

#include "LyricsPanel.hpp"
#include "audio/AudioEngine.hpp"
#include "ui/controllers/SunoController.hpp"
#include "core/Logger.hpp"
#include <QPainter>
#include <QVBoxLayout>
#include <QScrollBar>
#include <QMouseEvent>

namespace vc {

LyricsPanel::LyricsPanel(AudioEngine* audio, 
                         suno::SunoController* suno,
                         QWidget* parent)
    : QWidget(parent), audio_(audio), suno_(suno) {
    
    setupUI();
    
    // Create sync engine
    sync_ = std::make_unique<LyricsSync>(audio_, this);
    connect(sync_.get(), &LyricsSync::positionChanged, 
            this, &LyricsPanel::onPositionChanged);
    
    // Connect to audio track changes
    if (audio_) {
        audio_->trackChanged.connect([this]() {
            onAudioTrackChanged();
        });
    }
}

LyricsPanel::~LyricsPanel() = default;

void LyricsPanel::setupUI() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    
    // Search box
    searchEdit_ = new QLineEdit(this);
    searchEdit_->setPlaceholderText("Search lyrics...");
    searchEdit_->setStyleSheet(
        "QLineEdit { background-color: #404040; color: white; "
        "border: 1px solid #606060; padding: 5px; }");
    connect(searchEdit_, &QLineEdit::textChanged,
            this, &LyricsPanel::onSearchTextChanged);
    layout->addWidget(searchEdit_);
    
    // Scroll area for lyrics
    scrollArea_ = new QScrollArea(this);
    scrollArea_->setWidgetResizable(true);
    scrollArea_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea_->setStyleSheet("QScrollArea { border: none; background-color: #1e1e1e; }");
    
    // Content widget
    contentWidget_ = new QWidget();
    contentWidget_->setStyleSheet("background-color: #1e1e1e;");
    scrollArea_->setWidget(contentWidget_);
    
    layout->addWidget(scrollArea_);
    
    setStyleSheet("background-color: #1e1e1e;");
}

void LyricsPanel::loadLyrics(const LyricsData& lyrics) {
    lyrics_ = lyrics;
    sync_->loadLyrics(lyrics);
    currentLine_ = -1;
    searchResults_.clear();
    updateLayout();
    update();
    
    LOG_INFO("LyricsPanel: Loaded {} lines", lyrics.lineCount());
}

void LyricsPanel::clear() {
    lyrics_ = LyricsData();
    sync_->clear();
    currentLine_ = -1;
    searchResults_.clear();
    lineInfo_.clear();
    update();
}

void LyricsPanel::setFollowPlayback(bool follow) {
    followPlayback_ = follow;
}

void LyricsPanel::search(const QString& query) {
    searchQuery_ = query.toLower();
    searchResults_.clear();
    currentSearchResult_ = -1;
    
    if (!searchQuery_.isEmpty() && !lyrics_.empty()) {
        auto results = lyrics_.search(searchQuery_.toStdString());
        searchResults_ = results;
        emit searchResultsFound(static_cast<int>(searchResults_.size()));
        
        // Jump to first result
        if (!searchResults_.empty()) {
            jumpToLine(static_cast<int>(searchResults_[0]));
        }
    }
    
    update();
}

void LyricsPanel::clearSearch() {
    searchEdit_->clear();
    searchQuery_.clear();
    searchResults_.clear();
    update();
}

void LyricsPanel::jumpToLine(int lineIndex) {
    if (lineIndex < 0 || lineIndex >= static_cast<int>(lyrics_.lines.size()))
        return;
    
    ensureLineVisible(lineIndex);
    
    // If audio is playing, seek to that line
    if (audio_ && lineIndex < static_cast<int>(lyrics_.lines.size())) {
        f32 targetTime = lyrics_.lines[lineIndex].startTime;
        audio_->seek(Duration(static_cast<i64>(targetTime * 1000)));
    }
}

void LyricsPanel::paintEvent(QPaintEvent* event) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    
    renderLyrics(painter);
}

void LyricsPanel::renderLyrics(QPainter& painter) {
    if (lyrics_.empty()) {
        painter.setPen(inactiveColor_);
        painter.drawText(rect(), Qt::AlignCenter, "No lyrics loaded");
        return;
    }
    
    painter.setFont(font_);
    QFontMetrics fm(font_);
    lineHeight_ = fm.height() + 10;
    
    int y = 20;
    int margin = 20;
    int width = this->width() - (margin * 2);
    
    lineInfo_.clear();
    lineInfo_.reserve(lyrics_.lines.size());
    
    for (size_t i = 0; i < lyrics_.lines.size(); ++i) {
        const auto& line = lyrics_.lines[i];
        bool isActive = (static_cast<int>(i) == currentLine_);
        bool isSearchResult = false;
        
        // Check if this line is a search result
        if (!searchQuery_.isEmpty()) {
            QString lineText = QString::fromStdString(line.text).toLower();
            isSearchResult = lineText.contains(searchQuery_);
        }
        
        // Calculate text rect
        QString text = QString::fromStdString(line.text);
        QRect textRect(margin, y, width, lineHeight_);
        
        // Store line info
        LineInfo info;
        info.rect = textRect;
        info.text = text;
        info.isActive = isActive;
        info.isSearchResult = isSearchResult;
        lineInfo_.push_back(info);
        
        // Draw background for active/search lines
        if (isActive) {
            painter.fillRect(textRect.adjusted(-10, -2, 10, 2), highlightColor_);
        } else if (isSearchResult) {
            painter.fillRect(textRect.adjusted(-10, -2, 10, 2), searchHighlightColor_);
        }
        
        // Draw text
        if (isActive) {
            painter.setPen(activeColor_);
            painter.setFont(QFont(font_.family(), font_.pointSize(), QFont::Bold));
        } else {
            QColor color = inactiveColor_;
            // Fade distant lines
            int distance = std::abs(static_cast<int>(i) - currentLine_);
            if (distance > 3) {
                int alpha = std::max(50, 200 - (distance - 3) * 40);
                color.setAlpha(alpha);
            }
            painter.setPen(color);
            painter.setFont(font_);
        }
        
        painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, text);
        
        y += lineHeight_;
    }
    
    contentHeight_ = y + 20;
}

void LyricsPanel::mousePressEvent(QMouseEvent* event) {
    int lineIdx = lineIndexAt(event->pos());
    if (lineIdx >= 0 && lineIdx < static_cast<int>(lyrics_.lines.size())) {
        emit lineClicked(lineIdx, QString::fromStdString(lyrics_.lines[lineIdx].text));
        jumpToLine(lineIdx);
    }
}

int LyricsPanel::lineIndexAt(const QPoint& pos) const {
    for (size_t i = 0; i < lineInfo_.size(); ++i) {
        if (lineInfo_[i].rect.contains(pos)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void LyricsPanel::ensureLineVisible(int lineIndex) {
    if (lineIndex < 0 || lineIndex >= static_cast<int>(lineInfo_.size()))
        return;
    
    const auto& info = lineInfo_[lineIndex];
    scrollArea_->ensureVisible(0, info.rect.center().y(), 0, 100);
}

void LyricsPanel::onPositionChanged(const LyricsSyncPosition& pos) {
    int newLine = pos.lineIndex;
    if (newLine != currentLine_) {
        currentLine_ = newLine;
        update();
        
        if (followPlayback_ && currentLine_ >= 0) {
            ensureLineVisible(currentLine_);
        }
    }
}

void LyricsPanel::onSearchTextChanged(const QString& text) {
    search(text);
}

void LyricsPanel::onAudioTrackChanged() {
    // Try to load lyrics for new track
    if (suno_ && audio_) {
        auto item = audio_->playlist().currentItem();
        if (item && !item->metadata.sunoClipId.empty()) {
            auto result = suno_->getLyrics(item->metadata.sunoClipId);
            if (result.isOk()) {
                // Convert AlignedLyrics to LyricsData
                // This is a simplified version - full implementation would convert properly
                clear();
            }
        }
    }
}

void LyricsPanel::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    update();
}

void LyricsPanel::wheelEvent(QWheelEvent* event) {
    // Temporarily disable auto-scroll when user scrolls manually
    bool wasFollowing = followPlayback_;
    followPlayback_ = false;
    
    QWidget::wheelEvent(event);
    
    // Re-enable after a delay (or leave disabled)
    QTimer::singleShot(5000, this, [this, wasFollowing]() {
        followPlayback_ = wasFollowing;
    });
}

void LyricsPanel::updateLayout() {
    // Recalculate layout
    update();
}

// CompactLyricsPanel implementation

CompactLyricsPanel::CompactLyricsPanel(AudioEngine* audio,
                                      suno::SunoController* suno,
                                      QWidget* parent)
    : LyricsPanel(audio, suno, parent) {
    // More compact styling
    font_.setPointSize(12);
    lineHeight_ = 24;
}

void CompactLyricsPanel::setCompactMode(bool compact) {
    if (compact) {
        font_.setPointSize(11);
        lineHeight_ = 22;
    } else {
        font_.setPointSize(14);
        lineHeight_ = 30;
    }
    update();
}

void CompactLyricsPanel::paintEvent(QPaintEvent* event) {
    // Simpler rendering for compact mode
    QPainter painter(this);
    
    if (lyrics_.empty()) {
        painter.setPen(inactiveColor_);
        painter.drawText(rect(), Qt::AlignCenter, "♪");
        return;
    }
    
    painter.setFont(font_);
    QFontMetrics fm(font_);
    
    int y = 10;
    int margin = 10;
    
    // Only render visible lines
    for (size_t i = 0; i < lyrics_.lines.size(); ++i) {
        if (static_cast<int>(i) < currentLine_ - 2 || 
            static_cast<int>(i) > currentLine_ + 4) {
            continue;
        }
        
        const auto& line = lyrics_.lines[i];
        bool isActive = (static_cast<int>(i) == currentLine_);
        
        QString text = QString::fromStdString(line.text);
        
        if (isActive) {
            painter.setPen(activeColor_);
            painter.setFont(QFont(font_.family(), font_.pointSize(), QFont::Bold));
        } else {
            painter.setPen(inactiveColor_);
            painter.setFont(font_);
        }
        
        painter.drawText(margin, y, width() - margin * 2, lineHeight_,
                        Qt::AlignLeft | Qt::AlignVCenter, text);
        
        y += lineHeight_;
    }
}

} // namespace vc
