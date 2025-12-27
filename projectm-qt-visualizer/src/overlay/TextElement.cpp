#include "TextElement.hpp"
#include "audio/MediaMetadata.hpp"
#include "util/FileUtils.hpp"

namespace vc {

TextElement::TextElement() = default;

TextElement::TextElement(const OverlayElementConfig& config) {
    fromConfig(config);
}

void TextElement::setText(const QString& text) {
    if (text_ != text) {
        text_ = text;
        dirty_ = true;
    }
}

void TextElement::setTextTemplate(const QString& tmpl) {
    textTemplate_ = tmpl;
    text_ = tmpl;  // Show template until metadata update
    dirty_ = true;
}

void TextElement::updateFromMetadata(const MediaMetadata& meta) {
    if (textTemplate_.isEmpty()) return;
    
    QString result = textTemplate_;
    
    result.replace("{title}", QString::fromStdString(meta.displayTitle()));
    result.replace("{artist}", QString::fromStdString(meta.displayArtist()));
    result.replace("{album}", QString::fromStdString(meta.displayAlbum()));
    result.replace("{genre}", QString::fromStdString(meta.genre));
    result.replace("{year}", meta.year > 0 ? QString::number(meta.year) : "");
    result.replace("{track}", meta.trackNumber > 0 ? QString::number(meta.trackNumber) : "");
    result.replace("{duration}", QString::fromStdString(file::formatDuration(meta.duration)));
    result.replace("{bitrate}", QString::number(meta.bitrate) + " kbps");
    
    // Clean up empty placeholders
    result.replace(" - {artist}", "");
    result.replace("{artist} - ", "");
    
    setText(result);
}

Vec2 TextElement::calculatePixelPosition(u32 canvasWidth, u32 canvasHeight,
                                          u32 textWidth, u32 textHeight) const {
    // Start with normalized position converted to pixels
    f32 x = position_.x * canvasWidth;
    f32 y = position_.y * canvasHeight;
    
    // Adjust based on anchor
    switch (anchor_) {
        case TextAnchor::TopLeft:
            break;
        case TextAnchor::TopCenter:
            x -= textWidth * 0.5f;
            break;
        case TextAnchor::TopRight:
            x -= textWidth;
            break;
        case TextAnchor::CenterLeft:
            y -= textHeight * 0.5f;
            break;
        case TextAnchor::Center:
            x -= textWidth * 0.5f;
            y -= textHeight * 0.5f;
            break;
        case TextAnchor::CenterRight:
            x -= textWidth;
            y -= textHeight * 0.5f;
            break;
        case TextAnchor::BottomLeft:
            y -= textHeight;
            break;
        case TextAnchor::BottomCenter:
            x -= textWidth * 0.5f;
            y -= textHeight;
            break;
        case TextAnchor::BottomRight:
            x -= textWidth;
            y -= textHeight;
            break;
    }
    
    return {x, y};
}

OverlayElementConfig TextElement::toConfig() const {
    OverlayElementConfig cfg;
    cfg.id = id_;
    cfg.text = text_.toStdString();
    cfg.position = position_;
    cfg.fontSize = style_.fontSize;
    cfg.color = Color{
        static_cast<u8>(style_.color.red()),
        static_cast<u8>(style_.color.green()),
        static_cast<u8>(style_.color.blue()),
        static_cast<u8>(style_.color.alpha())
    };
    cfg.opacity = style_.opacity;
    cfg.visible = visible_;
    
    // Animation type to string
    switch (animation_.type) {
        case AnimationType::None: cfg.animation = "none"; break;
        case AnimationType::FadePulse: cfg.animation = "fade_pulse"; break;
        case AnimationType::Scroll: cfg.animation = "scroll"; break;
        case AnimationType::Bounce: cfg.animation = "bounce"; break;
        case AnimationType::TypeWriter: cfg.animation = "typewriter"; break;
        case AnimationType::Wave: cfg.animation = "wave"; break;
        case AnimationType::Shake: cfg.animation = "shake"; break;
        case AnimationType::Scale: cfg.animation = "scale"; break;
        case AnimationType::Rainbow: cfg.animation = "rainbow"; break;
    }
    cfg.animationSpeed = animation_.speed;
    
    // Anchor to string
    switch (anchor_) {
        case TextAnchor::TopLeft: cfg.anchor = "top_left"; break;
        case TextAnchor::TopCenter: cfg.anchor = "top_center"; break;
        case TextAnchor::TopRight: cfg.anchor = "top_right"; break;
        case TextAnchor::CenterLeft: cfg.anchor = "center_left"; break;
        case TextAnchor::Center: cfg.anchor = "center"; break;
        case TextAnchor::CenterRight: cfg.anchor = "center_right"; break;
        case TextAnchor::BottomLeft: cfg.anchor = "bottom_left"; break;
        case TextAnchor::BottomCenter: cfg.anchor = "bottom_center"; break;
        case TextAnchor::BottomRight: cfg.anchor = "bottom_right"; break;
    }
    
    return cfg;
}

void TextElement::fromConfig(const OverlayElementConfig& config) {
    id_ = config.id;
    position_ = config.position;
    visible_ = config.visible;
    
    // Check if text has placeholders
    QString txt = QString::fromStdString(config.text);
    if (txt.contains('{') && txt.contains('}')) {
        setTextTemplate(txt);
    } else {
        setText(txt);
    }
    
    style_.fontSize = config.fontSize;
    style_.color = QColor(config.color.r, config.color.g, config.color.b, config.color.a);
    style_.opacity = config.opacity;
    
    anchor_ = parseAnchor(config.anchor);
    animation_.type = parseAnimationType(config.animation);
    animation_.speed = config.animationSpeed;
    
    dirty_ = true;
}

TextAnchor TextElement::parseAnchor(const std::string& str) {
    if (str == "top_left" || str == "left") return TextAnchor::TopLeft;
    if (str == "top_center" || str == "top") return TextAnchor::TopCenter;
    if (str == "top_right") return TextAnchor::TopRight;
    if (str == "center_left") return TextAnchor::CenterLeft;
    if (str == "center") return TextAnchor::Center;
    if (str == "center_right" || str == "right") return TextAnchor::CenterRight;
    if (str == "bottom_left") return TextAnchor::BottomLeft;
    if (str == "bottom_center" || str == "bottom") return TextAnchor::BottomCenter;
    if (str == "bottom_right") return TextAnchor::BottomRight;
    return TextAnchor::TopLeft;
}

AnimationType TextElement::parseAnimationType(const std::string& str) {
    if (str == "none") return AnimationType::None;
    if (str == "fade_pulse" || str == "pulse" || str == "fade") return AnimationType::FadePulse;
    if (str == "scroll") return AnimationType::Scroll;
    if (str == "bounce") return AnimationType::Bounce;
    if (str == "typewriter" || str == "type") return AnimationType::TypeWriter;
    if (str == "wave") return AnimationType::Wave;
    if (str == "shake") return AnimationType::Shake;
    if (str == "scale") return AnimationType::Scale;
    if (str == "rainbow") return AnimationType::Rainbow;
    return AnimationType::None;
}

} // namespace vc