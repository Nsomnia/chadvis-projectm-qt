#include "TextElement.hpp"
#include <QRegularExpression>
#include "audio/MediaMetadata.hpp"
#include "util/FileUtils.hpp"

namespace vc {

TextElement::TextElement() : id_("unnamed") {
}

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
    text_ = tmpl;
    dirty_ = true;
}

void TextElement::updateFromMetadata(const MediaMetadata& meta) {
    if (textTemplate_.isEmpty())
        return;

    QString result = textTemplate_;
    result.replace("{title}", QString::fromStdString(meta.displayTitle()));
    result.replace("{artist}", QString::fromStdString(meta.displayArtist()));
    result.replace("{album}", QString::fromStdString(meta.displayAlbum()));
    result.replace("{genre}", QString::fromStdString(meta.genre));
    result.replace("{year}", meta.year > 0 ? QString::number(meta.year) : "");
    result.replace(
            "{track}",
            meta.trackNumber > 0 ? QString::number(meta.trackNumber) : "");
    result.replace("{duration}",
                   QString::fromStdString(file::formatDuration(meta.duration)));
    result.replace("{bitrate}", QString::number(meta.bitrate) + " kbps");
    result.replace("{samplerate}", QString::number(meta.sampleRate) + " Hz");

    // Arch Linux user special: allow {system} tag for that extra spice
    result.replace("{system}", "Arch Linux (btw)");

    if (result != text_) {
        text_ = result;
        dirty_ = true;
    }
}

OverlayElementConfig TextElement::toConfig() const {
    OverlayElementConfig cfg;
    cfg.id = id_;
    cfg.text = textTemplate_.isEmpty() ? text_.toStdString()
                                       : textTemplate_.toStdString();
    cfg.position = position_;
    cfg.fontSize = style_.fontSize;
    cfg.color = Color{static_cast<u8>(style_.color.red()),
                      static_cast<u8>(style_.color.green()),
                      static_cast<u8>(style_.color.blue()),
                      static_cast<u8>(style_.color.alpha())};
    cfg.opacity = style_.opacity;
    cfg.visible = visible_;

    // Animation type to string
    switch (animation_.type) {
    case AnimationType::None:
        cfg.animation = "none";
        break;
    case AnimationType::FadePulse:
        cfg.animation = "fade_pulse";
        break;
    case AnimationType::Scroll:
        cfg.animation = "scroll";
        break;
    case AnimationType::Bounce:
        cfg.animation = "bounce";
        break;
    case AnimationType::TypeWriter:
        cfg.animation = "typewriter";
        break;
    case AnimationType::Wave:
        cfg.animation = "wave";
        break;
    case AnimationType::Shake:
        cfg.animation = "shake";
        break;
    case AnimationType::Scale:
        cfg.animation = "scale";
        break;
    case AnimationType::Rainbow:
        cfg.animation = "rainbow";
        break;
    case AnimationType::DVDBounce:
        cfg.animation = "dvd_bounce";
        break;
    }
    cfg.animationSpeed = animation_.speed;

    return cfg;
}

void TextElement::fromConfig(const OverlayElementConfig& config) {
    id_ = config.id;
    QString rawText = QString::fromStdString(config.text);
    if (rawText.contains("{") && rawText.contains("}")) {
        textTemplate_ = rawText;
        text_ = "Loading...";
    } else {
        text_ = rawText;
        textTemplate_ = "";
    }
    position_ = config.position;
    style_.fontSize = config.fontSize;
    style_.color = QColor(
            config.color.r, config.color.g, config.color.b, config.color.a);
    style_.opacity = config.opacity;
    visible_ = config.visible;

    anchor_ = parseAnchor(config.anchor);
    animation_.type = parseAnimationType(config.animation);
    animation_.speed = config.animationSpeed;

    dirty_ = true;
}

Vec2 TextElement::calculatePixelPosition(u32 canvasWidth,
                                         u32 canvasHeight,
                                         u32 textWidth,
                                         u32 textHeight) const {
    f32 x = position_.x * canvasWidth;
    f32 y = position_.y * canvasHeight;
    switch (anchor_) {
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
    default:
        break; // TopLeft
    }
    return {x, y};
}

TextAnchor TextElement::parseAnchor(const std::string& str) {
    if (str == "top_left" || str == "left")
        return TextAnchor::TopLeft;
    if (str == "top_center" || str == "top")
        return TextAnchor::TopCenter;
    if (str == "top_right")
        return TextAnchor::TopRight;
    if (str == "center_left")
        return TextAnchor::CenterLeft;
    if (str == "center")
        return TextAnchor::Center;
    if (str == "center_right" || str == "right")
        return TextAnchor::CenterRight;
    if (str == "bottom_left")
        return TextAnchor::BottomLeft;
    if (str == "bottom_center" || str == "bottom")
        return TextAnchor::BottomCenter;
    if (str == "bottom_right")
        return TextAnchor::BottomRight;
    return TextAnchor::TopLeft;
}

AnimationType TextElement::parseAnimationType(const std::string& str) {
    if (str == "none")
        return AnimationType::None;
    if (str == "fade_pulse" || str == "pulse")
        return AnimationType::FadePulse;
    if (str == "scroll")
        return AnimationType::Scroll;
    if (str == "bounce")
        return AnimationType::Bounce;
    if (str == "typewriter")
        return AnimationType::TypeWriter;
    if (str == "wave")
        return AnimationType::Wave;
    if (str == "shake")
        return AnimationType::Shake;
    if (str == "scale")
        return AnimationType::Scale;
    if (str == "rainbow")
        return AnimationType::Rainbow;
    if (str == "dvd_bounce")
        return AnimationType::DVDBounce;
    return AnimationType::None;
}

} // namespace vc
