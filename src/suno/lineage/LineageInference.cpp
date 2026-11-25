#include "LineageInference.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>

#include "LineageScorer.hpp"

namespace vc::suno::lineage {

namespace {

/// Unordered key for an index pair, smaller index first so (a,b) and (b,a) collide.
std::uint64_t pairKey(std::uint32_t a, std::uint32_t b) {
    if (a > b) std::swap(a, b);
    return (static_cast<std::uint64_t>(a) << 32) | static_cast<std::uint64_t>(b);
}

constexpr std::uint32_t lowIndex(std::uint64_t key) {
    return static_cast<std::uint32_t>(key & 0xffffffffu);
}

constexpr std::uint32_t highIndex(std::uint64_t key) {
    return static_cast<std::uint32_t>(key >> 32);
}

} // namespace

InferenceResult inferEdges(const std::vector<Fingerprint>& prints, const Weights& weights,
                           const InferenceLimits& limits) {
    InferenceResult result;
    result.stats.clips = prints.size();
    if (prints.size() < 2) return result;

    // ── Blocking index ──────────────────────────────────────────────────────
    // Positions, not ids: the score is computed from the fingerprint in hand, and a
    // second lookup by id would only add a map to keep honest.
    std::vector<std::uint32_t> valid;
    valid.reserve(prints.size());
    for (std::uint32_t i = 0; i < prints.size(); ++i) {
        if (prints[i].valid) valid.push_back(i);
    }
    if (valid.size() < 2) return result;

    std::unordered_map<std::string, std::vector<std::uint32_t>> index;
    index.reserve(valid.size() * 6);
    const auto addTokens = [&index](char kind, const std::vector<std::string>& tokens,
                                    std::uint32_t position) {
        std::string key(1, kind);
        for (const auto& token : tokens) {
            key.resize(1);
            key += token;
            index[key].push_back(position);
        }
    };
    for (std::uint32_t position = 0; position < valid.size(); ++position) {
        const Fingerprint& print = prints[valid[position]];
        addTokens('t', print.titleTokens, position);
        addTokens('s', print.styleTokens, position);
    }

    // Sorted keys, so a truncated run truncates in the same place every time.
    std::vector<std::string> keys;
    keys.reserve(index.size());
    for (const auto& entry : index) {
        keys.push_back(entry.first);
    }
    std::sort(keys.begin(), keys.end());
    result.stats.buckets = index.size();

    // ── Candidate pairs: sharing one generic token is not evidence ───────────
    std::unordered_map<std::uint64_t, unsigned> hits;
    hits.reserve(std::min<std::size_t>(keys.size() * 4, limits.pairCap * 2));
    const std::size_t hitCeiling = limits.pairCap * 2;
    bool stopped = false;
    for (const auto& key : keys) {
        const auto& bucket = index[key];
        if (bucket.size() < 2) continue;
        if (bucket.size() > limits.bucketCap) {
            result.stats.bucketCapHit = true;
            continue;
        }
        for (std::size_t i = 0; i < bucket.size() && !stopped; ++i) {
            for (std::size_t j = i + 1; j < bucket.size(); ++j) {
                // Counted once past the threshold: the exact count above 2 is
                // never used, only "at least two shared tokens".
                unsigned& count = hits[pairKey(bucket[i], bucket[j])];
                if (count < 2) ++count;
                if (hits.size() >= hitCeiling) {
                    result.stats.pairCapHit = true;
                    stopped = true;
                    break;
                }
            }
        }
        if (stopped) break;
    }

    std::vector<std::pair<std::uint64_t, unsigned>> candidates;
    candidates.reserve(hits.size());
    for (const auto& entry : hits) {
        if (entry.second >= 2) candidates.emplace_back(entry.first, entry.second);
    }
    std::sort(candidates.begin(), candidates.end(),
              [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });
    result.stats.candidates = candidates.size();
    if (candidates.size() > limits.pairCap) {
        candidates.resize(limits.pairCap);
        result.stats.pairCapHit = true;
    }

    // ── Score, then keep the best few parents per child ─────────────────────
    std::vector<std::vector<Edge>> perChild(valid.size());
    for (const auto& candidate : candidates) {
        const std::uint32_t first = lowIndex(candidate.first);
        const std::uint32_t second = highIndex(candidate.first);
        if (first == second || first >= valid.size() || second >= valid.size()) continue;

        const Fingerprint& a = prints[valid[first]];
        const Fingerprint& b = prints[valid[second]];
        const Fingerprint* parent = nullptr;
        const Fingerprint* child = nullptr;
        olderFirst(a, b, &parent, &child);
        if (parent == nullptr || child == nullptr) continue;

        const Score scored = score(*parent, *child, weights);
        ++result.stats.pairsScored;
        if (!scored.eligible) continue;

        Edge edge;
        edge.childId = child->id;
        edge.parentId = parent->id;
        edge.relation = scored.relation;
        edge.provenance = Provenance::Inferred;
        edge.confidence = scored.confidence;
        edge.evidence = evidenceText(scored);
        const std::uint32_t childPosition = child == &a ? first : second;
        perChild[childPosition].push_back(std::move(edge));
    }

    const std::size_t keep =
            static_cast<std::size_t>(std::clamp(weights.edgesPerClip, 1, 32));
    for (auto& edges : perChild) {
        if (edges.size() > keep) {
            std::sort(edges.begin(), edges.end(), [](const Edge& lhs, const Edge& rhs) {
                if (lhs.confidence != rhs.confidence) return lhs.confidence > rhs.confidence;
                return lhs.parentId < rhs.parentId;
            });
            edges.resize(keep);
        }
        for (auto& edge : edges) {
            result.edges.push_back(std::move(edge));
        }
    }

    std::sort(result.edges.begin(), result.edges.end(), [](const Edge& lhs, const Edge& rhs) {
        if (lhs.childId != rhs.childId) return lhs.childId < rhs.childId;
        if (lhs.confidence != rhs.confidence) return lhs.confidence > rhs.confidence;
        return lhs.parentId < rhs.parentId;
    });
    result.stats.edges = result.edges.size();
    return result;
}

} // namespace vc::suno::lineage
