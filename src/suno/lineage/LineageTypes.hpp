#pragma once
// LineageTypes.hpp - the vocabulary of the song-lineage graph.
//
// A lineage edge says "this clip derives from that one". Two provenances exist and
// they are NOT interchangeable:
//
//   * Remote   - the server said so. `GET /clips/{id}/attribution` returns
//                `source_clips[]`, each entry carrying a `relationship` token.
//   * Inferred - ChadVis said so, from the clip's own text (title, style/genre
//                tags, description language, model generation and age). This is a
//                guess and the UI must present it as one.
//
// Keeping provenance on the edge rather than in a separate table is what lets one
// traversal read both, while `SunoLineageStore::upsert` refuses to let an inferred
// edge overwrite a remote one. Nothing in this header is Qt-dependent: the whole
// inference pipeline stays headless-testable.

#include <string>
#include <string_view>

namespace vc::suno::lineage {

/// How a child clip derives from its parent.
///
/// `Unknown` is a real answer, not a failure: the wire carries relationship codes
/// this tree has not seen, and the heuristic has no vocabulary for "the titles
/// look related but nothing names how". Both degrade here rather than inventing a
/// member.
enum class Relation { Unknown, Cover, Mashup, Extend, Remix, Stem, Inspiration, Persona };

inline std::string_view relationKey(Relation relation) {
    switch (relation) {
    case Relation::Cover:
        return "cover";
    case Relation::Mashup:
        return "mashup";
    case Relation::Extend:
        return "extend";
    case Relation::Remix:
        return "remix";
    case Relation::Stem:
        return "stem";
    case Relation::Inspiration:
        return "inspiration";
    case Relation::Persona:
        return "persona";
    case Relation::Unknown:
        break;
    }
    return "unknown";
}

inline Relation relationFromKey(std::string_view key) {
    if (key == "cover") return Relation::Cover;
    if (key == "mashup") return Relation::Mashup;
    if (key == "extend") return Relation::Extend;
    if (key == "remix") return Relation::Remix;
    if (key == "stem") return Relation::Stem;
    if (key == "inspiration") return Relation::Inspiration;
    if (key == "persona") return Relation::Persona;
    return Relation::Unknown;
}

/// The wire token from `source_clips[].relationship`.
///
/// Two code sets are observed in the corpus and both are accepted: the short
/// labels the song page renders (`COV` -> "Cover of", `EX` -> "Extended from")
/// and the long spellings a few captures carry. Anything else is `Unknown` on
/// purpose — a renamed code renders as "related" rather than as the wrong verb.
inline Relation relationFromWire(std::string_view code) {
    if (code == "COV" || code == "cov" || code == "cover" || code == "Cover") {
        return Relation::Cover;
    }
    if (code == "EX" || code == "ex" || code == "extend" || code == "extended") {
        return Relation::Extend;
    }
    if (code == "MU" || code == "mu" || code == "mashup" || code == "MASHUP") {
        return Relation::Mashup;
    }
    if (code == "REMIX" || code == "remix" || code == "RMX" || code == "rmx") {
        return Relation::Remix;
    }
    if (code == "STEM" || code == "stem") return Relation::Stem;
    if (code == "INSPO" || code == "inspo" || code == "inspiration") {
        return Relation::Inspiration;
    }
    if (code == "PERSONA" || code == "persona") return Relation::Persona;
    return Relation::Unknown;
}

enum class Provenance { Remote, Inferred };

inline std::string_view provenanceKey(Provenance provenance) {
    return provenance == Provenance::Remote ? "remote" : "inferred";
}

inline Provenance provenanceFromKey(std::string_view key) {
    // Unknown spellings are treated as INFERRED. That is the fail-closed direction:
    // a mislabelled row can then still be replaced by a rebuild, whereas calling an
    // inferred row "remote" would make it permanent.
    return key == "remote" ? Provenance::Remote : Provenance::Inferred;
}

/// One directed relationship: `childId` derives from `parentId`.
struct Edge {
    std::string childId;
    std::string parentId;
    Relation relation{Relation::Unknown};
    Provenance provenance{Provenance::Inferred};
    double confidence{0.0};
    /// Why this edge exists, in the user's words ("title 0.82 · style 0.64" or
    /// "server attribution"). Empty is legal and renders as nothing.
    std::string evidence;

    friend bool operator==(const Edge&, const Edge&) = default;
};

} // namespace vc::suno::lineage
