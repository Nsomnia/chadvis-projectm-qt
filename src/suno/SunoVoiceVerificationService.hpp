#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>

class QNetworkReply;

namespace vc::suno {

class SunoClient;

/// Strong wrapper for an upload id (`upload_id` on the wire).
///
/// It is deliberately NOT convertible to/from QString or to VerificationId:
/// passing a gen clip id to the upload stem route is a 404/422, so the types
/// keep the two stem routes' id kinds from being swapped at a call site.
struct UploadId {
    QString value;
    explicit UploadId(QString v) : value(std::move(v)) {}
    [[nodiscard]] bool isValid() const { return !value.isEmpty(); }
};

/// Strong wrapper for a voice-verification id (`id` on the wire, used by the
/// poll route). Not interchangeable with UploadId.
struct VerificationId {
    QString value;
    explicit VerificationId(QString v) : value(std::move(v)) {}
    [[nodiscard]] bool isValid() const { return !value.isEmpty(); }
};

/// Voice-verification chain for a persona voice (ENDPOINT-INVENTORY §5.15,
/// persona HAR 2026-11-06):
///
///   1. `GET /voice-verification/phrase/` -> {phrase_id, phrase_text}
///   2. `POST /processed_clip/voice-vox-stem` from an UPLOAD id
///      {upload_id, vocal_start_s, vocal_end_s} -> {id, status, voice_recording_id}
///   3. `POST /voice-verification/`
///      {voice_recording_id, verification_recording_id, phrase_id}
///   4. `GET /voice-verification/{id}` -> poll; terminal status "approved".
///
/// The upload stem route takes an upload id; the clip stem route in
/// SunoStemService (`POST /clip/{id}/vox-stem`) takes a gen clip id. The two
/// must not be interchanged, which is why this service takes UploadId and
/// VerificationId strong wrappers rather than bare QString.
///
/// Each operation carries its own generation counter and its own state
/// section: a failure empties only its own section and names the reason, and
/// a late reply from an abandoned call of the same operation is dropped. A
/// status string that was never observed degrades to `Status::Unknown` with
/// the raw object preserved; no terminal state is ever guessed.
class SunoVoiceVerificationService final : public QObject
{
    Q_OBJECT

public:
    /// Status values actually observed on this chain ("submitted" on the stem
    /// submit, "approved" as the only observed terminal). Anything else maps
    /// to `Unknown`.
    enum class Status { Submitted, Approved, Unknown };

    struct PhraseState {
        QString phraseId;
        QString phraseText;
        QJsonObject raw;
    };

    struct StemState {
        /// Processed-clip id (`id` on the wire) from the stem submit response.
        QString processedClipId;
        /// `voice_recording_id` from the stem submit response.
        QString voiceRecordingId;
        Status status = Status::Unknown;
        QString statusText;
        QJsonObject raw;
    };

    struct VerificationState {
        /// Verification id (`id` on the wire) from the verification submit.
        QString verificationId;
        Status status = Status::Unknown;
        QString statusText;
        QJsonObject raw;
    };

    struct PollState {
        Status status = Status::Unknown;
        QString statusText;
        QString verificationPromptText;
        QJsonObject raw;
    };

    explicit SunoVoiceVerificationService(SunoClient* client, QObject* parent = nullptr);

    void fetchPhrase();
    /// Submit a voice stem extraction over the caller-selected range for an
    /// uploaded recording. Rejects an invalid range (negative, or start >=
    /// end) locally with `failed`: no request reaches the network.
    void submitStem(const UploadId& uploadId, double vocalStartS, double vocalEndS);
    void submitVerification(const QString& voiceRecordingId, const QString& verificationRecordingId,
                            const QString& phraseId);
    void poll(const VerificationId& verificationId);

    [[nodiscard]] const PhraseState& phraseState() const { return phraseState_; }
    [[nodiscard]] const StemState& stemState() const { return stemState_; }
    [[nodiscard]] const VerificationState& verificationState() const { return verificationState_; }
    [[nodiscard]] const PollState& pollState() const { return pollState_; }
    [[nodiscard]] bool isFetchingPhrase() const { return fetching_; }
    [[nodiscard]] bool isSubmittingStem() const { return submittingStem_; }
    [[nodiscard]] bool isSubmittingVerification() const { return submittingVerification_; }
    [[nodiscard]] bool isPolling() const { return polling_; }
    [[nodiscard]] const QString& failureReason() const { return failureReason_; }

    static Status parseStatus(const QString& text);

signals:
    void changed();
    void failed(const QString& message);

private:
    enum class Operation { Phrase, Stem, Verification, Poll };

    void enqueue(Operation operation, const QString& path, const QString& method,
                 const QByteArray& data, bool retryable);
    void handleReply(QNetworkReply* reply, Operation operation, quint64 generation);
    void fail(Operation operation, const QString& message);

    SunoClient* client_;
    PhraseState phraseState_;
    StemState stemState_;
    VerificationState verificationState_;
    PollState pollState_;
    bool fetching_ = false;
    bool submittingStem_ = false;
    bool submittingVerification_ = false;
    bool polling_ = false;
    quint64 phraseGeneration_ = 0;
    quint64 stemGeneration_ = 0;
    quint64 verificationGeneration_ = 0;
    quint64 pollGeneration_ = 0;
    QString failureReason_;
};

} // namespace vc::suno
