#pragma once
// LineageScorer.hpp - how similar two clips are, and why, in numbers.
//
// `score()` is the whole heuristic and it is a pure function: no clock, no
// database, no randomness. Given the same two fingerprints and the same weights it
// returns the same six partial scores, so a user who wonders why two songs linked
// can be shown the components that produced the answer.

#include <string>

#include "LineageFingerprint.hpp"
#include "LineageTypes.hpp"
#include "LineageWeights.hpp"

namespace vc::suno::lineage {

struct Score {
    /// Weighted mean of the six signals, in [0, 1].
    double confidence{0.0};
    double title{0.0};
    double style{0.0};
    double prompt{0.0};
    double language{0.0};
    double model{0.0};
    double age{0.0};
    /// Passes every gate: confidence >= threshold AND (title >= titleFloor OR
    /// style >= styleFloor) AND, when enabled, the instrumental veto.
    bool eligible{false};
    /// Best guess at what the relationship IS, from the child's own words.
    Relation relation{Relation::Unknown};
};

/// Score `child` against one candidate `parent`. Callers must have established
/// the direction already (see `olderFirst`): a child is never older than its
/// parent in this model, and the age component encodes that.
Score score(const Fingerprint& parent, const Fingerprint& child, const Weights& weights);

/// Order two clips as (parent, child) by creation time, breaking ties on id so the
/// graph is identical on every machine. Either may be returned as parent.
void olderFirst(const Fingerprint& a, const Fingerprint& b, const Fingerprint** parent,
                const Fingerprint** child);

/// "title 0.82 · style 0.64 · age 0.41" — the evidence string stored on the edge.
/// Empty components are omitted so the sentence stays readable.
std::string evidenceText(const Score& score);

/// Keyword reading of the child's title and style: "… (Cover)" -> Cover. Priority
/// order is mashup, cover, remix, extend, stem; a title naming two of them is
/// reported by the earlier one, and nothing is inferred from an unknown word.
Relation relationHint(const Fingerprint& child);

} // namespace vc::suno::lineage
