#pragma once
// SunoJson.hpp - one parser for untrusted Suno JSON response bodies.

#include <QByteArray>
#include <QJsonObject>
#include <QString>

#include <expected>

namespace vc::suno {

/// Parse a response body into a JSON object, refusing both malformed JSON and
/// syntactically valid non-object bodies (array/string/number/bool/null).
/// QJsonDocument::object() silently turns the latter into an empty object,
/// which reads as "valid response, no data" and misreports the real fault.
/// errorContext is prefixed to the returned reason for logging.
[[nodiscard]] std::expected<QJsonObject, QString>
parseJsonObject(const QByteArray& body, QString errorContext);

} // namespace vc::suno
