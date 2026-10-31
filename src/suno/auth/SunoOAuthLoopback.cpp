#include "suno/auth/SunoOAuthLoopback.hpp"

#include "core/Logger.hpp"
#include "suno/HttpPolicy.hpp"
#include "suno/auth/AuthHeaders.hpp"

#include <QCryptographicHash>
#include <QDesktopServices>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrlQuery>

namespace vc::suno::auth {

namespace {

constexpr int kLoopbackTimeoutMs = 5 * 60 * 1000; // the user has to sign in
constexpr qsizetype kMaxRequestHeadBytes = 8 * 1024;

const QByteArray kOkPage = QByteArrayLiteral(
        "<!doctype html><html><body style=\"font-family:system-ui;text-align:center;"
        "padding-top:4rem\"><h2>Signed in to Suno</h2>"
        "<p>You can close this tab and return to ChadVis.</p></body></html>");

const QByteArray kBadRequestPage = QByteArrayLiteral(
        "<!doctype html><html><body style=\"font-family:system-ui;text-align:center;"
        "padding-top:4rem\"><h2>Unexpected request</h2>"
        "<p>This page is only used to finish a ChadVis sign-in.</p></body></html>");

void writeHtml(QTcpSocket* socket, const char* statusLine, const QByteArray& body)
{
    QByteArray response = QByteArray(statusLine) + "\r\n";
    response += "Content-Type: text/html; charset=utf-8\r\n";
    response += "Cache-Control: no-store\r\n";
    response += "Connection: close\r\n";
    response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n";
    response += body;
    socket->write(response);
    socket->disconnectFromHost();
}

} // namespace

// ---------------------------------------------------------------------------
// Pure helpers
// ---------------------------------------------------------------------------

QString SunoOAuthLoopback::makeCodeVerifier()
{
    // RFC 7636 §4.1: 43–128 chars from ALPHA / DIGIT / "-" / "." / "_" / "~".
    static const char alphabet[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~";
    constexpr int kLength = 64;
    QString out;
    out.reserve(kLength);
    auto* rng = QRandomGenerator::system();
    for (int i = 0; i < kLength; ++i) {
        out.append(QLatin1Char(alphabet[rng->bounded(int(sizeof(alphabet) - 1))]));
    }
    return out;
}

QString SunoOAuthLoopback::pkceChallenge(const QString& codeVerifier)
{
    const QByteArray digest =
            QCryptographicHash::hash(codeVerifier.toUtf8(), QCryptographicHash::Sha256);
    return QString::fromLatin1(digest.toBase64(QByteArray::Base64UrlEncoding |
                                               QByteArray::OmitTrailingEquals));
}

QUrl SunoOAuthLoopback::buildAuthorizeUrl(const QUrl& authorizeEndpoint,
                                          const ClientConfig& config,
                                          const QString& clientId,
                                          const QString& redirectUri,
                                          const QString& state,
                                          const QString& codeChallenge)
{
    QUrl url = authorizeEndpoint;
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("response_type"), QStringLiteral("code"));
    query.addQueryItem(QStringLiteral("client_id"), clientId);
    query.addQueryItem(QStringLiteral("redirect_uri"), redirectUri);
    query.addQueryItem(QStringLiteral("code_challenge"), codeChallenge);
    query.addQueryItem(QStringLiteral("code_challenge_method"), QStringLiteral("S256"));
    query.addQueryItem(QStringLiteral("state"), state);
    query.addQueryItem(QStringLiteral("scope"), config.scope);
    query.addQueryItem(QStringLiteral("resource"), config.resource);
    url.setQuery(query);
    return url;
}

std::optional<QByteArray> SunoOAuthLoopback::parseRequestTarget(const QByteArray& rawRequestHead)
{
    const qsizetype lineEnd = rawRequestHead.indexOf("\r\n");
    const QByteArray requestLine =
            lineEnd < 0 ? rawRequestHead : rawRequestHead.left(lineEnd);
    const QList<QByteArray> parts = requestLine.split(' ');
    if (parts.size() < 2) {
        return std::nullopt;
    }
    const QByteArray& method = parts.at(0);
    if (method != "GET" && method != "HEAD") {
        return std::nullopt;
    }
    const QByteArray& target = parts.at(1);
    if (!target.startsWith('/')) {
        return std::nullopt;
    }
    return target;
}

std::optional<QString> SunoOAuthLoopback::queryValue(const QByteArray& query, const char* key)
{
    const QByteArray wanted = QByteArray(key);
    for (const QByteArray& pair : query.split('&')) {
        const qsizetype eq = pair.indexOf('=');
        const QByteArray name =
                eq < 0 ? pair : pair.left(eq);
        if (name != wanted) {
            continue;
        }
        // A bare key is present-with-empty-value, not absent.
        return QUrl::fromPercentEncoding(eq < 0 ? QByteArray{} : pair.mid(eq + 1));
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

SunoOAuthLoopback::SunoOAuthLoopback(QNetworkAccessManager* networkManager, QObject* parent)
    : QObject(parent),
      nam_(networkManager),
      launcher_([](const QUrl& url) { return QDesktopServices::openUrl(url); })
{
}

SunoOAuthLoopback::~SunoOAuthLoopback() = default;

void SunoOAuthLoopback::setEndpoints(const Endpoints& endpoints) { endpoints_ = endpoints; }

void SunoOAuthLoopback::setBrowserLauncher(BrowserLauncher launcher)
{
    if (launcher) {
        launcher_ = std::move(launcher);
    }
}

bool SunoOAuthLoopback::isRunning() const { return running_; }

bool SunoOAuthLoopback::begin(QString* safeError)
{
    const auto fail = [safeError](const QString& message) {
        if (safeError) *safeError = message;
        return false;
    };

    if (running_) {
        return fail(QStringLiteral("a sign-in is already in progress"));
    }
    if (!nam_) {
        return fail(QStringLiteral("no network manager available"));
    }

    server_ = new QTcpServer(this);
    if (!server_->listen(QHostAddress::LocalHost, 0)) {
        server_->deleteLater();
        server_ = nullptr;
        return fail(QStringLiteral("could not open a local callback port"));
    }

    // 127.0.0.1, ephemeral port, path the registration named. The server's own
    // metadata says a loopback redirect is accepted (register answered 201 with
    // one in T0356's probe), so this is the sanctioned shape rather than a guess.
    redirectUri_ = QStringLiteral("http://127.0.0.1:%1/callback").arg(server_->serverPort());
    state_ = QString::fromLatin1(
            QCryptographicHash::hash(QByteArray::number(QRandomGenerator::system()->generate64()) +
                                             QByteArray::number(QRandomGenerator::system()->generate64()),
                                     QCryptographicHash::Sha256)
                    .toHex());

    QJsonObject body;
    body.insert(QStringLiteral("client_name"), config_.clientName);
    body.insert(QStringLiteral("redirect_uris"), QJsonArray{redirectUri_});
    body.insert(QStringLiteral("grant_types"),
                QJsonArray{QStringLiteral("authorization_code"), QStringLiteral("refresh_token")});
    body.insert(QStringLiteral("response_types"), QJsonArray{QStringLiteral("code")});
    body.insert(QStringLiteral("token_endpoint_auth_method"), QStringLiteral("none"));

    QNetworkRequest request(endpoints_.registration);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    http::applyRequestPolicy(request, http::RequestClass::Auth);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    request.setRawHeader("User-Agent", kBrowserUserAgent);
    request.setRawHeader("Accept", "application/json");

    running_ = true;
    connect(server_, &QTcpServer::newConnection, this, &SunoOAuthLoopback::onNewConnection);

    registerReply_ =
            postBounded(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    if (!registerReply_) {
        return false; // finishWithError already ran and reported the reason
    }
    connect(registerReply_, &QNetworkReply::finished, this, [this, reply = registerReply_]() {
        if (const auto it = readers_.find(reply); it != readers_.end()) {
            (void)it->pump(*reply); // final drain; the cap still applies
        }
        onRegisterFinished();
        reply->deleteLater();
    });

    timeout_ = new QTimer(this);
    timeout_->setSingleShot(true);
    connect(timeout_, &QTimer::timeout, this, [this]() {
        finishWithError(QStringLiteral("timed out waiting for the browser sign-in"));
    });
    timeout_->start(kLoopbackTimeoutMs);

    LOG_INFO("SunoOAuthLoopback: registering client for {}", redirectUri_.toStdString());
    return true;
}

QNetworkReply* SunoOAuthLoopback::postBounded(const QNetworkRequest& request,
                                              const QByteArray& body)
{
    auto reader = http::makeBodyReader(http::RequestClass::Auth,
                                       http::maxResponseBytes(http::RequestClass::Auth));
    if (!reader) {
        // Unreachable with the current constant cap, but a body we cannot bound
        // must not be requested at all.
        finishWithError(http::describePolicyFailureText(reader.error()));
        return nullptr;
    }
    QNetworkReply* reply = nam_->post(request, body);
    readers_.insert(reply, *reader);
    // Drain on readyRead, not at finished(): that is the difference between a
    // cap and a post-hoc check. An origin streaming forever is aborted one byte
    // past the cap, so the bytes never arrive.
    connect(reply, &QNetworkReply::readyRead, this, [this, reply]() {
        const auto it = readers_.find(reply);
        if (it == readers_.end()) {
            return;
        }
        if (auto pumped = it->pump(*reply); !pumped) {
            LOG_ERROR("SunoOAuthLoopback: {}", http::describePolicyFailure(pumped.error()));
            reply->abort();
        }
    });
    return reply;
}

void SunoOAuthLoopback::onRegisterFinished(){
    QNetworkReply* reply = registerReply_;
    registerReply_ = nullptr;
    if (!running_ || !reply) {
        return;
    }

    auto bounded = readers_.take(reply);
    if (bounded.failure()) {
        finishWithError(http::describePolicyFailureText(*bounded.failure()));
        return;
    }
    const QByteArray body = bounded.body();
    if (reply->error() != QNetworkReply::NoError) {
        finishWithError(QStringLiteral("client registration could not be reached"));
        return;
    }

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status != 200 && status != 201) {
        finishWithError(QStringLiteral("client registration was refused (HTTP %1)").arg(status));
        return;
    }

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        finishWithError(QStringLiteral("client registration answered an unreadable body"));
        return;
    }
    clientId_ = document.object().value(QStringLiteral("client_id")).toString();
    if (clientId_.isEmpty()) {
        finishWithError(QStringLiteral("client registration answered no client id"));
        return;
    }

    codeVerifier_ = makeCodeVerifier();
    const QUrl authorizeUrl = buildAuthorizeUrl(endpoints_.authorization, config_, clientId_,
                                                redirectUri_, state_,
                                                pkceChallenge(codeVerifier_));
    emit authorizationUrlReady(authorizeUrl);
    if (!launcher_(authorizeUrl)) {
        finishWithError(QStringLiteral("could not open the browser for sign-in"));
        return;
    }
    LOG_INFO("SunoOAuthLoopback: client {} — waiting on the loopback callback",
             clientId_.toStdString());
}

void SunoOAuthLoopback::onNewConnection()
{
    while (QTcpSocket* socket = server_->nextPendingConnection()) {
        connect(socket, &QTcpSocket::readyRead, this,
                [this, socket]() { onSocketReadyRead(socket); });
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        // A client that connects and sends nothing must not hold the run open.
        QTimer::singleShot(10'000, socket, [socket]() {
            if (socket->state() == QAbstractSocket::ConnectedState) {
                socket->abort();
            }
        });
    }
}

void SunoOAuthLoopback::onSocketReadyRead(QTcpSocket* socket)
{
    // One request per connection; buffer is bounded because a hostile local
    // process could otherwise stream a head forever.
    QByteArray buffer = socket->property("received").toByteArray();
    buffer.append(socket->readAll());
    socket->setProperty("received", buffer);
    if (buffer.size() > kMaxRequestHeadBytes) {
        writeHtml(socket, "HTTP/1.1 413 Payload Too Large", kBadRequestPage);
        return;
    }
    if (!buffer.contains("\r\n\r\n") && !buffer.endsWith('\n')) {
        return; // head not complete yet
    }

    const std::optional<QByteArray> target = parseRequestTarget(buffer);
    if (!target) {
        writeHtml(socket, "HTTP/1.1 400 Bad Request", kBadRequestPage);
        return;
    }
    if (!target->startsWith("/callback")) {
        // A browser may fetch /favicon.ico after the redirect; answer politely
        // and keep listening for the real callback.
        writeHtml(socket, "HTTP/1.1 404 Not Found", kBadRequestPage);
        return;
    }
    const bool accepted = handleCallback(*target);
    writeHtml(socket, accepted ? "HTTP/1.1 200 OK" : "HTTP/1.1 400 Bad Request",
              accepted ? kOkPage : kBadRequestPage);
}

bool SunoOAuthLoopback::handleCallback(const QByteArray& target)
{
    if (exchangeStarted_) {
        return false;
    }
    const qsizetype queryStart = target.indexOf('?');
    const QByteArray query =
            queryStart < 0 ? QByteArray{} : target.mid(queryStart + 1);

    // state first: a code for a run we did not start is not ours to exchange.
    const std::optional<QString> returnedState = queryValue(query, "state");
    if (!returnedState || *returnedState != state_) {
        finishWithError(QStringLiteral("the sign-in callback did not match this run (state)"));
        return false;
    }

    if (const std::optional<QString> error = queryValue(query, "error")) {
        finishWithError(QStringLiteral("sign-in was refused (%1)").arg(*error));
        return false;
    }
    const std::optional<QString> code = queryValue(query, "code");
    if (!code || code->isEmpty()) {
        finishWithError(QStringLiteral("the sign-in callback carried no code"));
        return false;
    }
    exchangeStarted_ = true;
    exchangeCode(*code);
    return true;
}

void SunoOAuthLoopback::exchangeCode(const QString& code)
{
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("authorization_code"));
    form.addQueryItem(QStringLiteral("code"), code);
    form.addQueryItem(QStringLiteral("client_id"), clientId_);
    form.addQueryItem(QStringLiteral("redirect_uri"), redirectUri_);
    form.addQueryItem(QStringLiteral("code_verifier"), codeVerifier_);
    form.addQueryItem(QStringLiteral("resource"), config_.resource);

