#include "SunoJson.hpp"

#include <QJsonDocument>
#include <QJsonParseError>

namespace vc::suno {

std::expected<QJsonObject, QString>
parseJsonObject(const QByteArray& body, QString errorContext) {
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return std::unexpected(QStringLiteral("%1: malformed JSON (%2)")
                                   .arg(errorContext, parseError.errorString()));
    }
    if (!document.isObject()) {
        return std::unexpected(
                QStringLiteral("%1: expected a JSON object").arg(errorContext));
    }
    return document.object();
}

} // namespace vc::suno
