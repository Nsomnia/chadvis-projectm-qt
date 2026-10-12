#include "SunoAudioUploadService.hpp"

#include "CapturedHosts.hpp"
#include "HttpPolicy.hpp"
#include "SunoClient.hpp"
#include "SunoEndpoints.hpp"

#include <QFile>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QStringList>

#include <algorithm>
#include <utility>

namespace vc::suno {

namespace {

QStringList requiredFieldNames()
{
    return {
        QStringLiteral("AWSAccessKeyId"),
        QStringLiteral("Content-Type"),
        QStringLiteral("key"),
        QStringLiteral("policy"),
        QStringLiteral("signature"),
    };
}

QStringList supportedAudioSuffixes()
{
    return {QStringLiteral("m4a")};
}

QString multipartDisposition(const QString& name, const QString& filename = {})
{
    QString escapedName = name;
    escapedName.replace(QLatin1Char('\\'), QLatin1String("\\\\"));
    escapedName.replace(QLatin1Char('"'), QLatin1String("\\\""));
    QString value = QStringLiteral("form-data; name=\"%1\"").arg(escapedName);
    if (!filename.isEmpty()) {
        QString escapedFilename = filename;
        escapedFilename.replace(QLatin1Char('\\'), QLatin1String("\\\\"));
        escapedFilename.replace(QLatin1Char('"'), QLatin1String("\\\""));
        value += QStringLiteral("; filename=\"%1\"").arg(escapedFilename);
    }
    return value;
}

}

SunoAudioUploadService::SunoAudioUploadService(SunoClient* client,
                                               QNetworkAccessManager* directNetworkManager,
                                               QObject* parent,
                                               DirectReplyFactory directReplyFactory)
    : QObject(parent)
    , client_(client)
    , directNetworkManager_(directNetworkManager)
    , directReplyFactory_(std::move(directReplyFactory))
    , status_(QStringLiteral("Choose an audio file to upload."))
{
    if (client_)
    {
        connect(client_, &SunoClient::authStateChanged, this,
                [this]() {
                    if (client_ && client_->authState() != auth::AuthState::ActiveValid) {
                        cancelForAuthLoss();
                    }
                });
        connect(client_, &SunoClient::needsReauth, this,
                [this]() { cancelForAuthLoss(); });
        connect(client_, &SunoClient::credentialInvalidated, this,
                [this]() { cancelForAuthLoss(); });
    }
}

SunoAudioUploadService::~SunoAudioUploadService()
{
    if (directReply_) {
        QNetworkReply* reply = directReply_;
        QPointer<QNetworkReply> guard(reply);
        directReply_ = nullptr;
        disconnect(reply, nullptr, this, nullptr);
        reply->abort();
        if (guard) {
            reply->deleteLater();
        }
    }
    ticket_.reset();
}

QJsonObject SunoAudioUploadService::initializeBody()
{
    QJsonObject body;
    body[QStringLiteral("extension")] = QStringLiteral("m4a");
    body[QStringLiteral("upload_type")] = QStringLiteral("file_upload");
    return body;
}

QJsonObject SunoAudioUploadService::finishBody(const QString& uploadFilename)
{
    QJsonObject body;
    body[QStringLiteral("agreed_to_vip_upload_terms")] = false;
    body[QStringLiteral("upload_filename")] = uploadFilename;
    body[QStringLiteral("upload_type")] = QStringLiteral("file_upload");
    return body;
}

std::expected<SunoAudioUploadService::InitializeResponse, QString>
SunoAudioUploadService::parseInitializeResponse(const QByteArray& payload)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return std::unexpected(QStringLiteral("Audio upload initialization returned invalid JSON."));
    }

    const QJsonObject root = document.object();
    const QJsonValue idValue = root.value(QStringLiteral("id"));
    const QJsonValue urlValue = root.value(QStringLiteral("url"));
    const QJsonValue uploadedValue = root.value(QStringLiteral("is_file_uploaded"));
    const QJsonValue fieldsValue = root.value(QStringLiteral("fields"));
    if (!idValue.isString() || idValue.toString().isEmpty() ||
        !urlValue.isString() || !uploadedValue.isBool() || !fieldsValue.isObject()) {
        return std::unexpected(QStringLiteral("Audio upload initialization response is incomplete."));
    }

    InitializeResponse response;
    response.id = idValue.toString();
    response.url = QUrl(urlValue.toString());
    response.isFileUploaded = uploadedValue.toBool();
    if (!isSupportedTemporaryUrl(response.url)) {
        return std::unexpected(QStringLiteral("Audio upload initialization returned an invalid upload URL."));
    }

    const QJsonObject fields = fieldsValue.toObject();
    for (const QString& name : fields.keys()) {
        const QJsonValue value = fields.value(name);
        if (!value.isString()) {
            return std::unexpected(QStringLiteral("Audio upload initialization returned invalid upload fields."));
        }
        response.fields.insert(name, value.toString());
    }
    for (const QString& name : requiredFieldNames()) {
        if (!response.fields.contains(name) || response.fields.value(name).isEmpty()) {
            return std::unexpected(QStringLiteral("Audio upload initialization response is missing a required upload field."));
        }
    }
    return response;
}

