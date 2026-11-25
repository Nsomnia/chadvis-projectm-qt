#include "LineageScorer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

#include "LineageText.hpp"

namespace vc::suno::lineage {

namespace {

constexpr double kAgeHorizonDays = 730.0;

double round2(double value) {
    return static_cast<double>(static_cast<long long>(value * 100.0 + 0.5)) / 100.0;
}

void appendComponent(std::string& out, const char* label, double value) {
    if (value <= 0.0) return;
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%s%s %.2f", out.empty() ? "" : " · ", label, value);
    out += buffer;
}

} // namespace

Relation relationHint(const Fingerprint& child) {
    std::string haystack = text::normalize(child.title);
    haystack += ' ';
    haystack += text::normalize(child.tagsRaw);
    // Whole-word match on the normalized text, because "covered in dust" is not a
    // cover and "remixology" is not a remix.
    const auto hasWord = [&haystack](std::string_view word) {
        std::size_t pos = 0;
        while ((pos = haystack.find(word, pos)) != std::string::npos) {
            const bool startOk = pos == 0 || haystack[pos - 1] == ' ';
            const std::size_t end = pos + word.size();
            const bool endOk = end >= haystack.size() || haystack[end] == ' ';
            if (startOk && endOk) return true;
            pos = end;
        }
        return false;
    };
    if (hasWord("mashup")) return Relation::Mashup;
    if (hasWord("cover")) return Relation::Cover;
    if (hasWord("remix")) return Relation::Remix;
    if (hasWord("extended") || hasWord("extend")) return Relation::Extend;
    if (hasWord("stem") || hasWord("acapella") || hasWord("acappella")) return Relation::Stem;
    return Relation::Unknown;
}

void olderFirst(const Fingerprint& a, const Fingerprint& b, const Fingerprint** parent,
                const Fingerprint** child) {
    // Unknown timestamps (0) sort last so a clip with a real date is preferred as
    // the parent. The id tie-break keeps the result stable across rebuilds.
    const auto better = [](const Fingerprint& lhs, const Fingerprint& rhs) {
        if (lhs.createdAt != rhs.createdAt) {
            if (lhs.createdAt == 0) return false;
            if (rhs.createdAt == 0) return true;
            return lhs.createdAt < rhs.createdAt;
        }
        return lhs.id < rhs.id;
    };
    if (better(a, b)) {
        *parent = &a;
        *child = &b;
    } else {
        *parent = &b;
        *child = &a;
    }
}

Score score(const Fingerprint& parent, const Fingerprint& child, const Weights& weights) {
    const Weights w = normalized(weights);
    Score result;
    if (!parent.valid || !child.valid || parent.id == child.id) return result;

    result.title = round2(text::jaccard(parent.titleTokens, child.titleTokens));
    // Containment is a real signal the token comparison understates: "Midnight
    // Drive" and "Midnight Drive (Cover)" share the whole name, and a cover that
    // APPENDS to the title scores lower than it should on tokens alone.
    if (!parent.normalizedTitle.empty() && !child.normalizedTitle.empty() &&
        (parent.normalizedTitle.find(child.normalizedTitle) != std::string::npos ||
         child.normalizedTitle.find(parent.normalizedTitle) != std::string::npos)) {
        result.title = std::max(result.title, 0.80);
    }
    result.style = round2(text::jaccard(parent.styleTokens, child.styleTokens));
    result.prompt = round2(text::jaccard(parent.promptTokens, child.promptTokens));

    if (parent.language == child.language) {
        // "none" on both sides means neither clip described itself. That is not
        // evidence of agreement, so it earns half the component rather than all.
        result.language = parent.language == "none" ? 0.5 : 1.0;
    }

    if (parent.modelMajor > 0 && parent.modelMajor == child.modelMajor) {
        result.model = 1.0;
    } else if (parent.modelMajor == 0 || child.modelMajor == 0) {
        result.model = 0.5;
    } else {
        const int generations = parent.modelMajor > child.modelMajor
                                        ? parent.modelMajor - child.modelMajor
                                        : child.modelMajor - parent.modelMajor;
        result.model = generations == 1 ? 0.4 : 0.1;
    }

    if (parent.createdAt == 0 || child.createdAt == 0) {
        result.age = 0.5;
    } else {
        const double days =
                static_cast<double>(child.createdAt - parent.createdAt) / 86400.0;
        result.age = std::clamp(1.0 - days / kAgeHorizonDays, 0.0, 1.0);
    }

    result.confidence = round2(w.title * result.title + w.style * result.style +
                              w.prompt * result.prompt + w.language * result.language +
                              w.model * result.model + w.age * result.age);

    const bool strongEnough =
            result.title >= weights.titleFloor || result.style >= weights.styleFloor;
    const bool instrumentationAgrees =
            !weights.instrumentalVeto || parent.instrumental == child.instrumental;
    result.eligible =
            strongEnough && instrumentationAgrees && result.confidence >= weights.threshold;
    result.relation = relationHint(child);
    return result;
}

std::string evidenceText(const Score& score) {
    std::string out;
    appendComponent(out, "title", score.title);
    appendComponent(out, "style", score.style);
    appendComponent(out, "prompt", score.prompt);
    appendComponent(out, "language", score.language);
    appendComponent(out, "model", score.model);
    appendComponent(out, "age", score.age);
    return out;
}

} // namespace vc::suno::lineage
