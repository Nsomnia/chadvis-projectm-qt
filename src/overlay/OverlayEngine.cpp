#include "OverlayEngine.hpp"
#include <QFontMetrics>
#include <QPainterPath>
#include "core/Logger.hpp"

namespace vc {

OverlayEngine::OverlayEngine()
    : renderer_(std::make_unique<OverlayRenderer>()) {
}

OverlayEngine::~OverlayEngine() = default;

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
    renderer_->draw();
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
