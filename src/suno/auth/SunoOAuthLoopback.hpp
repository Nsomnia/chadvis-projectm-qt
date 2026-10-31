#pragma once
// SunoOAuthLoopback.hpp — the official Suno OAuth 2.0 client (MCP lane).
//
// Suno's external OAuth server (studio-api-prod…/api/v2/external/oauth) is the
// auth lane for the MCP surface at api.suno.com/mcp — its RFC 8414 metadata was
// observed live on 2026-10-31 (docs/suno_api/MCP.md). This class implements the
// RFC 8252 native-app shape against it: dynamic client registration for a
// 127.0.0.1 redirect, PKCE S256, the system browser for sign-in and consent,
// and a one-shot loopback listener that catches the authorization code and
// exchanges it for a token set.
//
// This is NOT the Clerk cookie lane (ClerkAuthClient / T0199), and it does not
// touch the capture-gated Google/Facebook native-callback question in
// docs/suno_api/OAUTH_REDIRECT_ANALYSIS.md — there the social sign-in happens
// inside Suno's own browser flow, and what reaches this process is a
// Suno-issued code on a loopback the caller registered.
//
// Fail-closed rules this class keeps:
//   * binds 127.0.0.1 only, one request per connection, `state` verified;
//   * registration/token errors are reported as status + RFC 6749 error code,
//     never the body, and never a token in a log line;
//   * no redirect following on any request (ManualRedirectPolicy), so a
//     hostile 3xx cannot move a code or a verifier to another origin.

#include <QByteArray>
#include <QHash>
#include <QMetaType>
#include <QObject>
#include <QString>
#include <QUrl>

#include "suno/HttpPolicy.hpp"

#include <functional>
#include <optional>

class QNetworkAccessManager;
class QNetworkReply;
class QTcpServer;
class QTcpSocket;
class QTimer;

namespace vc::suno::auth {

class SunoOAuthLoopback final : public QObject
{
    Q_OBJECT

public:
    /// RFC 8414 endpoints, as observed in the server metadata (2026-10-31).
    struct Endpoints {
        QUrl authorization{QStringLiteral(
                "https://studio-api-prod.suno.com/api/v2/external/oauth/authorize/")};
        QUrl token{QStringLiteral(
                "https://studio-api-prod.suno.com/api/v2/external/oauth/token/")};
        QUrl registration{QStringLiteral(
                "https://studio-api-prod.suno.com/api/v2/external/oauth/register/")};
    };

    struct ClientConfig {
        QString clientName{QStringLiteral("ChadVis")};
        /// The eight scopes api.suno.com/mcp advertises in its protected-resource
        /// metadata. The server accepts 21; these are the MCP set.
        QString scope{QStringLiteral(
                "api:v0 credits:spend download library:read library:search "
                "library:write offline_access profile:read")};
        QString resource{QStringLiteral("https://api.suno.com/mcp")};
    };

    /// RFC 6749 token response, reduced to what a desktop client keeps.
    struct TokenSet {
        QString accessToken;
        QString refreshToken;
        QString tokenType;
        QString scope;
        qint64 expiresInSeconds{0};

        [[nodiscard]] bool isEmpty() const { return accessToken.isEmpty(); }
    };

    /// Browser launcher seam. `false` means the browser could not be opened;
    /// the default is QDesktopServices::openUrl.
    using BrowserLauncher = std::function<bool(const QUrl&)>;

    explicit SunoOAuthLoopback(QNetworkAccessManager* networkManager,
                               QObject* parent = nullptr);
    ~SunoOAuthLoopback() override;

    // ── Pure helpers (unit-tested; no sockets, no network) ─────────────────

    /// RFC 7636 code verifier: 64 characters from the unreserved set.
    [[nodiscard]] static QString makeCodeVerifier();

    /// S256 challenge: base64url(SHA-256(verifier)), unpadded.
    [[nodiscard]] static QString pkceChallenge(const QString& codeVerifier);

    /// The authorize URL with every parameter the observed MCP flow sends.
    [[nodiscard]] static QUrl buildAuthorizeUrl(const QUrl& authorizeEndpoint,
                                                const ClientConfig& config,
                                                const QString& clientId,
                                                const QString& redirectUri,
                                                const QString& state,
                                                const QString& codeChallenge);

    /// Extract the request target from the first line of a raw HTTP request:
    /// `"GET /callback?code=…&state=… HTTP/1.1\r\n…"` → `"/callback?code=…&state=…"`.
    /// Returns nullopt for anything that is not a GET/HEAD request line.
    [[nodiscard]] static std::optional<QByteArray> parseRequestTarget(
            const QByteArray& rawRequestHead);

    /// Percent-decoded value of `key` in a raw query string (`a=b&c=d`).
    [[nodiscard]] static std::optional<QString> queryValue(const QByteArray& query,
                                                           const char* key);

    // ── Seams ──────────────────────────────────────────────────────────────

    void setEndpoints(const Endpoints& endpoints);
    void setBrowserLauncher(BrowserLauncher launcher);

    // ── Lifecycle ──────────────────────────────────────────────────────────

    /// Listen on 127.0.0.1:0, register a dynamic client, build the authorize URL
    /// and open the browser. Asynchronous: one of authorized()/failed() follows.
    /// Returns false (with `safeError`) only when the run cannot start.
    bool begin(QString* safeError = nullptr);

    /// Abandon a run: close the listener, drop in-flight replies. No signal is
    /// emitted — the caller initiated this.
    void cancel();

    [[nodiscard]] bool isRunning() const;

signals:
    void authorizationUrlReady(const QUrl& url);
    void authorized(const vc::suno::auth::SunoOAuthLoopback::TokenSet& tokens);
    void failed(const QString& safeError);

private:
    void onRegisterFinished();
    void onNewConnection();
    void onSocketReadyRead(QTcpSocket* socket);
    /// Returns true when the callback was accepted (a page should say so).
    bool handleCallback(const QByteArray& target);
    void finishWithError(const QString& safeError);
    void exchangeCode(const QString& code);
    void cleanup();

    /// POST with the tree's streaming body policy: `BodyReader::pump` on
    /// readyRead (abort one byte past the cap), never a post-hoc `readAll`.
    /// Returns nullptr when no bounded reader could be built.
    QNetworkReply* postBounded(const QNetworkRequest& request, const QByteArray& body);

    QNetworkAccessManager* nam_{nullptr};
    QTcpServer* server_{nullptr};
    QTimer* timeout_{nullptr};
    QNetworkReply* registerReply_{nullptr};
    QHash<QNetworkReply*, http::BodyReader> readers_;
    BrowserLauncher launcher_;

    Endpoints endpoints_{};
    ClientConfig config_{};

    QString clientId_;
    QString redirectUri_;
    QString codeVerifier_;
    QString state_;

    bool running_{false};
    bool exchangeStarted_{false};
};

} // namespace vc::suno::auth

Q_DECLARE_METATYPE(vc::suno::auth::SunoOAuthLoopback::TokenSet)
