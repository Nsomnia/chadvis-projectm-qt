#include "SunoVoiceVerificationService.hpp"

#include "SunoClient.hpp"
#include "SunoEndpoints.hpp"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>

namespace vc::suno {

SunoVoiceVerificationService::SunoVoiceVerificationService(SunoClient* client, QObject* parent)
    : QObject(parent), client_(client) {}

SunoVoiceVerificationService::Status SunoVoiceVerificationService::parseStatus(const QString& text)
{
    if (text == QStringLiteral("submitted")) {
        return Status::Submitted;
    }
    if (text == QStringLiteral("approved")) {
        return Status::Approved;
    }
    return Status::Unknown;
}

void SunoVoiceVerificationService::fetchPhrase()
{
    enqueue(Operation::Phrase, qstr(endpoints::VOICE_VERIFICATION_PHRASE), "GET", {}, true);
}

void SunoVoiceVerificationService::submitStem(const UploadId& uploadId, double vocalStartS,
                                              double vocalEndS)
{
    if (!uploadId.isValid()) {
        fail(Operation::Stem, QStringLiteral("An upload id is required."));
        return;
    }
    if (vocalStartS < 0.0 || vocalEndS < 0.0 || vocalStartS >= vocalEndS) {
        fail(Operation::Stem,
             QStringLiteral("Invalid vocal range: start must be non-negative and less than end."));
        return;
    }
    const QByteArray body =
            QJsonDocument(QJsonObject{{QStringLiteral("upload_id"), uploadId.value},
                                      {QStringLiteral("vocal_start_s"), vocalStartS},
                                      {QStringLiteral("vocal_end_s"), vocalEndS}})
                    .toJson(QJsonDocument::Compact);
    enqueue(Operation::Stem, qstr(endpoints::PROCESSED_CLIP_VOICE_VOX_STEM), "POST", body, false);
}

void SunoVoiceVerificationService::submitVerification(const QString& voiceRecordingId,
                                                      const QString& verificationRecordingId,
                                                      const QString& phraseId)
{
    if (voiceRecordingId.isEmpty() || verificationRecordingId.isEmpty() || phraseId.isEmpty()) {
        fail(Operation::Verification, QStringLiteral("All verification fields are required."));
        return;
    }
    const QByteArray body = QJsonDocument(QJsonObject{{QStringLiteral("voice_recording_id"),
                                                         voiceRecordingId},
                                                        {QStringLiteral("verification_recording_id"),
                                                         verificationRecordingId},
                                                        {QStringLiteral("phrase_id"), phraseId}})
                                    .toJson(QJsonDocument::Compact);
    enqueue(Operation::Verification, qstr(endpoints::VOICE_VERIFICATION_CREATE), "POST", body, false);
}

void SunoVoiceVerificationService::poll(const VerificationId& verificationId)
{
    if (!verificationId.isValid()) {
        fail(Operation::Poll, QStringLiteral("A verification id is required."));
        return;
    }
    const QString path =
            QString(qstr(endpoints::VOICE_VERIFICATION_BY_ID)).replace(QStringLiteral("{}"), verificationId.value);
    enqueue(Operation::Poll, path, "GET", {}, true);
}

void SunoVoiceVerificationService::enqueue(Operation operation, const QString& path,
                                           const QString& method, const QByteArray& data,
                                           bool retryable)
{
    if (!client_ || !client_->isAuthenticated()) {
        fail(operation, QStringLiteral("Sign in to Suno first."));
        return;
    }
    quint64* counter = nullptr;
    bool* busy = nullptr;
    switch (operation) {
    case Operation::Phrase:
        counter = &phraseGeneration_;
        busy = &fetching_;
        break;
    case Operation::Stem:
        counter = &stemGeneration_;
        busy = &submittingStem_;
        break;
    case Operation::Verification:
        counter = &verificationGeneration_;
        busy = &submittingVerification_;
        break;
    case Operation::Poll:
        counter = &pollGeneration_;
        busy = &polling_;
        break;
    }
    *busy = true;
    const quint64 generation = ++(*counter);
    emit changed();

    QPointer<SunoVoiceVerificationService> guard(this);
    client_->enqueueAuthenticatedRequest(
            path, method.toStdString(), data,
            [guard, operation, generation](QNetworkReply* reply) {
                if (!guard) {
                    reply->deleteLater();
                    return;
                }
                guard->handleReply(reply, operation, generation);
            },
            retryable);
}

void SunoVoiceVerificationService::handleReply(QNetworkReply* reply, Operation operation,
                                               quint64 generation)
{
    quint64 current = 0;
    switch (operation) {
    case Operation::Phrase:
        current = phraseGeneration_;
        break;
    case Operation::Stem:
        current = stemGeneration_;
        break;
    case Operation::Verification:
        current = verificationGeneration_;
        break;
    case Operation::Poll:
        current = pollGeneration_;
        break;
    }
    // An answer from an abandoned call of the same operation is dropped, not
    // merged: a late reply must not overwrite the newer call's section. Other
    // operations carry their own generation, so they are unaffected.
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
    switch (operation) {
    case Operation::Phrase:
        fetching_ = false;
        phraseState_.phraseId = object.value(QStringLiteral("phrase_id")).toString();
        phraseState_.phraseText = object.value(QStringLiteral("phrase_text")).toString();
        phraseState_.raw = object;
        break;
    case Operation::Stem:
        submittingStem_ = false;
        stemState_.processedClipId = object.value(QStringLiteral("id")).toString();
        stemState_.voiceRecordingId = object.value(QStringLiteral("voice_recording_id")).toString();
        stemState_.statusText = object.value(QStringLiteral("status")).toString();
        stemState_.status = parseStatus(stemState_.statusText);
        stemState_.raw = object;
        break;
    case Operation::Verification:
        submittingVerification_ = false;
        verificationState_.verificationId = object.value(QStringLiteral("id")).toString();
        verificationState_.statusText = object.value(QStringLiteral("status")).toString();
        verificationState_.status = parseStatus(verificationState_.statusText);
        verificationState_.raw = object;
        break;
    case Operation::Poll:
        polling_ = false;
        pollState_.statusText = object.value(QStringLiteral("status")).toString();
        pollState_.status = parseStatus(pollState_.statusText);
        pollState_.verificationPromptText =
                object.value(QStringLiteral("verification_prompt_text")).toString();
        pollState_.raw = object;
        break;
    }
    emit changed();
}

void SunoVoiceVerificationService::fail(Operation operation, const QString& message)
{
    switch (operation) {
    case Operation::Phrase:
        fetching_ = false;
        phraseState_ = {};
        break;
    case Operation::Stem:
        submittingStem_ = false;
        stemState_ = {};
        break;
    case Operation::Verification:
        submittingVerification_ = false;
        verificationState_ = {};
        break;
    case Operation::Poll:
        polling_ = false;
        pollState_ = {};
        break;
    }
    failureReason_ = message;
    emit failed(message);
}

} // namespace vc::suno
