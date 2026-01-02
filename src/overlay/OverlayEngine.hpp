#pragma once
// OverlayEngine.hpp - Text overlay logic and composition
// "I write the pixels, OverlayRenderer draws them."

#include "OverlayConfig.hpp"
#include "OverlayRenderer.hpp"
#include "TextAnimator.hpp"
#include "TextElement.hpp"
#include "audio/MediaMetadata.hpp"
#include "util/Types.hpp"

#include <QImage>
#include <QPainter>
#include <memory>

namespace vc {

class OverlayEngine {
public:
    OverlayEngine();
    ~OverlayEngine();

    // Initialize (Load config, setup renderer)
    void init();

    // Configuration Access
    OverlayConfig& config() {
        return config_;
    }
    const OverlayConfig& config() const {
        return config_;
    }

    // Animation Access
    TextAnimator& animator() {
        return animator_;
    }

    // Runtime Updates
    void update(f32 deltaTime);
    void onBeat(f32 intensity);
    void updateMetadata(const MediaMetadata& meta);

    // Rendering
    // 1. Updates animation state
    // 2. Draws text to QImage (if needed)
    // 3. Uploads to GPU (if needed)
    // 4. Draws full screen quad
    void render(u32 width, u32 height);

    // Helpers
    void setEnabled(bool e) {
        enabled_ = e;
    }
    bool enabled() const {
        return enabled_;
    }

private:
    // Logic Methods
    void drawToCanvas(u32 width, u32 height);
    void renderElement(QPainter& painter,
                       TextElement& element,
                       u32 width,
                       u32 height);
    QFont createFont(const TextStyle& style);

    // State
    OverlayConfig config_;
    TextAnimator animator_;
    MediaMetadata currentMetadata_;

    // Rendering Resources
    std::unique_ptr<OverlayRenderer> renderer_;
    std::unique_ptr<QImage> canvas_;

    u32 lastWidth_{0};
    u32 lastHeight_{0};
    bool enabled_{true};
    bool needsUpload_{false};
};

} // namespace vc