bool SunoAudioUploadService::isSupportedTemporaryUrl(const QUrl& url)
{
    // T0033: the scheme/port/userinfo/path half of this was a fourth hand-rolled
    // copy of the outbound-host rules (see CapturedHosts.hpp:6). That half is now
    // the registry's, via mayBeFetched -- an upload carries the file but no bearer.
    //
    // The host test STAYS, and it is not redundant: the registry answers "is this a
    // host we may talk to", which admits artwork, media and the studio API too.
    // This function answers the narrower "is this the upload target", and widening
    // it would let a response redirect an upload at a captured-but-wrong host. Host
    // policy and endpoint identity are different questions; only the first belongs
    // in the registry.
    if (!url.host().compare(qstr(endpoints::AUDIO_UPLOAD_STORAGE_HOST),
                            Qt::CaseInsensitive) == 0) {
        return false;
    }
    // Fragment is not sent to the server and is not a host-policy question.
    return hosts::mayBeFetched(url, hosts::Tier::Production) &&
           url.fragment().isEmpty();
}

std::expected<QNetworkRequest, QString>
SunoAudioUploadService::directRequest(const QUrl& url)
{
    if (!isSupportedTemporaryUrl(url)) {
        return std::unexpected(QStringLiteral("Audio upload URL is not an allowed storage origin."));
    }
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    // 60 s, not 30: this is the one direction where a long silent gap is real,
    // because a congested uplink fills its TCP buffers and then waits. Still a
    // stall deadline rather than a wall-clock budget, so a 100 MB upload on a slow
    // line is not killed for taking fifteen minutes -- only for going silent that
    // long. See HttpPolicy.hpp for why that distinction is what makes the API
    // usable here at all.
    http::applyRequestPolicy(request, http::RequestClass::Upload);
    return request;
}

bool SunoAudioUploadService::isSupportedAudioPath(const QString& path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    return !suffix.isEmpty() && supportedAudioSuffixes().contains(suffix);
}

