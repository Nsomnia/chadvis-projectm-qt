#pragma once
// LineageInference.hpp - from a library of clips to a set of candidate edges.
//
// The naive shape of this problem is O(n^2): compare every clip with every other.
// On the user's own reported 6752-clip library that is 22.8 million pairs, each a
// handful of set intersections. So candidates are BLOCKED first: two clips are
// only compared when they already share at least two index tokens (one title word
// and one style word, or two title words). Generic buckets ("pop", "love") are
// dropped rather than compared, which is what keeps a library with a thousand
// synthwave tracks from becoming a thousand-way clique.
//
// Everything here is pure and deterministic: same clips, same weights, same edges,
// in the same order. That is what makes the result reviewable and testable.

#include <cstddef>
#include <vector>

#include "LineageFingerprint.hpp"
#include "LineageTypes.hpp"
#include "LineageWeights.hpp"

namespace vc::suno::lineage {

struct InferenceLimits {
    /// Hard ceiling on scored pairs. When reached, the remaining candidates are
    /// dropped and `pairCapHit` says so — a truncated graph is reported, never
    /// presented as a complete one.
    std::size_t pairCap{250000};
    /// A token shared by more clips than this is too generic to block on (it would
    /// pair every track in the library with every other). Buckets above the cap are
    /// skipped and counted.
    std::size_t bucketCap{160};
};

struct InferenceStats {
    std::size_t clips{0};
    std::size_t buckets{0};
    std::size_t candidates{0};
    std::size_t pairsScored{0};
    std::size_t edges{0};
    bool pairCapHit{false};
    bool bucketCapHit{false};
};

struct InferenceResult {
    std::vector<Edge> edges;
    InferenceStats stats;
};

/// Infer parent/child edges across one library snapshot.
///
/// Direction comes from creation time (`olderFirst`), the relation from the
/// child's own wording (`relationHint`), and the confidence from the weighted
/// score. Each child keeps at most `weights.edgesPerClip` parents, best first.
InferenceResult inferEdges(const std::vector<Fingerprint>& prints, const Weights& weights,
                           const InferenceLimits& limits = {});

} // namespace vc::suno::lineage
