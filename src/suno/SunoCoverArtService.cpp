#include "SunoCoverArtService.hpp"

#include "SunoClient.hpp"
#include "SunoEndpoints.hpp"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>

namespace vc::suno {

namespace {

/// Status, tracked body, parsed object: the same read every service needs.
/// A body that did not arrive, a non-2xx, and unreadable JSON all become one
/// reason, so the caller has a single failure path.
std::expected<QJsonObject, QString> readObject(SunoClient& client, QNetworkReply* reply,
                                               const QString& label)
{
    const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    auto body = client.readTrackedBody(reply);
    if (!body) {
        return std::unexpected(body.error());
    }
    if (status < 200 || status >= 300) {
        return std::unexpected(
                QStringLiteral("Couldn't generate %1 (HTTP %2).").arg(label).arg(status));
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(*body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return std::unexpected(
                QStringLiteral("Couldn't generate %1: unreadable response body.").arg(label));
    }
    return document.object();
}

} // namespace

SunoCoverArtService::SunoCoverArtService(SunoClient* client, QObject* parent)
    : QObject(parent), client_(client) {}

QJsonObject SunoCoverArtService::requestBody(const QString& prompt)
{
    return QJsonObject{{QStringLiteral("prompt"), prompt}};
}

std::expected<SunoCoverArtService::Image, QString>
SunoCoverArtService::parseImage(const QJsonObject& root)
{
    const QString s3Id = root.value(QStringLiteral("image_s3_id")).toString();
    if (s3Id.isEmpty()) {
        return std::unexpected(
                QStringLiteral("Couldn't generate cover art: no image id in the response."));
    }
    Image image;
    image.imageUrl = root.value(QStringLiteral("image_url")).toString();
    image.imageS3Id = s3Id;
    image.uploadId = root.value(QStringLiteral("upload_id")).toString();
    return image;
}

void SunoCoverArtService::generate(const QString& prompt)
{
    const QString trimmed = prompt.trimmed();
    if (trimmed.isEmpty()) {
        fail(QStringLiteral("Enter a prompt to generate cover art."));
        return;
    }
    if (!client_ || !client_->isAuthenticated()) {
        fail(QStringLiteral("Sign in to Suno first."));
        return;
    }

    // A new prompt abandons the previous answer: the image is the answer to one
    // prompt, so keeping the old one while the new one renders would show the
    // wrong cover.
    image_ = {};
    failureReason_.clear();
    loading_ = true;
    ++generation_;
    emit imageChanged();

    const quint64 generation = generation_;
    QPointer<SunoCoverArtService> guard(this);
    client_->enqueueAuthenticatedRequest(
            qstr(endpoints::GEN_PROMPT_IMAGE), "POST",
            QJsonDocument(requestBody(trimmed)).toJson(QJsonDocument::Compact),
            [guard, generation](QNetworkReply* reply) {
                if (!guard) {
                    reply->deleteLater();
                    return;
                }
                guard->handleReply(reply, generation);
            });
}

void SunoCoverArtService::handleReply(QNetworkReply* reply, quint64 generation)
{
    // A reply from an abandoned generation is dropped, not merged: a late answer
    // would overwrite the image for the prompt the user is waiting on.
    if (generation != generation_ || !client_) {
        reply->deleteLater();
        return;
    }
    const auto root = readObject(*client_, reply, QStringLiteral("cover art"));
    reply->deleteLater();

    loading_ = false;
    if (!root) {
        fail(root.error());
        return;
    }
    auto parsed = parseImage(*root);
    if (!parsed) {
        fail(parsed.error());
        return;
    }
    image_ = *parsed;
    emit imageChanged();
}

void SunoCoverArtService::fail(const QString& message)
{
    // Bumping the generation also abandons any request still in flight: a local
    // refusal (empty prompt, signed out) must not be followed by an older reply
    // filling the state.
    ++generation_;
    loading_ = false;
    image_ = {};
    failureReason_ = message;
    emit imageChanged();
    emit failed(message);
}

} // namespace vc::suno
