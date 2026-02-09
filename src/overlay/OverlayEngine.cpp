#include "OverlayEngine.hpp"
#include <QFontMetrics>
#include <QPainterPath>
#include "core/Logger.hpp"
#include "core/Config.hpp"

namespace vc {

OverlayEngine::OverlayEngine()
    : renderer_(std::make_unique<OverlayRenderer>()) {
}

OverlayEngine::~OverlayEngine() {
    cleanup();
}

void OverlayEngine::init() {
    config_.loadFromAppConfig();

    if (config_.empty()) {
        config_.createDefaultWatermark();
        config_.createNowPlayingElement();
        config_.saveToAppConfig();
    }

    LOG_INFO("OverlayEngine: Initialized with {} elements", config_.count());

    // Renderer init is delayed until render() ensures GL context
}

void OverlayEngine::cleanup() {
    if (renderer_) {
        renderer_->cleanup();
    }
}

void OverlayEngine::update(f32 deltaTime) {
    if (!enabled_)
        return;
    animator_.update(deltaTime);
}

void OverlayEngine::onBeat(f32 intensity) {
    if (!enabled_)
        return;
    animator_.onBeat(intensity);
}

void OverlayEngine::updateMetadata(const MediaMetadata& meta) {
    currentMetadata_ = meta;
    for (auto& elem : config_) {
        elem->updateFromMetadata(meta);
    }
}

void OverlayEngine::render(u32 width, u32 height) {
    if (!enabled_)
        return;

    // 1. Initialize Renderer if needed
    if (!renderer_->isInitialized()) {
        renderer_->init();
    }

    // 2. Check if we need to redraw the canvas (CPU side)
    bool mustRedraw = needsUpload_;
    if (width != lastWidth_ || height != lastHeight_)
        mustRedraw = true;

    if (!mustRedraw) {
        // Check animations or dirty flags
        for (const auto& elem : config_) {
            if (!elem->visible())
                continue;
            if (elem->isDirty()) {
                mustRedraw = true;
                break;
            }
            if (elem->animation().type != AnimationType::None) {
                mustRedraw = true;
                break;
            }
        }
    }

    // 3. Draw to canvas if needed
    if (mustRedraw) {
        drawToCanvas(width, height);
        // 4. Upload to GPU
        if (canvas_)
            renderer_->upload(*canvas_);
        needsUpload_ = false;
    }

    // 5. Draw Quad
    // Ensure blending is enabled for the overlay texture
    renderer_->draw();
}

void OverlayEngine::setAlignedLyrics(const suno::AlignedLyrics& lyrics) {
    std::lock_guard lock(mutex_);
    alignedLyrics_ = lyrics;
    // Log lyrics reception for verification
    if (!lyrics.lines.empty()) {
        LOG_INFO("OverlayEngine: Received {} lines of lyrics for song: {}", lyrics.lines.size(), lyrics.songId);
    } else {
        LOG_WARN("OverlayEngine: Received empty lyrics payload for song: {}", lyrics.songId);
    }
    needsUpload_ = true;
}

void OverlayEngine::updatePlaybackTime(f32 time_s) {
    std::lock_guard lock(mutex_);
    playbackTime_ = time_s;
    // We always need to redraw if we have synced lyrics
    if (!alignedLyrics_.empty()) {
        needsUpload_ = true;
    }
}

void OverlayEngine::drawToCanvas(u32 width, u32 height) {
    // Recreate if size changed
    if (width != lastWidth_ || height != lastHeight_ || !canvas_) {
        canvas_ = std::make_unique<QImage>(
                width, height, QImage::Format_RGBA8888);
        lastWidth_ = width;
        lastHeight_ = height;
    }

    canvas_->fill(Qt::transparent);

    if (config_.empty())
        return;

    QPainter painter(canvas_.get());
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);

    for (auto& elem : config_) {
        if (elem->visible()) {
            renderElement(painter, *elem, width, height);
            elem->markClean();
        }
    }

