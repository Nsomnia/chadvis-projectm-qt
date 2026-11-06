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

void RatingManager::setRating(const std::string& path, int stars) {
    std::lock_guard<std::mutex> lock(mutex_);
    ratings_[path] = std::clamp(stars, 0, 5);
}

int RatingManager::getRating(const std::string& path) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = ratings_.find(path);
    return (it != ratings_.end()) ? it->second : 0;
}

Result<void> RatingManager::load() {
    auto path = file::configDir() / "ratings.toml";
    if (!fs::exists(path))
        return Result<void>::ok();

    // Parse into a local table first: a parse error must leave the live ratings
    // untouched, and the file walk stays outside the lock either way.
    std::map<std::string, int> parsed;
    try {
        auto tbl = toml::parse_file(path.string());
        for (auto&& [key, value] : tbl) {
            if (auto stars = value.as_integer()) {
                parsed[std::string(key.str())] = static_cast<int>(stars->get());
            }
        }
    } catch (const toml::parse_error& e) {
        return Result<void>::err("Failed to parse ratings: " +
                                 std::string(e.description()));
    }

    std::size_t total = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [key, stars] : parsed)
            ratings_[key] = stars;
        total = ratings_.size();
    }
    LOG_INFO("Loaded {} preset ratings", total);
    return Result<void>::ok();
}

Result<void> RatingManager::save() {
    auto path = file::configDir() / "ratings.toml";
    toml::table tbl;
    {
        // Snapshot under the lock, serialize and write outside it: save() runs on
        // every rating click, and the disk write must not block a reader.
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto const& [key, stars] : ratings_) {
            tbl.insert(key, stars);
        }
    }

    // Serialize first, then let the shared writer own the temp+rename: a
    // truncated write here loses every rating, and save() runs on every
    // favourite toggle rather than once per session (T0059).
    std::ostringstream out;
    out << tbl;
    return file::writeText(path, out.str());
}

} // namespace vc
