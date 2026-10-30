#pragma once

#include "SunoModels.hpp"

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <expected>

#include <vector>

class QNetworkReply;
class QTimer;

namespace vc::suno {

class SunoClient;

/// One song generation: the captcha preflight, the submit, and the completion
/// poll. The three steps are one class because the second is meaningless without
/// the first and the third is the only thing that says whether the second
/// worked.
///
/// Contracts, all measured against the live service on 2026-10-30 with the
/// account owner's own session (owner grant, AGENTS.md "Authorized live work"):
///
///   POST /c/check {"ctype":"generation"} -> 200 {required: bool, captcha_version: int}
///     2 means Cloudflare Turnstile and `token_provider: 2`; anything else (or a
///     missing version) means hCaptcha and `token_provider: 1`.
///
///   POST /generate/v2-web/  -- a FLAT body. Sending `{params:{...}}` answers a
///     422 naming `body.params.prompt`, and that path is a red herring: the route
///     accepts several body schemas and FastAPI reports the last variant's error.
///     The flat keys are the ones that work, and `task` is a closed enum whose
///     ordinary value is `generate`.
///
///   The submit needs no `token` when `/c/check` says `required: false`. When it
///     says true, the submit without a token answers 422
///     `token_validation_failed` -- a real refusal, never a silent retry, because
///     a submit that quietly spent credits after a failed challenge would be the
///     worst outcome in this file.
///
///   Completion is read back with `GET /clips/get_songs_by_ids?ids=...`, the
///     route the site itself uses for a submitted batch. There is no fixed
///     polling interval in any capture, so this class polls at a fixed, bounded
///     rate and stops on its own; a clip that never completes ends as `timeout`,
///     not as an infinite spinner.
class SunoGenerationService final : public QObject
{
    Q_OBJECT

public:
    enum class State {
        Idle,
        CheckingCaptcha,
        NeedsCaptcha,
        Submitting,
        Polling,
        Completed,
        Failed,
    };
    Q_ENUM(State)

    struct Preflight {
        bool required{false};
        int captchaVersion{0};
        /// Turnstile (2) -> 2, everything else -> 1. Used only when a token is
        /// supplied; an absent token is never accompanied by a provider.
        [[nodiscard]] int tokenProvider() const { return captchaVersion == 2 ? 2 : 1; }
    };

    struct Request {
        QString prompt;
        QString tags;
        QString title;
        QString negativeTags;
        QString model;   ///< `mv`, e.g. chirp-v3-5. Empty -> the wire default.
        bool instrumental{false};
        /// Supplied by the caller when a challenge was solved. Empty is normal
        /// and correct when `required` is false.
        QString captchaToken;
        int captchaVersion{0};
    };

    explicit SunoGenerationService(SunoClient* client, QObject* parent = nullptr);

    /// The submit body, as captured. Static so the exact shape is testable
    /// without a network, and so no caller can quietly invent its own.
    static QJsonObject buildSubmitBody(const Request& request, const QString& transactionId);

    /// `{required, captcha_version}` from the preflight body. Absent keys mean
    /// "not required" -- a preflight that answers 200 with an empty object is the
    /// site's own "no challenge" case.
    static Preflight parsePreflight(const QJsonObject& root);

    /// The ids of a submit response (`{id, clips:[{id,...}]}`); the batch id is
    /// accepted as a clip id when `clips` is missing, because the older prose
    /// contract did exactly that.
    static std::vector<std::string> parseSubmittedClipIds(const QJsonObject& root);

    /// Step 1. Emits `preflightReady` or `failed`.
    void checkCaptcha();

    /// Step 2. Only after a preflight; a submit with no preflight is refused
    /// rather than guessed, because `required` decides whether a token is needed.
    void submit(const Request& request);

    /// Step 3 is automatic after a successful submit.

    [[nodiscard]] State state() const { return state_; }
    [[nodiscard]] const Preflight& preflight() const { return preflight_; }
    [[nodiscard]] const std::vector<std::string>& submittedClipIds() const { return clipIds_; }
    [[nodiscard]] const QList<SunoClip>& clips() const { return clips_; }
    [[nodiscard]] QString error() const { return error_; }

signals:
    void preflightReady();
    /// The server wants a challenge this build cannot solve. The UI must say so
    /// and offer the browser, never a dead button.
    void captchaRequired(int captchaVersion);
    void clipsSubmitted();
    void clipsUpdated();
    void completed();
    void failed(const QString& message);
    void stateChanged();

private:
    void enqueuePreflight();
    void enqueueSubmit(const Request& request);
    void enqueuePoll();
    void handlePreflightReply(QNetworkReply* reply, quint64 requestId);
    void handleSubmitReply(QNetworkReply* reply, const Request& request, quint64 requestId);
    void handlePollReply(QNetworkReply* reply, quint64 requestId);
    void setState(State state);
    void fail(const QString& message);
    void stopPolling();

    SunoClient* client_;
    State state_{State::Idle};
    Preflight preflight_;
    std::vector<std::string> clipIds_;
    QList<SunoClip> clips_;
    QString error_;
    QTimer* pollTimer_;
    int pollsRemaining_{0};
    quint64 requestId_{0};
    bool preflightDone_{false};
};

} // namespace vc::suno