void SunoAudioUploadService::start(const QString& localFilePath)
{
    if (uploading_) {
        setError(QStringLiteral("An audio upload is already in progress."));
        return;
    }

    // Local file validation runs BEFORE the auth check (T0035). Two reasons, and
    // the second is the real one: validating a path costs one stat and needs no
    // credentials, while the auth check cannot say anything useful about the
    // file. Ordering it first means "this file is too large" is reported as such
    // instead of being masked by "sign in first", and it keeps the size bound
    // reachable in a test that has no client at all.
    const QFileInfo fileInfo(localFilePath);
    if (localFilePath.isEmpty() || !isSupportedAudioPath(localFilePath) ||
        !fileInfo.exists() || !fileInfo.isFile() || !fileInfo.isReadable() ||
        fileInfo.size() <= 0) {
        fail(QStringLiteral("Choose a readable audio file."));
        return;
    }
    // The upper bound that did not exist. Checked before the ticket is
    // requested, so an oversized file costs one stat rather than a full
    // initialize/upload/finish round trip that would fail at S3.
    if (fileInfo.size() > kMaxUploadBytes) {
        fail(QStringLiteral("That audio file is too large to upload (limit %1 MB).")
                 .arg(kMaxUploadBytes / (1024 * 1024)));
        return;
    }

    if (!client_ || !client_->isAuthenticated() ||
        (!directNetworkManager_ && !directReplyFactory_)) {
        fail(QStringLiteral("Sign in to Suno before uploading audio."));
        return;
    }

    const quint64 generation = ++generation_;
    filePath_ = localFilePath;
    fileName_ = fileInfo.fileName();
    uploadId_.clear();
    ticket_.reset();
    // T0035: attempt budget is per-upload, never per-service-lifetime, so a
    // retried upload that exhausts it does not also disable retrying the next one.
    directAttempts_ = 0;
    setError({});
    setProgress(0);
    setStatus(QStringLiteral("Preparing audio upload."));
    setUploading(true);
    initialize();
    Q_UNUSED(generation);
}

void SunoAudioUploadService::initialize()
{
    const quint64 generation = generation_;
    QPointer<SunoAudioUploadService> guard(this);
    client_->enqueueAuthenticatedRequest(
            qstr(endpoints::UPLOADS_AUDIO),
            "POST",
            QJsonDocument(initializeBody()).toJson(QJsonDocument::Compact),
            [guard, generation](QNetworkReply* reply) {
                if (guard) {
                    guard->handleInitializeReply(reply, generation);
                } else if (reply) {
                    reply->deleteLater();
                }
            },
            false);
}

void SunoAudioUploadService::handleInitializeReply(QNetworkReply* reply, quint64 generation)
{
    if (!reply) {
        return;
    }
    if (generation != generation_ || !uploading_) {
        reply->deleteLater();
        return;
    }

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    // Via SunoClient::readTrackedBody, NOT http::readBodyBounded(*reply, ...).
    //
    // This reply came from client_->enqueueAuthenticatedRequest, so SunoClient
    // armed a readyRead pump on it. readBodyBounded would therefore read an
    // ALREADY-DRAINED device and return an empty QByteArray -- which is not an
    // error, so it would sail straight into parseInitializeResponse and fail
    // every upload with a parse error that names nothing. The hazard is silent
    // and total, which is why tests/unit/suno/test_BoundedBody.cpp scans every
    // source for it rather than trusting this comment.
    auto body = client_ ? client_->readTrackedBody(reply)
                        : std::expected<QByteArray, QString>{
                              std::unexpected(QStringLiteral(
                                  "No client, so no bounded response body is "
                                  "available for this request"))};
    if (!body) {
        reply->deleteLater();
        fail(body.error());
        return;
    }
    const QByteArray payload = std::move(*body);
    reply->deleteLater();
    if (status != 200) {
        fail(QStringLiteral("Audio upload initialization failed."));
        return;
    }

    auto parsed = parseInitializeResponse(payload);
    if (!parsed) {
        fail(parsed.error());
        return;
    }
    if (parsed->isFileUploaded) {
        fail(QStringLiteral("The selected audio was already marked as uploaded."));
        return;
    }

    ticket_ = std::move(*parsed);
    uploadId_ = ticket_->id;
    setProgress(0);
    setStatus(QStringLiteral("Uploading audio."));
    startDirectUpload(generation);
}

