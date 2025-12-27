#pragma once
// OverlayConfig.hpp - Overlay system configuration
// Managing all your text elements in one place

#include "util/Types.hpp"
#include "util/Result.hpp"
#include "TextElement.hpp"
#include <vector>
#include <memory>

namespace vc {

class OverlayConfig {
public:
    OverlayConfig();
    
    // Load/save
    Result<void> load(const fs::path& path);
    Result<void> save(const fs::path& path) const;
    void loadFromAppConfig();
    void saveToAppConfig();
    
    // Element management
    TextElement* addElement();
    TextElement* addElement(const OverlayElementConfig& config);
    void removeElement(const std::string& id);
    void removeElementAt(usize index);
    void clear();
    
    // Access
    TextElement* findById(const std::string& id);
    const TextElement* findById(const std::string& id) const;
    TextElement* elementAt(usize index);
    const TextElement* elementAt(usize index) const;
    
    usize count() const { return elements_.size(); }
    bool empty() const { return elements_.empty(); }
    
    // Iteration
    auto begin() { return elements_.begin(); }
    auto end() { return elements_.end(); }
    auto begin() const { return elements_.begin(); }
    auto end() const { return elements_.end(); }
    
    // Default elements
    void createDefaultWatermark();
    void createNowPlayingElement();
    
    // Global settings
    bool enabled() const { return enabled_; }
    void setEnabled(bool e) { enabled_ = e; }
    
    const std::string& defaultFont() const { return defaultFont_; }
    void setDefaultFont(const std::string& font) { defaultFont_ = font; }
    
    u32 defaultFontSize() const { return defaultFontSize_; }
    void setDefaultFontSize(u32 size) { defaultFontSize_ = size; }
    
private:
    std::string generateId();
    
    std::vector<std::unique_ptr<TextElement>> elements_;
    bool enabled_{true};
    std::string defaultFont_{"Liberation Sans"};
    u32 defaultFontSize_{32};
    u32 nextId_{1};
};

} // namespace vc