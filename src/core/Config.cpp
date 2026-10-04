#include "Config.hpp"
#include <algorithm>
#include "ConfigLoader.hpp"

namespace vc {

Config& Config::instance() {
    static Config instance;
    return instance;
}

Result<void> Config::load(const fs::path& path) {
    std::lock_guard lock(mutex_);
    configPath_ = path;
    return ConfigLoader::load(*this, path);
}

Result<void> Config::loadDefault() {
    std::lock_guard lock(mutex_);
    return ConfigLoader::loadDefault(*this);
}

Result<void> Config::save(const fs::path& path) const {
    std::lock_guard lock(mutex_);
    return ConfigLoader::save(*this, path);
}

void Config::addOverlayElement(OverlayElementConfig elem) {
    std::lock_guard lock(mutex_);
    overlay_.elements.push_back(std::move(elem));
    markDirty();
}

void Config::removeOverlayElement(const std::string& id) {
    std::lock_guard lock(mutex_);
    std::erase_if(overlay_.elements,
                  [&id](const auto& e) { return e.id == id; });
    markDirty();
}

OverlayElementConfig* Config::findOverlayElement(const std::string& id) {
    for (auto& elem : overlay_.elements) {
        if (elem.id == id)
            return &elem;
    }
    return nullptr;
}

} // namespace vc
