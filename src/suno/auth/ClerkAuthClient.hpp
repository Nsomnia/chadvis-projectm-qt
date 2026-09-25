#pragma once

#include "AuthTypes.hpp"

#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QString>

class QNetworkReply;
class QNetworkRequest;
class QUrl;

namespace vc::suno::auth {

/// Secret-free classification of the most recently completed auth request.
enum class AuthFailureKind {
    None,
    NoActiveSession,
    RejectedCredential,
    ProtocolMismatch,
    MalformedResponse,
};

/// Type of value found in the single persisted `suno/default` slot. This is
/// an internal auth-module classification and is never exposed to QML.
enum class StoredCredentialShape {
    Empty,
    BearerToken,
    ClerkCookieHeader,
    Unsupported,
};

struct StoredCredentialClassification {
    StoredCredentialShape shape = StoredCredentialShape::Empty;
    AuthFailureKind failureKind = AuthFailureKind::None;
    /// Secret-free diagnostic suitable for logs and the local auth UI.
    QString safeDiagnostic;
};

/// Classifies a credential by shape only. JWTs use the same decoder acceptance
/// as the rest of the auth module; cookie headers must contain name=value pairs
/// and at least one capture-proven Clerk cookie (`__client` / `__client_uat`).
[[nodiscard]] StoredCredentialClassification classifyStoredCredential(
        const QString& value);

class ClerkAuthClient : public QObject {
    Q_OBJECT

public:
    static constexpr const char* AUTH_BASE = "https://auth.suno.com/v1";

    explicit ClerkAuthClient(QObject* parent = nullptr);
    ~ClerkAuthClient() override;
    ClerkAuthClient(const ClerkAuthClient&) = delete;
    ClerkAuthClient& operator=(const ClerkAuthClient&) = delete;

    /// GET the Clerk client envelope and emit bearerReady() with the token of
    /// the last-active session (decoded expiry included), or authFailed().
    void fetchBearer(const Credentials& creds);

    /// POST a touch for `sessionId` to mint a fresh bearer token.
    void touch(const Credentials& creds, const QString& sessionId);

    /// Classification of the most recently completed request. Reset to None
    /// when a request starts and after a successful bearer exchange.
    [[nodiscard]] AuthFailureKind failureKind() const noexcept { return failureKind_; }

signals:
    void bearerReady(const vc::suno::auth::BearerToken& token);
    void authFailed(const QString& reason);

private:
    friend class ClerkAuthClientTestAccess;

    /// Everything a reply handler needs to continue (or fall back) for one
    /// logical request.
    struct CallContext {
        Credentials creds;
        QString sessionId; ///< Empty for fetchBearer.
        bool allowFallback = true;
    };

    void startClientFetch(CallContext ctx);
    void startTouch(CallContext ctx);
    void startTokenFallback(CallContext ctx);

    void handleReply(QNetworkReply* reply, CallContext ctx);
    void handleEnvelopeBody(const QByteArray& body, const CallContext& ctx);
    void handleTokenFallbackBody(const QByteArray& body);

    [[nodiscard]] static AuthFailureKind classifyHttpFailure(int status) noexcept;
    [[nodiscard]] static QString httpFailureReason(int status);
    void emitFailure(AuthFailureKind kind, const QString& reason);

    /// Abort every in-flight reply; called from the destructor so QNAM never
    /// touches a dead owner.
    void abortInflight();

    static QNetworkRequest makeRequest(const QUrl& url, const Credentials& creds, bool isPost);

    QNetworkAccessManager* nam_ = nullptr;

    /// In-flight replies (aborted on destruction). QPointer guards against a
    /// reply that already self-destructed via deleteLater().
    QList<QPointer<QNetworkReply>> inflight_;

    QString lastKnownSessionId_;

    AuthFailureKind failureKind_ = AuthFailureKind::None;
};

} // namespace vc::suno::auth