    QNetworkRequest request(endpoints_.token);
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/x-www-form-urlencoded"));
    http::applyRequestPolicy(request, http::RequestClass::Auth);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    request.setRawHeader("User-Agent", kBrowserUserAgent);
    request.setRawHeader("Accept", "application/json");

    QNetworkReply* reply = postBounded(request, form.toString(QUrl::FullyEncoded).toUtf8());
    if (!reply) {
        return; // finishWithError already reported the unusable reader
    }
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (!running_) {
            return;
        }
        if (const auto it = readers_.find(reply); it != readers_.end()) {
            (void)it->pump(*reply); // final drain; the cap still applies
        }
        auto bounded = readers_.take(reply);
        if (bounded.failure()) {
            finishWithError(http::describePolicyFailureText(*bounded.failure()));
            return;
        }
        const QByteArray body = bounded.body();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QJsonParseError parseError{};
        const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
        const QJsonObject object =
                (parseError.error == QJsonParseError::NoError && document.isObject())
                        ? document.object()
                        : QJsonObject{};

        if (reply->error() != QNetworkReply::NoError || status < 200 || status >= 300) {
            // RFC 6749 §5.2: `error` is a machine code and safe to surface;
            // the description is not, because a proxy may echo the request.
            const QString code = object.value(QStringLiteral("error")).toString();
            finishWithError(code.isEmpty()
                                    ? QStringLiteral("the token exchange was refused (HTTP %1)").arg(status)
                                    : QStringLiteral("the token exchange was refused (%1)").arg(code));
            return;
        }

