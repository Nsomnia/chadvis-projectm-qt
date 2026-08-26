#pragma once
// Purpose: Resolve a SunoClip by id from the in-memory library cache, falling
//          back to the persistent SunoDatabase.
// This class does NOT perform network requests or mutate either store.

#include <optional>
#include <string>
#include <vector>

#include "suno/SunoModels.hpp"

namespace vc::suno {

class SunoDatabase;

/// Search accumulated library clips first (fresh data), then the database
/// (persistent). Returns nullopt when the clip is known to neither.
std::optional<SunoClip> resolveClip(const std::vector<SunoClip>& libraryClips,
                                    SunoDatabase& db,
                                    const std::string& clipId);

} // namespace vc::suno
