#pragma once
// ClipParser.hpp - single canonical parser for captured Suno clip/feed JSON.
//
// Tolerance contract (Lane C ground rules):
//   - missing fields        -> struct defaults
//   - wrong-typed fields    -> field skipped, never a crash
//   - unknown fields        -> ignored liberally
//   - clip without an id    -> parse error (cannot be deduped downstream)

#include "SunoModels.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QList>
#include <cmath>
#include <cstddef>
#include <expected>
#include <optional>
#include <vector>

namespace vc::suno {

/// One tolerance contract for every tolerant JSON read in this module.
///
/// The four services each grew their own copy and two of them DISAGREED:
/// `optBool` accepted `"True"`/`"False"` (the captured feed-filter spelling)
/// while the notification parser's `booleanValue` accepted only `"true"`/`"1"`,
/// so the same field could be true in one code path and false in another.
/// These helpers are the single answer. They are liberal on input — the
/// capture is the authority, not the JSON spec — and total: nothing throws.
namespace json {

/// String (or number/bool) rendered as text; missing/array/object -> "".
/// Integral numbers lose the `.0`, which is what wire ids look like when a
/// server sends them as numbers.
[[nodiscard]] inline QString stringValue(const QJsonValue& value) {
    if (value.isString()) {
        return value.toString();
    }
    if (value.isDouble()) {
        const double number = value.toDouble();
        if (std::floor(number) == number) {
            return QString::number(static_cast<qint64>(number));
        }
        return QString::number(number, 'g', 15);
    }
    if (value.isBool()) {
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    }
    return {};
}

/// Integer from a JSON number or a numeric string; else nullopt.
[[nodiscard]] inline std::optional<qint64> integerValue(const QJsonValue& value) {
    if (value.isDouble()) {
        return static_cast<qint64>(value.toDouble());
    }
    if (value.isString()) {
        bool ok = false;
        const qint64 parsed = value.toString().toLongLong(&ok);
        if (ok) {
            return parsed;
        }
    }
    return std::nullopt;
}

/// Double from a JSON number or a numeric string; else `fallback`.
[[nodiscard]] inline double doubleValue(const QJsonValue& value, double fallback = 0.0) {
    if (value.isDouble()) {
        return value.toDouble();
    }
    if (value.isString()) {
        bool ok = false;
        const double parsed = value.toString().toDouble(&ok);
        if (ok) {
            return parsed;
        }
    }
    return fallback;
}

/// Bool from a real bool or a string that is exactly "true"/"false" in any
/// case; else `fallback`. Deliberately NOT numeric: `is_download_unlocked`
/// reads through here, and a wire change from bool to 1/0 must fail closed
/// (locked) rather than silently unlock every download. Whitespace is not
/// trimmed for the same reason — `" true"` is unreadable, not true.
[[nodiscard]] inline bool booleanValue(const QJsonValue& value, bool fallback = false) {
    if (value.isBool()) {
        return value.toBool();
    }
    if (value.isString()) {
        const QString spelling = value.toString();
        if (spelling.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0) {
            return true;
        }
        if (spelling.compare(QStringLiteral("false"), Qt::CaseInsensitive) == 0) {
            return false;
        }
    }
    return fallback;
}

/// String items of an array, skipping non-strings. Non-array -> empty.
[[nodiscard]] inline std::vector<std::string> stringList(const QJsonValue& value) {
    std::vector<std::string> out;
    if (!value.isArray()) {
        return out;
    }
    const QJsonArray array = value.toArray();
    out.reserve(static_cast<std::size_t>(array.size()));
    for (const QJsonValue& item : array) {
        if (item.isString()) {
            out.push_back(item.toString().toStdString());
        }
    }
    return out;
}

} // namespace json

class ClipParser {
public:
    /// One /api/feed/v3 envelope page.
    struct FeedPage {
        QList<SunoClip> clips;
        QString nextCursor;   ///< Empty when ABSENT (= feed exhausted).
        bool hasMore{false};
    };

    /// Parse one clip object from the captured schema superset.
    [[nodiscard]] static std::expected<SunoClip, QString> parseClip(const QJsonObject& obj);

    /// Parse a clips[] array; per-item errors are logged and skipped, so a
    /// single malformed entry never poisons the page. Errors only when the
    /// argument itself is not an array context (kept for symmetry).
    [[nodiscard]] static std::expected<QList<SunoClip>, QString>
    parseClipArray(const QJsonArray& arr);

    /// Parse the full feed/v3 response envelope (clips + cursor state).
    [[nodiscard]] static std::expected<FeedPage, QString>
    parseFeedEnvelope(const QJsonObject& root);

    [[nodiscard]] static std::optional<QString>
    selectImageUrl(const QString& primary, const QString& fallback = {});

    // ── Shared tolerant accessors (also used by SunoAccountManager) ──────

    /// String value; non-string or missing -> default.
    [[nodiscard]] static QString
    optString(const QJsonObject& obj, const QString& key, const QString& def = {});

    /// Integer value; accepts JSON ints and numeric strings, else default.
    [[nodiscard]] static qint64 optInt(const QJsonObject& obj, const QString& key,
                                       qint64 def = 0);

    /// Bool value; accepts real bools and "True"/"False" strings, else default.
    [[nodiscard]] static bool optBool(const QJsonObject& obj, const QString& key,
                                      bool def = false);

    /// Double value; accepts numbers and numeric strings, else default.
    [[nodiscard]] static double optDouble(const QJsonObject& obj, const QString& key,
                                          double def = 0.0);
};

} // namespace vc::suno
