#include "Config.hpp"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include "ConfigLoader.hpp"

namespace vc {

Config& Config::instance() {
    static Config instance;
    return instance;
}

void Config::assertAffinity() const {
#ifndef NDEBUG
    if (std::this_thread::get_id() != owner_) {
        // Not LOG_*: this can fire before Logger::init has picked its sinks,
        // and the message must survive that. assert() aborts under a debugger
        // with the offending thread id in the message.
        std::fprintf(stderr,
                     "Config touched from a non-owning thread — the T0022 "
                     "contract is main-thread-affine mutation; copy what you "
                     "need on the main thread instead.\n");
        assert(false && "Config mutated off the owning thread");
    }
#endif
}

Result<void> Config::load(const fs::path& path) {
    assertAffinity();
    std::lock_guard lock(mutex_);
    configPath_ = path;
    return ConfigLoader::load(*this, path);
}

Result<void> Config::loadDefault() {
    assertAffinity();
    std::lock_guard lock(mutex_);
    return ConfigLoader::loadDefault(*this);
}

Result<void> Config::save(const fs::path& path) const {
    assertAffinity();
    std::lock_guard lock(mutex_);
    auto result = ConfigLoader::save(*this, path);
    // Clear the dirty flag ONLY on success. ConfigLoader::save is atomic
    // (temp -> completeness check -> rename), so a successful call means the
    // bytes are durable; leaving the flag set would rewrite the file on every
    // quit regardless of change (T0023).
    if (result.isOk()) {
        markClean();
    }
    return result;
}

void Config::addOverlayElement(OverlayElementConfig elem) {
    assertAffinity();
    std::lock_guard lock(mutex_);
    overlay_.elements.push_back(std::move(elem));
    markDirty();
}

void Config::removeOverlayElement(const std::string& id) {
    assertAffinity();
    std::lock_guard lock(mutex_);
    std::erase_if(overlay_.elements,
                  [&id](const auto& e) { return e.id == id; });
    markDirty();
}

OverlayElementConfig* Config::findOverlayElement(const std::string& id) {
    assertAffinity();
    for (auto& elem : overlay_.elements) {
        if (elem.id == id)
            return &elem;
    }
    return nullptr;
}

} // namespace vc
