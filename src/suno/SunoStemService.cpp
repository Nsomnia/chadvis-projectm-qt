#include "SunoStemService.hpp"

#include "SunoClient.hpp"
#include "SunoEndpoints.hpp"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>

namespace vc::suno {

SunoStemService::SunoStemService(SunoClient* client, QObject* parent)
    : QObject(parent), client_(client) {}

SunoStemService::Status SunoStemService::parseStatus(const QString& text)
{
    if (text == QStringLiteral("submitted")) {
        return Status::Submitted;
    }
    if (text == QStringLiteral("complete")) {
        return Status::Complete;
    }
    return Status::Unknown;
}

void SunoStemService::submit(const QString& clipId, double vocalStartS, double vocalEndS)
{
    if (vocalStartS < 0.0 || vocalEndS < 0.0 || vocalStartS >= vocalEndS) {
        fail(Operation::Submit,
             QStringLiteral("Invalid vocal range: start must be non-negative and less than end."));
        return;
    }
    const QByteArray body = QJsonDocument(QJsonObject{{QStringLiteral("vocal_start_s"), vocalStartS},
                                                        {QStringLiteral("vocal_end_s"), vocalEndS}})
                                    .toJson(QJsonDocument::Compact);
    const QString path = QString(qstr(endpoints::CLIP_VOX_STEM)).replace(QStringLiteral("{}"), clipId);
    enqueue(Operation::Submit, path, "POST", body);
}

void SunoStemService::poll(const QString& processedClipId)
{
    const QString path =
            QString(qstr(endpoints::PROCESSED_CLIP)).replace(QStringLiteral("{}"), processedClipId);
    enqueue(Operation::Poll, path, "GET", {});
}

void SunoStemService::enqueue(Operation operation, const QString& path, const QString& method,
                              const QByteArray& data)
{
    if (!client_ || !client_->isAuthenticated()) {
        fail(operation, QStringLiteral("Sign in to Suno first."));
        return;
    }
    if (operation == Operation::Submit) {
        submitting_ = true;
        ++submitGeneration_;
    } else {
        polling_ = true;
        ++pollGeneration_;
    }
    const quint64 generation =
            operation == Operation::Submit ? submitGeneration_ : pollGeneration_;
    emit changed();

    QPointer<SunoStemService> guard(this);
    client_->enqueueAuthenticatedRequest(path, method.toStdString(), data,
                                         [guard, operation, generation](QNetworkReply* reply) {
                                             if (!guard) {
                                                 reply->deleteLater();
                                                 return;
                                             }
                                             guard->handleReply(reply, operation, generation);
                                         });
}

void SunoStemService::handleReply(QNetworkReply* reply, Operation operation, quint64 generation)
{
    const quint64 current = operation == Operation::Submit ? submitGeneration_ : pollGeneration_;
    // An answer from an abandoned call of the same operation is dropped, not
    // merged: a late reply must not overwrite the newer call's section. The
    // other operation carries its own generation, so it is unaffected.
    if (generation != current || !client_) {
        reply->deleteLater();
        return;
    }
    const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    auto body = client_->readTrackedBody(reply);
    reply->deleteLater();

    if (!body) {
        fail(operation, body.error());
        return;
    }
    if (status < 200 || status >= 300) {
        fail(operation, QStringLiteral("Request failed (HTTP %1).").arg(status));
        return;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(*body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        fail(operation, QStringLiteral("Unreadable response body."));
        return;
    }

    failureReason_.clear();
    const QJsonObject object = document.object();
    if (operation == Operation::Submit) {
        submitting_ = false;
        submitState_.processedClipId = object.value(QStringLiteral("id")).toString();
        submitState_.statusText = object.value(QStringLiteral("status")).toString();
        submitState_.status = parseStatus(submitState_.statusText);
        submitState_.raw = object;
    } else {
        polling_ = false;
        pollState_.statusText = object.value(QStringLiteral("status")).toString();
        pollState_.status = parseStatus(pollState_.statusText);
        pollState_.vocalAudioUrl = object.value(QStringLiteral("vocal_audio_url")).toString();
        pollState_.raw = object;
    }
    emit changed();
}

void SunoStemService::fail(Operation operation, const QString& message)
{
    if (operation == Operation::Submit) {
        submitting_ = false;
        submitState_ = {};
    } else {
        polling_ = false;
        pollState_ = {};
    }
    failureReason_ = message;
    emit failed(message);
}

} // namespace vc::suno
