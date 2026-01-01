#pragma once
#include <map>
#include <string>
#include "util/Result.hpp"
#include "util/Types.hpp"

namespace vc {

class RatingManager {
public:
    static RatingManager& instance();

    void setRating(const std::string& presetName, int stars);
    int getRating(const std::string& presetName) const;

    Result<void> load();
    Result<void> save();

private:
    RatingManager() = default;
    std::map<std::string, int> ratings_;
};

} // namespace vc
