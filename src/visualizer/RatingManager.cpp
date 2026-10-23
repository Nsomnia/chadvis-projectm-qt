#include "RatingManager.hpp"
#include <toml++/toml.h>
#include <sstream>
#include "core/Logger.hpp"
#include "util/FileUtils.hpp"

namespace vc {

RatingManager& RatingManager::instance() {
    static RatingManager instance;
    return instance;
}

void RatingManager::setRating(const std::string& name, int stars) {
    ratings_[name] = std::clamp(stars, 0, 5);
}

int RatingManager::getRating(const std::string& name) const {
    auto it = ratings_.find(name);
    return (it != ratings_.end()) ? it->second : 0;
}

Result<void> RatingManager::load() {
    auto path = file::configDir() / "ratings.toml";
    if (!fs::exists(path))
        return Result<void>::ok();

    try {
        auto tbl = toml::parse_file(path.string());
        for (auto&& [key, value] : tbl) {
            if (auto stars = value.as_integer()) {
                ratings_[std::string(key.str())] =
                        static_cast<int>(stars->get());
            }
        }
        LOG_INFO("Loaded {} preset ratings", ratings_.size());
    } catch (const toml::parse_error& e) {
        return Result<void>::err("Failed to parse ratings: " +
                                 std::string(e.description()));
    }
    return Result<void>::ok();
}

Result<void> RatingManager::save() {
    auto path = file::configDir() / "ratings.toml";
    toml::table tbl;
    for (auto const& [name, stars] : ratings_) {
        tbl.insert(name, stars);
    }

    // Serialize first, then let the shared writer own the temp+rename: a
    // truncated write here loses every rating, and save() runs on every
    // favourite toggle rather than once per session (T0059).
    std::ostringstream out;
    out << tbl;
    return file::writeText(path, out.str());
}

} // namespace vc