void SunoAudioUploadService::startDirectUpload(quint64 generation)
{
    if (!ticket_ || generation != generation_ ||
        (!directNetworkManager_ && !directReplyFactory_)) {
        fail(QStringLiteral("Audio upload could not start."));
        return;
    }

    QFile* file = new QFile(filePath_);
    if (!file->open(QIODevice::ReadOnly)) {
        delete file;
        fail(QStringLiteral("The selected audio file could not be read."));
        return;
    }

    auto* multipart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    for (auto it = ticket_->fields.constBegin(); it != ticket_->fields.constEnd(); ++it) {
        QHttpPart fieldPart;
        fieldPart.setHeader(QNetworkRequest::ContentDispositionHeader,
                            multipartDisposition(it.key()));
        fieldPart.setHeader(QNetworkRequest::ContentTypeHeader,
                            QStringLiteral("text/plain; charset=UTF-8"));
        fieldPart.setBody(it.value().toUtf8());
        multipart->append(fieldPart);
    }

    QHttpPart filePart;
    filePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                       multipartDisposition(QStringLiteral("file"), file->fileName()));
    // T0035: was hardcoded application/octet-stream while the only accepted
    // suffix is m4a, so every upload declared itself opaque binary. Prefer the
    // server's own validated `Content-Type` field from the ticket -- it was
    // already required and already checked in parseInitializeResponse, and
    // throwing it away in favour of a local constant is how the two drifted.
    // m4a is ISO-BMFF, hence audio/mp4; the octet-stream fallback stays for a
    // ticket that somehow lacks the field, which parseInitializeResponse makes
    // unreachable but which should not crash if that changes.
    const QString ticketContentType = ticket_->fields.value(QStringLiteral("Content-Type"));
    filePart.setHeader(QNetworkRequest::ContentTypeHeader,
                       ticketContentType.isEmpty()
                           ? QStringLiteral("audio/mp4")
                           : ticketContentType);
    filePart.setBodyDevice(file);
    multipart->append(filePart);
    file->setParent(multipart);

    auto request = directRequest(ticket_->url);
    if (!request) {
        multipart->deleteLater();
        fail(request.error());
        return;
    }
    QNetworkReply* reply = directReplyFactory_
                                   ? directReplyFactory_(*request, multipart)
                                   : directNetworkManager_->post(*request, multipart);
    if (!reply) {
        multipart->deleteLater();
        fail(QStringLiteral("The audio upload could not be started."));
        return;
    }
    if (generation != generation_ || !uploading_) {
        reply->deleteLater();
        multipart->deleteLater();
        return;
    }
    multipart->setParent(reply);
    directReply_ = reply;

    QPointer<SunoAudioUploadService> guard(this);
    connect(reply, &QNetworkReply::uploadProgress, this,
            [guard, generation](qint64 sent, qint64 total) {
                if (!guard || generation != guard->generation_ || total <= 0) {
                    return;
                }
                const qint64 percent = (sent * 100) / total;
                guard->setProgress(static_cast<int>(std::clamp<qint64>(percent, 0, 100)));
            });
    connect(reply, &QNetworkReply::finished, this,
            [guard, generation, reply]() {
                if (guard) {
                    guard->handleDirectReply(reply, generation);
                } else {
                    reply->deleteLater();
                }
            });
}

void SunoAudioUploadService::handleDirectReply(QNetworkReply* reply, quint64 generation)
{
    if (!reply) {
        return;
    }
    if (generation != generation_ || !uploading_ || reply != directReply_) {
        reply->deleteLater();
        return;
    }

    directReply_ = nullptr;
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QNetworkReply::NetworkError networkError = reply->error();
    reply->deleteLater();
    if (networkError != QNetworkReply::NoError || status != 204) {
        // T0035: one retry, and only for the transient case. The ticket is
        // dropped before deciding, because a retry needs a fresh one -- S3
        // tickets are single-use, so replaying the old fields would fail as a
        // signature error and the user would see a retry that can never work.
        //
        // Retryable is deliberately narrow: no HTTP status at all, or 5xx, with a
        // network error that is not a cancellation. A 4xx is the server refusing
        // this exact request, and asking again is asking the same question.
        const bool cancelled = networkError == QNetworkReply::OperationCanceledError;
        const bool transportFailure = status == 0 || (status >= 500 && status < 600);
        const bool retryable = !uploading_ ? false
                                           : (!cancelled && transportFailure &&
                                              directAttempts_ < kMaxDirectUploadAttempts);
        ticket_.reset();
        if (retryable) {
            ++directAttempts_;
            setStatus(QStringLiteral("Upload interrupted. Retrying (%1 of %2)...")
                          .arg(directAttempts_)
                          .arg(kMaxDirectUploadAttempts - 1));
            setProgress(0);
            initialize();
            return;
        }
        fail(QStringLiteral("The audio file could not be uploaded."));
        return;
    }

    setProgress(100);
    setStatus(QStringLiteral("Finalizing audio upload."));
    startFinish(generation);
}

