#pragma once
// OverlayEngine.hpp - Text overlay rendering engine
// QPainter doing the heavy lifting

#include "util/Types.hpp"
#include "TextElement.hpp"
#include "TextAnimator.hpp"
#include "OverlayConfig.hpp"
#include "audio/MediaMetadata.hpp"

#include <QPainter>
#include <QImage>
#include "util/GLIncludes.hpp"
#include <QOpenGLTexture>
#include <memory>

namespace vc {

class OverlayEngine {
public:
    OverlayEngine();
    ~OverlayEngine();
    
    // Initialize (call after GL context ready)
    void init();
    
    // Configuration
    OverlayConfig& config() { return config_; }
    const OverlayConfig& config() const { return config_; }
    
    // Animation
    TextAnimator& animator() { return animator_; }
    
    // Update
    void update(f32 deltaTime);
    void onBeat(f32 intensity);
    void updateMetadata(const MediaMetadata& meta);
    
    // Rendering
    void render(u32 width, u32 height);
    void renderToImage(QImage& image);
    
    // GL texture for compositing (call after render)
    GLuint texture() const;
    
    // Enable/disable
    bool enabled() const { return enabled_; }
    void setEnabled(bool e) { enabled_ = e; }
    
private:
    void renderElement(QPainter& painter, TextElement& element,
                       u32 canvasWidth, u32 canvasHeight);
    void updateTexture();
    QFont createFont(const TextStyle& style);
    
    OverlayConfig config_;
    TextAnimator animator_;
    
    std::unique_ptr<QImage> canvas_;
    std::unique_ptr<QOpenGLTexture> texture_;
    
    u32 lastWidth_{0};
    u32 lastHeight_{0};
    bool enabled_{true};
    bool needsTextureUpdate_{false};
    
    MediaMetadata currentMetadata_;
};

} // namespace vc