    // Render Karaoke/Synced Lyrics
    if (enabled_) {
        std::lock_guard lock(mutex_);
        const auto& kConfig = CONFIG.karaoke();
        
        // Trace logging: detailed diagnostics for lyrics rendering
        static int traceCounter = 0;
        bool shouldLogTrace = (traceCounter++ % 300 == 0); // Once per ~5 seconds at 60fps
        
        if (shouldLogTrace && !alignedLyrics_.empty()) {
            LOG_TRACE("OverlayEngine: Lyrics state - Lines: {}, Words: {}, PlaybackTime: {:.2f}s, KaraokeEnabled: {}", 
                      alignedLyrics_.lines.size(), alignedLyrics_.words.size(), 
                      playbackTime_, kConfig.enabled);
        }
        
        // Detailed tracing for why lyrics might not render
        if (shouldLogTrace && !alignedLyrics_.empty() && !kConfig.enabled) {
            LOG_DEBUG("OverlayEngine: Lyrics available but karaoke display is disabled in config");
        }
        
        if (kConfig.enabled && !alignedLyrics_.empty()) {
            if (!alignedLyrics_.lines.empty()) {
                // Find active line with detailed tracing
                int activeLineIdx = -1;
                for (int i = 0; i < (int)alignedLyrics_.lines.size(); ++i) {
                    if (playbackTime_ >= alignedLyrics_.lines[i].start_s &&
                        playbackTime_ <= alignedLyrics_.lines[i].end_s) {
                        activeLineIdx = i;
                        break;
                    }
                }

                // Trace why no active line found
                if (activeLineIdx == -1 && shouldLogTrace) {
                    if (playbackTime_ < alignedLyrics_.lines.front().start_s) {
                        LOG_TRACE("OverlayEngine: Playback time {:.2f}s is before first line at {:.2f}s",
                                  playbackTime_, alignedLyrics_.lines.front().start_s);
                    } else if (playbackTime_ > alignedLyrics_.lines.back().end_s) {
                        LOG_TRACE("OverlayEngine: Playback time {:.2f}s is after last line at {:.2f}s",
                                  playbackTime_, alignedLyrics_.lines.back().end_s);
                    } else {
                        // Search for gap between lines
                        for (size_t i = 0; i < alignedLyrics_.lines.size() - 1; ++i) {
                            if (playbackTime_ > alignedLyrics_.lines[i].end_s && 
                                playbackTime_ < alignedLyrics_.lines[i+1].start_s) {
                                LOG_TRACE("OverlayEngine: Playback time {:.2f}s is in gap between lines {} and {} (gap: {:.2f}s)",
                                          playbackTime_, i, i+1,
                                          alignedLyrics_.lines[i+1].start_s - alignedLyrics_.lines[i].end_s);
                                break;
                            }
                        }
                    }
                }

                if (activeLineIdx != -1) {
                    const auto& line = alignedLyrics_.lines[activeLineIdx];
                    
                    QFont font(QString::fromStdString(kConfig.fontFamily));
                    font.setPixelSize(kConfig.fontSize);
                    font.setBold(kConfig.bold);
                    painter.setFont(font);
                    
                    // Center the line
                    QFontMetrics fm(painter.font());
                    int textWidth = fm.horizontalAdvance(QString::fromStdString(line.text));
                    f32 currentX = (width - textWidth) / 2.0f;
                    f32 centerY = height * kConfig.yPosition;

                    // Draw words with highlighting
                    int wordsRendered = 0;
                    for (const auto& w : line.words) {
                        bool active = (playbackTime_ >= w.start_s && playbackTime_ <= w.end_s);
                        
                        QString wordText = QString::fromStdString(w.word + " ");
                        int wordWidth = fm.horizontalAdvance(wordText);

                        QColor activeC(kConfig.activeColor.r, kConfig.activeColor.g, kConfig.activeColor.b, kConfig.activeColor.a);
                        QColor inactiveC(kConfig.inactiveColor.r, kConfig.inactiveColor.g, kConfig.inactiveColor.b, kConfig.inactiveColor.a);
                        QColor shadowC(kConfig.shadowColor.r, kConfig.shadowColor.g, kConfig.shadowColor.b, kConfig.shadowColor.a);

                        // Shadow
                        painter.setPen(shadowC);
                        painter.drawText(currentX + 2, centerY + 2, wordText);
                        
                        // Main Text
                        painter.setPen(active ? activeC : inactiveC);
                        painter.drawText(currentX, centerY, wordText);

                        currentX += wordWidth;
                        wordsRendered++;
                    }
                    
                    if (shouldLogTrace) {
                        LOG_TRACE("OverlayEngine: Rendered line {} with {} words at time {:.2f}s",
                                  activeLineIdx, wordsRendered, playbackTime_);
                    }
                }
            } else {
                // Fallback: Simple strategy: Find words around current playback time
                int activeIdx = -1;
                for (int i = 0; i < (int)alignedLyrics_.words.size(); ++i) {
                    if (playbackTime_ >= alignedLyrics_.words[i].start_s &&
                        playbackTime_ <= alignedLyrics_.words[i].end_s) {
                        activeIdx = i;
                        break;
                    }
                }

                if (activeIdx != -1) {
                    int start = std::max(0, activeIdx - 5);
                    int end = std::min((int)alignedLyrics_.words.size() - 1,
                                       activeIdx + 5);

                    QFont font(QString::fromStdString(kConfig.fontFamily));
                    font.setPixelSize(kConfig.fontSize);
                    font.setBold(kConfig.bold);
                    painter.setFont(font);

                    f32 currentX = width * 0.1f;
                    f32 centerY = height * kConfig.yPosition;

                    for (int i = start; i <= end; ++i) {
                        const auto& w = alignedLyrics_.words[i];
                        bool active = (i == activeIdx);

                        QColor activeC(kConfig.activeColor.r, kConfig.activeColor.g, kConfig.activeColor.b, kConfig.activeColor.a);
                        QColor inactiveC(kConfig.inactiveColor.r, kConfig.inactiveColor.g, kConfig.inactiveColor.b, kConfig.inactiveColor.a);
                        QColor shadowC(kConfig.shadowColor.r, kConfig.shadowColor.g, kConfig.shadowColor.b, kConfig.shadowColor.a);

                        painter.setPen(shadowC);
                        painter.drawText(currentX + 2,
                                         centerY + 2,
                                         QString::fromStdString(w.word));
                        painter.setPen(active ? activeC : inactiveC);
                        painter.drawText(
                                currentX, centerY, QString::fromStdString(w.word));

                        currentX +=
                                QFontMetrics(painter.font())
                                        .horizontalAdvance(QString::fromStdString(
                                                w.word + " "));
                    }
                }
            }
        }
    }
}