void SunoAudioUploadService::startFinish(quint64 generation)
{
    if (uploadId_.isEmpty() || fileName_.isEmpty() || generation != generation_) {
        fail(QStringLiteral("Audio upload finalization could not start."));
        return;
    }
    if (uploadId_.contains(QChar('/')) || uploadId_.contains(QChar('?')) ||
        uploadId_.contains(QChar('#'))) {
        fail(QStringLiteral("Audio upload finalization could not start."));
        return;
    }

    QString endpoint = qstr(endpoints::UPLOADS_AUDIO_UPLOAD_FINISH);
    endpoint.replace(QStringLiteral("{}"), uploadId_);
    QPointer<SunoAudioUploadService> guard(this);
    client_->enqueueAuthenticatedRequest(
            endpoint,
            "POST",
            QJsonDocument(finishBody(fileName_)).toJson(QJsonDocument::Compact),
            [guard, generation](QNetworkReply* reply) {
                if (guard) {
                    guard->handleFinishReply(reply, generation);
                } else if (reply) {
                    reply->deleteLater();
                }
            },
            false);
}

void SunoAudioUploadService::handleFinishReply(QNetworkReply* reply, quint64 generation)
{
    if (!reply) {
        return;
    }
    if (generation != generation_ || !uploading_) {
        reply->deleteLater();
        return;
    }

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QNetworkReply::NetworkError networkError = reply->error();
    reply->deleteLater();
    if (networkError != QNetworkReply::NoError || status < 200 || status >= 300) {
        fail(QStringLiteral("Audio upload finalization failed."));
        return;
    }

    const QString completedId = uploadId_;
    finish();
    emit completed(completedId);
}

void SunoAudioUploadService::cancelForAuthLoss()
{
    if (uploading_)
    {
        fail(QStringLiteral("Not authenticated"));
    }
}

void SunoAudioUploadService::cancel()
{
    if (!uploading_) {
        return;
    }
    // fail() bumps the generation, which is what makes every in-flight callback
    // a no-op, aborts and deletes directReply_, and drops the ticket. So this is
    // the same teardown as an error with a different sentence -- deliberately not
    // a parallel implementation that could drift from it.
    fail(QStringLiteral("Audio upload cancelled."));
}

void SunoAudioUploadService::fail(const QString& message)
{
    ++generation_;
    if (directReply_) {
        QNetworkReply* reply = directReply_;
        QPointer<QNetworkReply> guard(reply);
        directReply_ = nullptr;
        disconnect(reply, nullptr, this, nullptr);
        reply->abort();
        if (guard) {
            reply->deleteLater();
        }
    }
    ticket_.reset();
    filePath_.clear();
    fileName_.clear();
    uploadId_.clear();
    setUploading(false);
    setProgress(0);
    setStatus(QStringLiteral("Audio upload failed."));
    setError(message);
    emit failed(message);
}

void SunoAudioUploadService::finish()
{
    ticket_.reset();
    filePath_.clear();
    fileName_.clear();
    uploadId_.clear();
    setUploading(false);
    setProgress(100);
    setStatus(QStringLiteral("Audio upload complete."));
    setError({});
}

void SunoAudioUploadService::setUploading(bool uploading)
{
    if (uploading_ == uploading) {
        return;
    }
    uploading_ = uploading;
    emit stateChanged();
}

void SunoAudioUploadService::setProgress(int progress)
{
    const int bounded = qBound(0, progress, 100);
    if (progress_ == bounded) {
        return;
    }
    progress_ = bounded;
    emit progressChanged();
}

void SunoAudioUploadService::setStatus(const QString& status)
{
    if (status_ == status) {
        return;
    }
    status_ = status;
    emit statusChanged();
}

void SunoAudioUploadService::setError(const QString& error)
{
    if (error_ == error) {
        return;
    }
    error_ = error;
    emit errorChanged();
}

}
