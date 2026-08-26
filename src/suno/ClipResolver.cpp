#include "suno/ClipResolver.hpp"

#include "suno/SunoDatabase.hpp"
#include "util/Result.hpp"

namespace vc::suno {

std::optional<SunoClip> resolveClip(const std::vector<SunoClip>& libraryClips,
                                    SunoDatabase& db,
                                    const std::string& clipId) {
    for (const auto& clip : libraryClips) {
        if (clip.id == clipId) return clip;
    }

    auto clipRes = db.getClip(clipId);
    if (clipRes.isOk() && clipRes.value()) return *clipRes.value();

    return std::nullopt;
}

} // namespace vc::suno
