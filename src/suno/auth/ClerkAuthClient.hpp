#pragma once
// ClerkAuthClient.hpp - owns ALL Clerk traffic for Suno authentication.
//
// Wire contract captured from auth.suno.com traffic (T1 Burp capture, Aug
// 2026) - implement exactly this, nothing speculative:
//   primary   GET  {AUTH_BASE}/client?__clerk_api_version=..&_clerk_js_version=..
//   primary   POST {AUTH_BASE}/client/sessions/{sid}/touch?...   (empty body)
//   fallback  POST https://clerk.suno.com/v1/client/sessions/{sid}/client
//                  ?_is_native=true&_clerk_js_version=5.117.0     (legacy path)
//
// Every request carries the captured Cookie header verbatim plus browser-like
// Origin/Referer/User-Agent; POSTs are FORM-encoded (empty body), NOT JSON.
//
// Policy stays with the caller: no timers here, and at most ONE fallback
// attempt per request (no retry storms).

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

class ClerkAuthClient : public QObject {
    Q_OBJECT

public:
    // Endpoints / protocol versions observed on the wire (Aug 2026 capture).
    static constexpr const char* AUTH_BASE = "https://auth.suno.com/v1";
    static constexpr const char* LEGACY_BASE = "https://clerk.suno.com";
    static inline const QString CLERK_API_VERSION = QStringLiteral("2025-11-10");
    static inline const QString CLERK_JS_VERSION = QStringLiteral("5.117.0");

    explicit ClerkAuthClient(QObject* parent = nullptr);
    ~ClerkAuthClient() override;
    ClerkAuthClient(const ClerkAuthClient&) = delete;
    ClerkAuthClient& operator=(const ClerkAuthClient&) = delete;

    /// GET the Clerk client envelope and emit bearerReady() with the token of
    /// the last-active session (decoded expiry included), or authFailed().
    void fetchBearer(const Credentials& creds);

    /// POST a touch for `sessionId` to mint a fresh bearer token.
    void touch(const Credentials& creds, const QString& sessionId);

signals:
    void bearerReady(const vc::suno::auth::BearerToken& token);
    void authFailed(const QString& reason);

private:
    /// Everything a reply handler needs to continue (or fall back) for one
    /// logical request.
    struct CallContext {
        Credentials creds;
        QString sessionId; ///< Empty for fetchBearer.
        bool allowFallback = true;
    };

    void startClientFetch(CallContext ctx);
    void startTouch(CallContext ctx);
    void startLegacyFallback(CallContext ctx);

    void handleReply(QNetworkReply* reply, CallContext ctx);
    void handleEnvelopeBody(const QByteArray& body, const CallContext& ctx);
    void handleLegacyBody(const QByteArray& body);

    /// Abort every in-flight reply; called from the destructor so QNAM never
    /// touches a dead owner.
    void abortInflight();

    static QNetworkRequest makeRequest(const QUrl& url, const Credentials& creds, bool isPost);

    QNetworkAccessManager* nam_ = nullptr;

    /// In-flight replies (aborted on destruction). QPointer guards against a
    /// reply that already self-destructed via deleteLater().
    QList<QPointer<QNetworkReply>> inflight_;

    /// Session id from the most recent successful envelope; lets fetchBearer
    /// use the legacy fallback even before touch() has ever been called.
    QString lastKnownSessionId_;
};

} // namespace vc::suno::auth
