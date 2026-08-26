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
#include <QString>
#include <QList>
#include <expected>

namespace vc::suno {

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
