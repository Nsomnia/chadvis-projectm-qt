#include "SunoAudioUploadService.hpp"

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
    const int port = url.port();
    return url.isValid() && !url.isRelative() &&
           url.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0 &&
           url.host().compare(qstr(endpoints::AUDIO_UPLOAD_STORAGE_HOST),
                              Qt::CaseInsensitive) == 0 &&
           (port == -1 || port == 443) && url.userInfo().isEmpty() &&
           url.fragment().isEmpty() && !url.path().isEmpty();
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
    if (!client_ || !client_->isAuthenticated() ||
        (!directNetworkManager_ && !directReplyFactory_)) {
        fail(QStringLiteral("Sign in to Suno before uploading audio."));
        return;
    }

    const QFileInfo fileInfo(localFilePath);
    if (localFilePath.isEmpty() || !isSupportedAudioPath(localFilePath) ||
        !fileInfo.exists() || !fileInfo.isFile() || !fileInfo.isReadable() ||
        fileInfo.size() <= 0) {
        fail(QStringLiteral("Choose a readable audio file."));
        return;
    }

    const quint64 generation = ++generation_;
    filePath_ = localFilePath;
    fileName_ = fileInfo.fileName();
    uploadId_.clear();
    ticket_.reset();
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
    const QByteArray payload = reply->readAll();
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
    filePart.setHeader(QNetworkRequest::ContentTypeHeader,
                       QStringLiteral("application/octet-stream"));
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
    ticket_.reset();
    if (networkError != QNetworkReply::NoError || status != 204) {
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
