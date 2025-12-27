#include "OverlayEngine.hpp"
#include "core/Logger.hpp"

#include <QPainterPath>
#include <QFontMetrics>

namespace vc {

OverlayEngine::OverlayEngine() = default;

OverlayEngine::~OverlayEngine() = default;

void OverlayEngine::init() {
    config_.loadFromAppConfig();
    
    // Create default elements if none exist
    if (config_.empty()) {
        config_.createDefaultWatermark();
        config_.createNowPlayingElement();
        config_.saveToAppConfig();
    }
    
    LOG_INFO("Overlay engine initialized with {} elements", config_.count());
}

void OverlayEngine::update(f32 deltaTime) {
    if (!enabled_) return;
    animator_.update(deltaTime);
}

void OverlayEngine::onBeat(f32 intensity) {
    if (!enabled_) return;
    animator_.onBeat(intensity);
}

void OverlayEngine::updateMetadata(const MediaMetadata& meta) {
    currentMetadata_ = meta;
    
    // Update all elements with new metadata
    for (auto& elem : config_) {
        elem->updateFromMetadata(meta);
    }
}

void OverlayEngine::render(u32 width, u32 height) {
    if (!enabled_ || config_.empty()) return;
    
    // Recreate canvas if size changed
    if (width != lastWidth_ || height != lastHeight_) {
        canvas_ = std::make_unique<QImage>(width, height, QImage::Format_RGBA8888);
        lastWidth_ = width;
        lastHeight_ = height;
        texture_.reset();
    }
    
    // Clear canvas
    canvas_->fill(Qt::transparent);
    
    // Render elements
    QPainter painter(canvas_.get());
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    
    for (auto& elem : config_) {
        if (elem->visible()) {
            renderElement(painter, *elem, width, height);
        }
    }
    
    painter.end();
    
    needsTextureUpdate_ = true;
}

void OverlayEngine::renderToImage(QImage& image) {
    if (!enabled_ || config_.empty()) return;
    
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    
    for (auto& elem : config_) {
        if (elem->visible()) {
            renderElement(painter, *elem, image.width(), image.height());
        }
    }
    
    painter.end();
}

void OverlayEngine::renderElement(QPainter& painter, TextElement& element,
                                   u32 canvasWidth, u32 canvasHeight) {
    // Get animated state
    AnimationState state = animator_.computeAnimatedState(element, canvasWidth, canvasHeight);
    const auto& style = element.style();
    
    // Create font
    QFont font = createFont(style);
    
    // Scale font if animation requires it
    if (std::abs(state.scale - 1.0f) > 0.001f) {
        font.setPointSizeF(font.pointSizeF() * state.scale);
    }
    
    painter.setFont(font);
    
    // Calculate text metrics
    QFontMetrics fm(font);
    QString text = state.visibleText;
    QRect textRect = fm.boundingRect(text);
    
    // Calculate position
    Vec2 pixelPos = element.calculatePixelPosition(
        canvasWidth, canvasHeight, textRect.width(), textRect.height());
    
    // Apply animation offset
    pixelPos.x += state.offset.x;
    pixelPos.y += state.offset.y;
    
    // Clamp to screen
    pixelPos.x = std::clamp(pixelPos.x, 0.0f, static_cast<f32>(canvasWidth - textRect.width()));
    pixelPos.y = std::clamp(pixelPos.y, 0.0f, static_cast<f32>(canvasHeight));
    
    QPointF pos(pixelPos.x, pixelPos.y + textRect.height());  // Qt draws from baseline
    
    // Set opacity
    painter.setOpacity(state.opacity);
    
    // Draw shadow
    if (style.shadow) {
        QColor shadowColor = style.shadowColor;
        shadowColor.setAlphaF(shadowColor.alphaF() * state.opacity);
        painter.setPen(shadowColor);
        painter.drawText(pos + QPointF(style.shadowOffset.x, style.shadowOffset.y), text);
    }
    
    // Draw outline
    if (style.outline) {
        QPainterPath path;
        path.addText(pos, font, text);
        
        QPen outlinePen(style.outlineColor);
        outlinePen.setWidthF(style.outlineWidth * 2);
        painter.strokePath(path, outlinePen);
    }
    
    // Draw text
    QColor textColor = state.color;
    textColor.setAlphaF(textColor.alphaF() * state.opacity);
    painter.setPen(textColor);
    painter.drawText(pos, text);
    
    // Reset opacity
    painter.setOpacity(1.0f);
}

GLuint OverlayEngine::texture() const {
    if (needsTextureUpdate_ && canvas_) {
        const_cast<OverlayEngine*>(this)->updateTexture();
    }
    
    return texture_ ? texture_->textureId() : 0;
}

void OverlayEngine::updateTexture() {
    if (!canvas_) return;
    
    if (!texture_) {
        texture_ = std::make_unique<QOpenGLTexture>(QOpenGLTexture::Target2D);
        texture_->setMinificationFilter(QOpenGLTexture::Linear);
        texture_->setMagnificationFilter(QOpenGLTexture::Linear);
        texture_->setWrapMode(QOpenGLTexture::ClampToEdge);
    }
    
    // Upload image to texture
    if (texture_->isCreated()) {
        texture_->destroy();
    }
    texture_->setData(*canvas_);
    
    needsTextureUpdate_ = false;
}

QFont OverlayEngine::createFont(const TextStyle& style) {
    QFont font(style.fontFamily);
    font.setPointSize(style.fontSize);
    font.setBold(style.bold);
    font.setItalic(style.italic);
    return font;
}

} // namespace vc