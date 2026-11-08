#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <expected>

class QNetworkReply;

namespace vc::suno {

class SunoClient;

/// The single-image cover-art generator: `POST /gen/prompt_image/`
/// (ENDPOINT-INVENTORY §4.10, T0362 step 6).
///
/// Request `{prompt}` answers `{image_url, image_s3_id, upload_id}`, where
/// `image_s3_id` is of the form `image_<uuid>`. That is exactly the id
/// `POST /playlist/v2/{id}/cover-image` takes as `cover_source.id`, so it is the
/// whole point of this route. The three observed keys are kept as structured
/// fields and nothing else is typed: the capture pinned those and no more.
///
/// One request, one answer. A failure leaves the state empty with a reason
/// rather than a stale image from the previous prompt.
class SunoCoverArtService final : public QObject
{
    Q_OBJECT

public:
    struct Image {
        /// `image_url`: the generated image itself.
        QString imageUrl;
        /// `image_s3_id` (`image_<uuid>`): what the cover-image verb wants.
        QString imageS3Id;
        /// `upload_id`: the server's own handle for the uploaded image.
        QString uploadId;

        [[nodiscard]] bool isEmpty() const { return imageS3Id.isEmpty(); }
    };

    explicit SunoCoverArtService(SunoClient* client, QObject* parent = nullptr);

    /// `{"prompt": <prompt>}` — the whole recorded request.
    static QJsonObject requestBody(const QString& prompt);

    /// `image_s3_id` is the required field: without it the answer cannot become
    /// a playlist cover, so a response missing it is a failure, not a partial
    /// success. `image_url` and `upload_id` are passed through as sent.
    static std::expected<Image, QString> parseImage(const QJsonObject& root);

    /// Generate one cover image. An empty prompt is a local failure with no
    /// network call.
    void generate(const QString& prompt);

    [[nodiscard]] const Image& image() const { return image_; }
    [[nodiscard]] QString failureReason() const { return failureReason_; }
    [[nodiscard]] bool isLoading() const { return loading_; }

signals:
    /// The state changed: a request started, or one settled (either way).
    void imageChanged();
    void failed(const QString& message);

private:
    void handleReply(QNetworkReply* reply, quint64 generation);
    void fail(const QString& message);

    SunoClient* client_;
    Image image_;
    QString failureReason_;
    bool loading_{false};
    quint64 generation_{0};
};

} // namespace vc::suno