        TokenSet tokens;
        tokens.accessToken = object.value(QStringLiteral("access_token")).toString();
        tokens.refreshToken = object.value(QStringLiteral("refresh_token")).toString();
        tokens.tokenType = object.value(QStringLiteral("token_type")).toString();
        tokens.scope = object.value(QStringLiteral("scope")).toString();
        tokens.expiresInSeconds =
                static_cast<qint64>(object.value(QStringLiteral("expires_in")).toDouble());
        if (tokens.isEmpty()) {
            finishWithError(QStringLiteral("the token exchange answered no access token"));
            return;
        }

        // Never log the token itself: presence and lifetime only.
        LOG_INFO("SunoOAuthLoopback: authorized (access token present, refresh {}, {} s)",
                 tokens.refreshToken.isEmpty() ? "absent" : "present",
                 tokens.expiresInSeconds);
        running_ = false;
        cleanup();
        emit authorized(tokens);
    });
}

void SunoOAuthLoopback::finishWithError(const QString& safeError)
{
    if (!running_) {
        return;
    }
    running_ = false;
    cleanup();
    LOG_WARN("SunoOAuthLoopback: {}", safeError.toStdString());
    emit failed(safeError);
}

void SunoOAuthLoopback::cleanup()
{
    readers_.clear();
    if (timeout_) {
        timeout_->stop();
        timeout_->deleteLater();
        timeout_ = nullptr;
    }
    if (registerReply_) {
        registerReply_->abort();
        registerReply_ = nullptr;
    }
    if (server_) {
        server_->close();
        server_->deleteLater();
        server_ = nullptr;
    }
    exchangeStarted_ = false;
}

void SunoOAuthLoopback::cancel()
{
    running_ = false;
    cleanup();
}

} // namespace vc::suno::auth
