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
#include <functional>
#include <optional>

class QHttpMultiPart;
class QNetworkAccessManager;
class QNetworkReply;

namespace vc::suno {

class SunoClient;

class SunoAudioUploadService final : public QObject
{
    Q_OBJECT

public:
    using DirectReplyFactory = std::function<QNetworkReply*(
            const QNetworkRequest&, QHttpMultiPart*)>;

    struct InitializeResponse
    {
        QString id;
        QUrl url;
        bool isFileUploaded{false};
        QMap<QString, QString> fields;
    };

    explicit SunoAudioUploadService(SunoClient* client,
                                   QNetworkAccessManager* directNetworkManager,
                                   QObject* parent = nullptr,
                                   DirectReplyFactory directReplyFactory = {});
    ~SunoAudioUploadService() override;

    [[nodiscard]] static QJsonObject initializeBody();
    [[nodiscard]] static QJsonObject finishBody(const QString& uploadFilename);
    [[nodiscard]] static std::expected<InitializeResponse, QString>
    parseInitializeResponse(const QByteArray& payload);
    [[nodiscard]] static bool isSupportedTemporaryUrl(const QUrl& url);
    [[nodiscard]] static std::expected<QNetworkRequest, QString>
    directRequest(const QUrl& url);
    [[nodiscard]] static bool isSupportedAudioPath(const QString& path);

    void start(const QString& localFilePath);

    /// T0035: refuse to start an upload larger than this. 500 MiB. Suno's own
    /// limit is not documented anywhere in this tree, so this is a bound on what
    /// we are willing to push through a user-chosen file, NOT a claim about the
    /// server. Without it `start()` validated only `size() > 0`, so a 2 GB file
    /// would be POSTed straight to S3 with no way for the user to stop it.
    static constexpr qint64 kMaxUploadBytes = 500LL * 1024 * 1024;

    /// T0035: user-initiated cancel. Previously the only aborts were private and
    /// destructor-driven, so a long upload was unstoppable from the UI.
    ///
    /// Reports through `failed()` rather than a separate `cancelled()` signal:
    /// the outcome a caller cares about is "no upload is running and this is why",
    /// and a second terminal signal would mean every completion handler needs two
    /// branches to avoid treating a cancel as a still-pending upload.
    void cancel();

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
    void cancelForAuthLoss();
    /// T0035: one retry of the storage POST. S3 tickets are single-use, so a
    /// retry MUST go back through initialize() for a fresh ticket rather than
    /// replaying the old fields -- a replayed POST is a signature failure, not a
    /// retry. One attempt is deliberate: the failure mode being fixed is a
    /// dropped connection, which a second POST fixes; anything else is a real
    /// error the user should see, and looping at them is worse than one retry.
    static constexpr int kMaxDirectUploadAttempts = 2;
    void fail(const QString& message);
    void finish();
    void setUploading(bool uploading);
    void setProgress(int progress);
    void setStatus(const QString& status);
    void setError(const QString& error);

    SunoClient* client_{nullptr};
    QPointer<QNetworkAccessManager> directNetworkManager_;
    DirectReplyFactory directReplyFactory_;
    QNetworkReply* directReply_{nullptr};
    std::optional<InitializeResponse> ticket_;
    QString filePath_;
    QString fileName_;
    QString uploadId_;
    /// T0035: direct-upload attempts already spent on the current file. Reset by
    /// `start()`, so a retry never compounds with a previous upload's attempts.
    int directAttempts_{0};
    quint64 generation_{0};
    bool uploading_{false};
    int progress_{0};
    QString status_;
    QString error_;
};

}
