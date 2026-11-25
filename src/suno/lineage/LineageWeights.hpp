#pragma once
// LineageWeights.hpp - the user's dials on the lineage heuristic.
//
// Every number the inference uses lives here, so "why did these two songs link?"
// has one answer and one place to change it. The struct is plain data and the
// validation is separate from the parsing (`std::expected`, no throwing), because
// a weight comes from a GUI slider and must never be able to corrupt a rebuild.

#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <array>
#include <expected>
#include <string>

namespace vc::suno::lineage {

struct Weights {
    // ── Signals, summed after normalisation ──────────────────────────────────
    /// Shared words in the two titles. The strongest single signal in the corpus:
    /// covers, extends and remixes keep the name.
    double title{0.35};
    /// Shared style/genre tags ("synthwave, dreamy, female vocals"). Second
    /// strongest, and the one that catches a renamed cover.
    double style{0.30};
    /// Shared vocabulary in the free-form description.
    double prompt{0.15};
    /// Same writing system. Small on purpose: it separates a Japanese-language
    /// pair from an English one, it does not identify a language.
    double language{0.05};
    /// Model generation. A v3 clip may be the parent of a v4 cover; the signal is
    /// direction-free and only says "these came from the same era of the model".
    double model{0.05};
    /// Creation proximity, capped at two years.
    double age{0.10};

    // ── Gates and limits ─────────────────────────────────────────────────────
    /// Minimum blended confidence for an inferred edge to be stored.
    double threshold{0.55};
    /// An edge also needs at least ONE strong signal: title at or above this, or
    /// style at or above `styleFloor`. Without this gate two unrelated songs that
    /// share a generic style tag and a common word become related.
    double titleFloor{0.45};
    double styleFloor{0.60};
    /// Refuse to link an instrumental to a vocal track. Off, for the user who
    /// deliberately works in instrumental-cover lineage.
    bool instrumentalVeto{true};
    /// How many parents a single clip may be given by one rebuild (best first).
    int edgesPerClip{4};
    /// Graph traversal limits used by the view.
    int ancestorDepth{4};
    int descendantDepth{4};
};

/// Validate a candidate set. Returns a sentence naming the first problem; every
/// rule here is one a slider can actually break.
std::expected<void, std::string> validate(const Weights& weights);

/// `validate` plus repair: out-of-range values are clamped and, if every signal
/// weight is zero, the defaults for the signals are restored. Used when reading
/// persisted values, where refusing to start would be worse than clamping.
/// Returns the names of the fields it had to change (empty when clean).
QStringList sanitize(Weights& weights);

/// Signals normalised so they sum to 1.0. If they sum to zero the defaults are
/// used, so the score is always a weighted mean in [0, 1].
Weights normalized(const Weights& weights);

QVariantMap toVariantMap(const Weights& weights);

/// Parse the six signal weights and the gates from `values`. Unknown keys are
/// ignored; absent keys keep their default. Returns the parsed struct, never an
/// error — use `validate`/`sanitize` on the result.
Weights fromVariantMap(const QVariantMap& values);

} // namespace vc::suno::lineage
