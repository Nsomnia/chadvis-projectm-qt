#pragma once
// TextElement.hpp - Individual text overlay item
// Because hardcoded watermarks are for amateurs

#include <QColor>
#include <QFont>
#include <QString>
#include "core/Config.hpp"
#include "util/Types.hpp"

namespace vc {

// Forward declaration
struct MediaMetadata;

enum class TextAnchor {
    TopLeft,
    TopCenter,
    TopRight,
    CenterLeft,
    Center,
    CenterRight,
    BottomLeft,
    BottomCenter,
    BottomRight
};

enum class AnimationType {
    None,
    FadePulse, // Fade in and out
    Scroll, // Scroll horizontally
    Bounce, // Bounce up and down
    TypeWriter, // Character by character reveal
    Wave, // Wavy text effect
    Shake, // Random shake (beat reactive)
    Scale, // Scale pulse
    Rainbow, // Color cycling
    DVDBounce // DVD logo style edge bouncing
};

struct TextStyle {
    QString fontFamily{"Liberation Sans"};
    u32 fontSize{32};
    QColor color{Qt::white};
    f32 opacity{1.0f};
    bool bold{false};
    bool italic{false};
    bool shadow{true};
    QColor shadowColor{0, 0, 0, 128};
    Vec2 shadowOffset{2.0f, 2.0f};
    bool outline{false};
    QColor outlineColor{Qt::black};
    f32 outlineWidth{1.0f};
};

struct AnimationParams {
    AnimationType type{AnimationType::None};
    f32 speed{1.0f};
    f32 amplitude{1.0f}; // For bounce/wave
    f32 phase{0.0f}; // Starting phase offset
    bool beatReactive{false};
};

class TextElement {
public:
    TextElement();
    explicit TextElement(const OverlayElementConfig& config);

    // Identification
    std::string id() const {
        return id_;
    }
    void setId(const std::string& id) {
        id_ = id;
    }

    // Content
    QString text() const {
        return text_;
    }
    void setText(const QString& text);
    void setTextTemplate(const QString& tmpl); // With {placeholders}
    void updateFromMetadata(const MediaMetadata& meta);

    // Position (normalized 0-1)
    Vec2 position() const {
        return position_;
    }
    void setPosition(Vec2 pos) {
        position_ = pos;
    }
    void setPosition(f32 x, f32 y) {
        position_ = {x, y};
    }

    // Anchor point
    TextAnchor anchor() const {
        return anchor_;
    }
    void setAnchor(TextAnchor anchor) {
        anchor_ = anchor;
    }

    // Style
    const TextStyle& style() const {
        return style_;
    }
    TextStyle& style() {
        return style_;
    }
    void setStyle(const TextStyle& style) {
        style_ = style;
    }

    // Animation
    const AnimationParams& animation() const {
        return animation_;
    }
    AnimationParams& animation() {
        return animation_;
    }
    void setAnimation(const AnimationParams& anim) {
        animation_ = anim;
    }

    // Visibility
    bool visible() const {
        return visible_;
    }
    void setVisible(bool v) {
        visible_ = v;
    }
    void toggleVisible() {
        visible_ = !visible_;
    }

    // State
    bool isDirty() const {
        return dirty_;
    }
    void markClean() {
        dirty_ = false;
    }

    // Convert to/from config
    OverlayElementConfig toConfig() const;
    void fromConfig(const OverlayElementConfig& config);

    // Calculate pixel position for given canvas size
    Vec2 calculatePixelPosition(u32 canvasWidth,
                                u32 canvasHeight,
                                u32 textWidth,
                                u32 textHeight) const;

private:
    static TextAnchor parseAnchor(const std::string& str);
    static AnimationType parseAnimationType(const std::string& str);

    std::string id_;
    QString text_;
    QString textTemplate_;
    Vec2 position_{0.5f, 0.5f};
    TextAnchor anchor_{TextAnchor::TopLeft};
    TextStyle style_;
    AnimationParams animation_;
    bool visible_{true};
    bool dirty_{true};
};

} // namespace vc