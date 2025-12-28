#include "OverlayConfig.hpp"
#include "core/Config.hpp"
#include "core/Logger.hpp"

namespace vc {

OverlayConfig::OverlayConfig() = default;

Result<void> OverlayConfig::load(const fs::path& path) {
    // For now, just load from app config
    // Could add separate overlay config file later
    loadFromAppConfig();
    return Result<void>::ok();
}

Result<void> OverlayConfig::save(const fs::path& path) const {
    // Save to app config
    const_cast<OverlayConfig*>(this)->saveToAppConfig();
    return CONFIG.save(path);
}

void OverlayConfig::loadFromAppConfig() {
    clear();
    
    for (const auto& elemConfig : CONFIG.overlayElements()) {
        addElement(elemConfig);
    }
    
    LOG_DEBUG("Loaded {} overlay elements from config", elements_.size());
}

void OverlayConfig::saveToAppConfig() {
    auto& cfgElements = CONFIG.overlayElements();
    cfgElements.clear();
    
    for (const auto& elem : elements_) {
        cfgElements.push_back(elem->toConfig());
    }
}

TextElement* OverlayConfig::addElement() {
    auto elem = std::make_unique<TextElement>();
    elem->setId(generateId());
    elem->setText("New Text");
    elem->setPosition(0.5f, 0.5f);
    elem->setAnchor(TextAnchor::Center);
    
    TextStyle style;
    style.fontFamily = QString::fromStdString(defaultFont_);
    style.fontSize = defaultFontSize_;
    elem->setStyle(style);
    
    elements_.push_back(std::move(elem));
    return elements_.back().get();
}

TextElement* OverlayConfig::addElement(const OverlayElementConfig& config) {
    auto elem = std::make_unique<TextElement>(config);
    if (elem->id().empty()) {
        elem->setId(generateId());
    }
    elements_.push_back(std::move(elem));
    return elements_.back().get();
}

void OverlayConfig::removeElement(const std::string& id) {
    std::erase_if(elements_, [&id](const auto& e) { return e->id() == id; });
}

void OverlayConfig::removeElementAt(usize index) {
    if (index < elements_.size()) {
        elements_.erase(elements_.begin() + index);
    }
}

void OverlayConfig::clear() {
    elements_.clear();
}

TextElement* OverlayConfig::findById(const std::string& id) {
    for (auto& elem : elements_) {
        if (elem->id() == id) return elem.get();
    }
    return nullptr;
}

const TextElement* OverlayConfig::findById(const std::string& id) const {
    for (const auto& elem : elements_) {
        if (elem->id() == id) return elem.get();
    }
    return nullptr;
}

TextElement* OverlayConfig::elementAt(usize index) {
    return index < elements_.size() ? elements_[index].get() : nullptr;
}

const TextElement* OverlayConfig::elementAt(usize index) const {
    return index < elements_.size() ? elements_[index].get() : nullptr;
}

void OverlayConfig::createDefaultWatermark() {
    auto* elem = addElement();
    elem->setId("watermark");
    elem->setText("ChadVis Player");
    elem->setPosition(0.02f, 0.95f);
    elem->setAnchor(TextAnchor::BottomLeft);
    
    TextStyle style;
    style.fontFamily = "Liberation Sans";
    style.fontSize = 24;
    style.color = QColor(255, 255, 255);
    style.opacity = 0.7f;
    style.shadow = true;
    elem->setStyle(style);
}

void OverlayConfig::createNowPlayingElement() {
    auto* elem = addElement();
    elem->setId("now_playing");
    elem->setTextTemplate("{artist} - {title}");
    elem->setPosition(0.5f, 0.05f);
    elem->setAnchor(TextAnchor::TopCenter);
    
    TextStyle style;
    style.fontFamily = "Liberation Sans";
    style.fontSize = 36;
    style.color = QColor(0, 255, 136);  // ChadVis green
    style.opacity = 0.9f;
    style.shadow = true;
    elem->setStyle(style);
    
    AnimationParams anim;
    anim.type = AnimationType::FadePulse;
    anim.speed = 0.5f;
    elem->setAnimation(anim);
}

std::string OverlayConfig::generateId() {
    return "element_" + std::to_string(nextId_++);
}

} // namespace vc