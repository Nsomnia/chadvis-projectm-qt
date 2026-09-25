#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QMap>
#include <QNetworkRequest>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>
#include <expected>
#include <optional>

class QNetworkAccessManager;
class QNetworkReply;

namespace vc::suno {

class SunoClient;

class SunoAudioUploadService final : public QObject
{
    Q_OBJECT

public:
    struct InitializeResponse
    {
        QString id;
        QUrl url;
        bool isFileUploaded{false};
        QMap<QString, QString> fields;
    };

    explicit SunoAudioUploadService(SunoClient* client,
                                   QNetworkAccessManager* directNetworkManager,
                                   QObject* parent = nullptr);
    ~SunoAudioUploadService() override;

    [[nodiscard]] static QJsonObject initializeBody();
    [[nodiscard]] static QJsonObject finishBody(const QString& uploadFilename);
    [[nodiscard]] static std::expected<InitializeResponse, QString>
    parseInitializeResponse(const QByteArray& payload);
    [[nodiscard]] static QNetworkRequest directRequest(const QUrl& url);
    [[nodiscard]] static bool isSupportedAudioPath(const QString& path);

    void start(const QString& localFilePath);

    [[nodiscard]] bool isUploading() const { return uploading_; }
    [[nodiscard]] int progress() const { return progress_; }
    [[nodiscard]] QString status() const { return status_; }
    [[nodiscard]] QString error() const { return error_; }

signals:
    void stateChanged();
    void progressChanged();
    void statusChanged();
    void errorChanged();
    void completed(const QString& uploadId);
    void failed(const QString& message);

private:
    void initialize();
    void handleInitializeReply(QNetworkReply* reply, quint64 generation);
    void startDirectUpload(quint64 generation);
    void handleDirectReply(QNetworkReply* reply, quint64 generation);
    void startFinish(quint64 generation);
    void handleFinishReply(QNetworkReply* reply, quint64 generation);
    void fail(const QString& message);
    void finish();
    void setUploading(bool uploading);
    void setProgress(int progress);
    void setStatus(const QString& status);
    void setError(const QString& error);

    SunoClient* client_{nullptr};
    QPointer<QNetworkAccessManager> directNetworkManager_;
    QNetworkReply* directReply_{nullptr};
    std::optional<InitializeResponse> ticket_;
    QString filePath_;
    QString fileName_;
    QString uploadId_;
    quint64 generation_{0};
    bool uploading_{false};
    int progress_{0};
    QString status_;
    QString error_;
};

}
