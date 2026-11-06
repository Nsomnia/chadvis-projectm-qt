#pragma once
#include <map>
#include <mutex>
#include <string>
#include "util/Result.hpp"
#include "util/Types.hpp"

namespace vc {

/// Per-preset star ratings, keyed on the preset file path.
///
/// The key is the path, not the name: two banks routinely hold a preset with the
/// same file name, and a name key merges their ratings into one. Every preset a
/// caller can rate carries a path (PresetInfo::path), so callers pass that.
///
/// One process-wide instance is shared by the GUI thread (rating clicks) and any
/// worker that reads a preset list, so every map access takes mutex_.
class RatingManager {
public:
    static RatingManager& instance();

    void setRating(const std::string& presetPath, int stars);
    int getRating(const std::string& presetPath) const;

    Result<void> load();
    Result<void> save();

private:
    RatingManager() = default;

    // Mutable because getRating() is const but must still lock.
    mutable std::mutex mutex_;
    std::map<std::string, int> ratings_;
};

} // namespace vc
