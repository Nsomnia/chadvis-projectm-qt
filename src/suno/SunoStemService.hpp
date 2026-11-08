#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>

class QNetworkReply;

namespace vc::suno {

class SunoClient;

/// Vocal stem extraction over a user-selected range, then poll for the result.
///
/// Evidence: ENDPOINT-INVENTORY §5.14 (HAR-11 2026-11-06).
///   * `POST /clip/{id}/vox-stem` with body exactly
///     `{"vocal_start_s":…, "vocal_end_s":…}` answers
///     `{id, status, source_clip_id, vocal_start_s, vocal_end_s}` where `id`
///     is a processed-clip id, not a gen id.
///   * `GET /processed_clip/{id}` returns the same keys and, once the stem is
///     finished, adds `vocal_audio_url`. The finished status observed for a
///     processed clip is `"complete"`.
///   * The twin stem route (`POST /processed_clip/voice-vox-stem`, §5.15)
///     observed `"submitted"` as its initial status.
///   * The range comes from the caller's selection. There is no auto-detect
///     route in the capture, so this service never invents one: an invalid
///     range (negative, or start >= end) fails before any request is sent.
///
/// A failure empties only the affected operation's section and names the
/// reason; the rest of the state is untouched. A status string that was never
/// observed degrades to `Status::Unknown` with the raw object preserved — it
/// is never guessed into a terminal state.
class SunoStemService final : public QObject
{
    Q_OBJECT

public:
    /// Status values actually observed for processed clips and their twin
    /// stem route. Anything else maps to `Unknown`.
    enum class Status { Submitted, Complete, Unknown };

    struct SubmitState {
        /// Processed-clip id (`id` on the wire) from the submit response.
        QString processedClipId;
        Status status = Status::Unknown;
        /// Verbatim status string from the wire, including unobserved ones.
        QString statusText;
        /// Full raw submit response object.
        QJsonObject raw;
    };

    struct PollState {
        /// Status string from the wire ("complete" when finished).
        Status status = Status::Unknown;
        QString statusText;
        /// Empty until the server adds it (finished stem).
        QString vocalAudioUrl;
        /// Full raw poll response object.
        QJsonObject raw;
    };

    explicit SunoStemService(SunoClient* client, QObject* parent = nullptr);

    /// Submit a stem extraction for `clipId` over the caller-selected range
    /// [`vocalStartS`, `vocalEndS`]. Rejects the range locally (with `failed`)
    /// when it is negative or empty: no request reaches the network.
    void submit(const QString& clipId, double vocalStartS, double vocalEndS);
    /// Poll one processed clip for its current state.
    void poll(const QString& processedClipId);

    [[nodiscard]] const SubmitState& submitState() const { return submitState_; }
    [[nodiscard]] const PollState& pollState() const { return pollState_; }
    [[nodiscard]] bool isSubmitting() const { return submitting_; }
    [[nodiscard]] bool isPolling() const { return polling_; }
    [[nodiscard]] const QString& failureReason() const { return failureReason_; }

    static Status parseStatus(const QString& text);

signals:
    void changed();
    void failed(const QString& message);

private:
    enum class Operation { Submit, Poll };

    void enqueue(Operation operation, const QString& path, const QString& method,
                 const QByteArray& data);
    void handleReply(QNetworkReply* reply, Operation operation, quint64 generation);
    void fail(Operation operation, const QString& message);

    SunoClient* client_;
    SubmitState submitState_;
    PollState pollState_;
    bool submitting_ = false;
    bool polling_ = false;
    quint64 submitGeneration_ = 0;
    quint64 pollGeneration_ = 0;
    QString failureReason_;
};

} // namespace vc::suno