void OverlayEngine::renderElement(QPainter& painter,
                                  TextElement& element,
                                  u32 width,
                                  u32 height) {
    AnimationState state =
            animator_.computeAnimatedState(element, width, height);
    const auto& style = element.style();

    QFont font = createFont(style);
    if (std::abs(state.scale - 1.0f) > 0.001f) {
        font.setPointSizeF(font.pointSizeF() * state.scale);
    }
    painter.setFont(font);

    QFontMetrics fm(font);
    QString text = state.visibleText;
    QRect textRect = fm.boundingRect(text);

    Vec2 pixelPos = element.calculatePixelPosition(
            width, height, textRect.width(), textRect.height());

    // Add Animation Offset
    pixelPos.x += state.offset.x;
    pixelPos.y += state.offset.y;

    // Simple clamping
    pixelPos.x = std::clamp(pixelPos.x, -200.0f, static_cast<f32>(width));
    pixelPos.y = std::clamp(pixelPos.y, -200.0f, static_cast<f32>(height));

    QPointF pos(pixelPos.x, pixelPos.y + textRect.height());

    painter.setOpacity(state.opacity);

    // Shadow
    if (style.shadow) {
        QColor shadowC = style.shadowColor;
        shadowC.setAlphaF(shadowC.alphaF() * state.opacity);
        painter.setPen(shadowC);
        painter.drawText(
                pos + QPointF(style.shadowOffset.x, style.shadowOffset.y),
                text);
    }

    // Outline
    if (style.outline) {
        QPainterPath path;
        path.addText(pos, font, text);
        QPen outlinePen(style.outlineColor);
        outlinePen.setWidthF(style.outlineWidth * 2);
        painter.strokePath(path, outlinePen);
    }

    // Text
    QColor textC = state.color;
    textC.setAlphaF(textC.alphaF() * state.opacity);
    painter.setPen(textC);
    painter.drawText(pos, text);

    painter.setOpacity(1.0f);
}

QFont OverlayEngine::createFont(const TextStyle& style) {
    QFont font(style.fontFamily);
    font.setPointSize(style.fontSize);
    font.setBold(style.bold);
    font.setItalic(style.italic);
    return font;
}

} // namespace vc
