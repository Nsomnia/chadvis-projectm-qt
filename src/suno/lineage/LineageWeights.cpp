#include "LineageWeights.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace vc::suno::lineage {

namespace {

constexpr std::array<std::pair<const char*, double Weights::*>, 6> kSignalFields{{
        {"title", &Weights::title},
        {"style", &Weights::style},
        {"prompt", &Weights::prompt},
        {"language", &Weights::language},
        {"model", &Weights::model},
        {"age", &Weights::age},
}};

double clamp01(double value) {
    if (!std::isfinite(value)) return 0.0;
    return std::clamp(value, 0.0, 1.0);
}

} // namespace

std::expected<void, std::string> validate(const Weights& weights) {
    for (const auto& entry : kSignalFields) {
        const double value = weights.*(entry.second);
        if (!std::isfinite(value) || value < 0.0) {
            return std::unexpected(std::string("weight '") + entry.first +
                                   "' must be zero or greater");
        }
    }
    double sum = 0.0;
    for (const auto& entry : kSignalFields) {
        sum += weights.*(entry.second);
    }
    if (sum <= 0.0) {
        return std::unexpected(std::string("at least one signal weight must be greater than zero"));
    }
    if (!std::isfinite(weights.threshold) || weights.threshold < 0.0 || weights.threshold > 1.0) {
        return std::unexpected(std::string("threshold must be between 0 and 1"));
    }
    if (weights.titleFloor < 0.0 || weights.titleFloor > 1.0 ||
        weights.styleFloor < 0.0 || weights.styleFloor > 1.0) {
        return std::unexpected(std::string("the title and style gates must be between 0 and 1"));
    }
    if (weights.edgesPerClip < 1 || weights.edgesPerClip > 32) {
        return std::unexpected(std::string("edges per clip must be between 1 and 32"));
    }
    if (weights.ancestorDepth < 1 || weights.ancestorDepth > 12 ||
        weights.descendantDepth < 1 || weights.descendantDepth > 12) {
        return std::unexpected(std::string("graph depth must be between 1 and 12"));
    }
    return {};
}

QStringList sanitize(Weights& weights) {
    QStringList repaired;
    double sum = 0.0;
    for (const auto& entry : kSignalFields) {
        double& value = weights.*(entry.second);
        const double clamped = clamp01(value);
        if (!std::isfinite(value) || clamped != value) {
            value = clamped;
            repaired.append(QString::fromLatin1(entry.first));
        }
        sum += value;
    }
    if (sum <= 0.0) {
        // Every signal off is not a preference, it is a broken file. Restore the
        // weight defaults rather than silently linking nothing.
        const Weights defaults;
        for (const auto& entry : kSignalFields) {
            weights.*(entry.second) = defaults.*(entry.second);
            repaired.append(QString::fromLatin1(entry.first));
        }
    }
    if (!std::isfinite(weights.threshold) || weights.threshold < 0.0 || weights.threshold > 1.0) {
        weights.threshold = std::clamp(std::isfinite(weights.threshold) ? weights.threshold : 0.55,
                                       0.0, 1.0);
        repaired.append(QStringLiteral("threshold"));
    }
    weights.titleFloor = clamp01(weights.titleFloor);
    weights.styleFloor = clamp01(weights.styleFloor);
    weights.edgesPerClip = std::clamp(weights.edgesPerClip, 1, 32);
    weights.ancestorDepth = std::clamp(weights.ancestorDepth, 1, 12);
    weights.descendantDepth = std::clamp(weights.descendantDepth, 1, 12);
    return repaired;
}

Weights normalized(const Weights& weights) {
    Weights out = weights;
    double sum = 0.0;
    for (const auto& entry : kSignalFields) {
        sum += out.*(entry.second);
    }
    if (sum <= 0.0) return out;
    for (const auto& entry : kSignalFields) {
        out.*(entry.second) /= sum;
    }
    return out;
}

QVariantMap toVariantMap(const Weights& weights) {
    QVariantMap map;
    for (const auto& entry : kSignalFields) {
        map.insert(QString::fromLatin1(entry.first), weights.*(entry.second));
    }
    map.insert(QStringLiteral("threshold"), weights.threshold);
    map.insert(QStringLiteral("titleFloor"), weights.titleFloor);
    map.insert(QStringLiteral("styleFloor"), weights.styleFloor);
    map.insert(QStringLiteral("instrumentalVeto"), weights.instrumentalVeto);
    map.insert(QStringLiteral("edgesPerClip"), weights.edgesPerClip);
    map.insert(QStringLiteral("ancestorDepth"), weights.ancestorDepth);
    map.insert(QStringLiteral("descendantDepth"), weights.descendantDepth);
    return map;
}

Weights fromVariantMap(const QVariantMap& values) {
    Weights weights;
    const auto readDouble = [&values](const QString& key, double& target) {
        const auto it = values.constFind(key);
        if (it == values.constEnd() || !it->isValid()) return;
        bool ok = false;
        const double parsed = it->toDouble(&ok);
        if (ok) target = parsed;
    };
    const auto readInt = [&values](const QString& key, int& target) {
        const auto it = values.constFind(key);
        if (it == values.constEnd() || !it->isValid()) return;
        bool ok = false;
        const int parsed = it->toInt(&ok);
        if (ok) target = parsed;
    };
    for (const auto& entry : kSignalFields) {
        readDouble(QString::fromLatin1(entry.first), weights.*(entry.second));
    }
    readDouble(QStringLiteral("threshold"), weights.threshold);
    readDouble(QStringLiteral("titleFloor"), weights.titleFloor);
    readDouble(QStringLiteral("styleFloor"), weights.styleFloor);
    readInt(QStringLiteral("edgesPerClip"), weights.edgesPerClip);
    readInt(QStringLiteral("ancestorDepth"), weights.ancestorDepth);
    readInt(QStringLiteral("descendantDepth"), weights.descendantDepth);
    if (values.contains(QStringLiteral("instrumentalVeto"))) {
        weights.instrumentalVeto = values.value(QStringLiteral("instrumentalVeto")).toBool();
    }
    return weights;
}

} // namespace vc::suno::lineage
