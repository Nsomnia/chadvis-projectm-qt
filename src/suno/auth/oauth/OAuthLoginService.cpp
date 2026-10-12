#include "OAuthLoginService.hpp"

#include <QUrlQuery>

#include <algorithm>
#include <exception>
#include <utility>

namespace vc::suno::auth::oauth {
namespace {

QDateTime systemUtcNow() {
    return QDateTime::currentDateTimeUtc();
}

OAuthLoginService::TransactionFactory systemTransactionFactory() {
    return [](const QString& path, quint16 port, const QDateTime& deadline) {
        return OAuthTransaction::make(path, port, deadline);
    };
}

bool constantTimeEquals(const QString& lhs, const QString& rhs) {
    const QByteArray left = lhs.toUtf8();
    const QByteArray right = rhs.toUtf8();
    const qsizetype count = std::max(left.size(), right.size());
    quint8 difference = static_cast<quint8>(left.size() ^ right.size());
    for (qsizetype i = 0; i < count; ++i) {
        const auto a = i < left.size() ? static_cast<unsigned char>(left.at(i)) : 0;
        const auto b = i < right.size() ? static_cast<unsigned char>(right.at(i)) : 0;
        difference = static_cast<quint8>(difference | (a ^ b));
    }
    return difference == 0;
}

bool callbackState(const QUrl& url, QString* state) {
    const QUrlQuery query(url.query(QUrl::FullyEncoded));
    bool found = false;
    for (const auto& item : query.queryItems(QUrl::FullyDecoded)) {
        if (item.first != QLatin1String("state")) continue;
        if (found) return false;
        *state = item.second;
        found = true;
    }
    return found;
}

/// T0166. A single query key, read once, duplicates refused. OAuth's own rules
/// are a MUST NOT on parameter repetition (RFC 6749 3.1), so two `code` values
/// are an ambiguity rather than something to pick a winner from.
bool callbackParam(const QUrl& url, QLatin1StringView key, QString* value) {
    const QUrlQuery query(url.query(QUrl::FullyEncoded));
    bool found = false;
    for (const auto& item : query.queryItems(QUrl::FullyDecoded)) {
        if (item.first != key) continue;
        if (found) return false;
        *value = item.second;
        found = true;
    }
    return found;
}

/// OAuth error codes are a fixed registry, not free text (RFC 6749 4.1.2.1).
/// Anything else is an IdP extension we do not model, so it is passed through
/// verbatim rather than silently mapped to a generic message that would hide
/// which one occurred.
[[nodiscard]] bool knownOAuthErrorCode(QLatin1StringView code) {
    return code == QLatin1String("invalid_request") || code == QLatin1String("unauthorized_client") ||
           code == QLatin1String("access_denied") || code == QLatin1String("unsupported_response_type") ||
           code == QLatin1String("invalid_scope") || code == QLatin1String("server_error") ||
           code == QLatin1String("temporarily_unavailable");
}

/// T0166. Maps an IdP error to a message that is safe to show and to log.
///
/// `error_description` is attacker-influenced text: the callback comes off a
/// socket any local process can connect to, so echoing it verbatim would put
/// arbitrary strings into the UI and into the log. Only recognised OAuth error
/// codes are rendered, and the registry is a fixed set -- so what reaches the
/// user is always a string this file chose.
[[nodiscard]] QString safeOAuthErrorMessage(QLatin1StringView code) {
    if (!knownOAuthErrorCode(code)) {
        return QStringLiteral("The sign-in provider reported an unrecognised error");
    }
    if (code == QLatin1String("access_denied")) {
        return QStringLiteral("Sign-in was declined");
    }
    if (code == QLatin1String("temporarily_unavailable") ||
        code == QLatin1String("server_error")) {
        return QStringLiteral("The sign-in provider is temporarily unavailable");
    }
    return QStringLiteral("The sign-in provider rejected the request");
}

/// T0166. Whether a successful authorization response carried a usable code.
///
/// RFC 6749 4.1.2: the success and failure arms are mutually exclusive and both
/// are a single query parameter. So `error` wins if present -- an attacker who
/// can inject `error` into an otherwise valid callback should not be able to
/// ride along by omitting `code` -- and success requires exactly one non-empty
/// `code`.
[[nodiscard]] bool hasUsableAuthorizationCode(const QUrl& url) {
    QString errorCode;
    if (callbackParam(url, QLatin1String("error"), &errorCode)) return false;
    QString code;
    if (!callbackParam(url, QLatin1String("code"), &code)) return false;
    if (code.isEmpty()) return false;
    // Bound the value the same way the state is bounded. The URL itself is
    // capped by the listener, so this is belt and braces against a code that
    // is legitimately short enough to matter and absurdly long.
    return code.size() <= 2048;
}

} // namespace

QString stateToString(State state) {
    switch (state) {
    case State::SignedOut:
        return QStringLiteral("signedOut");
    case State::BrowserOpen:
        return QStringLiteral("browserOpen");
    case State::CallbackReceived:
        return QStringLiteral("callbackReceived");
    case State::Authenticated:
        return QStringLiteral("authenticated");
    case State::Error:
        return QStringLiteral("error");
    case State::Cancelled:
        return QStringLiteral("cancelled");
    }
    return QStringLiteral("error");
}

OAuthLoginService::OAuthLoginService(OpenBrowser openBrowser, QObject* parent)
    : OAuthLoginService(std::move(openBrowser), systemUtcNow, systemTransactionFactory(),
                        parent) {}

OAuthLoginService::OAuthLoginService(OpenBrowser openBrowser, Clock clock,
                                     TransactionFactory transactionFactory, QObject* parent)
    : QObject(parent),
      openBrowser_(std::move(openBrowser)),
      clock_(std::move(clock)),
      transactionFactory_(std::move(transactionFactory)),
      listener_(new LoopbackListener(this)),
      deadlineTimer_(new QTimer(this)) {
    deadlineTimer_->setSingleShot(true);
    deadlineTimer_->setInterval(kLoginTimeoutMs);
    connect(deadlineTimer_, &QTimer::timeout, this,
            [this] { fail(QStringLiteral("Sign-in timed out")); });
    connect(listener_, &LoopbackListener::requestReceived, this,
            &OAuthLoginService::acceptCallback);
    connect(listener_, &LoopbackListener::failed, this, &OAuthLoginService::fail);
}

void OAuthLoginService::begin(const CaptureApprovedLaunch& launch) {
    if (transaction_) {
        emit failed(QStringLiteral("Sign-in is already in progress"));
        return;
    }
    if (!usableLaunch(launch)) {
        fail(QStringLiteral("Desktop sign-in is not enabled in this build"));
        return;
    }

    stopTransport();
    const QDateTime now = clock_ ? clock_() : systemUtcNow();
    if (!now.isValid()) {
        fail(QStringLiteral("Unable to start desktop sign-in"));
        return;
    }

    auto started = listener_->start(launch.loopbackPath);
    if (!started) {
        fail(QStringLiteral("Unable to start local sign-in listener"));
        return;
    }

    auto transaction = transactionFactory_(
        launch.loopbackPath, listener_->port(), now.addMSecs(kLoginTimeoutMs));
    if (!transaction) {
        listener_->stop();
        fail(QStringLiteral("Unable to create secure sign-in transaction"));
        return;
    }

    transaction_ = std::move(*transaction);
    deadlineTimer_->start();
    setState(State::BrowserOpen);

    bool opened = false;
    try {
        opened = openBrowser_ && openBrowser_(launch.authorizationUrl);
    } catch (const std::exception&) {
        opened = false;
    } catch (...) {
        opened = false;
    }
    if (!opened) {
        fail(QStringLiteral("Unable to open the sign-in page"));
        return;
    }
    if (state_ == State::BrowserOpen) emit browserOpened();
}

void OAuthLoginService::acceptCallback(const QUrl& callbackUrl) {
    if (!transaction_) {
        fail(QStringLiteral("Sign-in callback is invalid or has expired"));
        return;
    }

    const OAuthTransaction& active = *transaction_;
    QString suppliedState;
    const bool hasState = callbackState(callbackUrl, &suppliedState);
    const bool pathMatches = callbackUrl.isValid() &&
        callbackUrl.path(QUrl::FullyEncoded) == active.expectedPath;
    const bool withinUrlCap =
        callbackUrl.toEncoded(QUrl::FullyEncoded).size() <= LoopbackListener::kMaxUrlLengthBytes;
    const QDateTime now = clock_ ? clock_() : systemUtcNow();
    const bool beforeDeadline = now.isValid() && now <= active.deadlineUtc;
    if (!pathMatches || !withinUrlCap || !hasState || suppliedState.size() > 256 ||
        !constantTimeEquals(active.state, suppliedState) || !beforeDeadline) {
        fail(QStringLiteral("Sign-in callback is invalid or has expired"));
        return;
    }

    // T0166. An IdP reports failure by redirecting to the SAME loopback path
    // with the SAME valid state and an `error` parameter instead of a `code`
    // (RFC 6749 4.1.2.1). Everything validated above therefore passes on a
    // failure response, so without this the transaction reached
    // State::CallbackReceived and the coordinator ran installCredentials_ on
    // an exchange that had no code to exchange -- `?error=access_denied`
    // sign-in "succeeded".
    //
    // Checked before the ownership transfer, so a rejected callback leaves no
    // half-consumed transaction behind and fail() resets cleanly.
    if (!hasUsableAuthorizationCode(callbackUrl)) {
        QString errorCode;
        if (callbackParam(callbackUrl, QLatin1String("error"), &errorCode) &&
            !errorCode.isEmpty()) {
            // A recognised code gets its own message; an unknown or empty one
            // gets the generic refusal, because an IdP extension we do not
            // model must not become a string we echo back to the user.
            fail(safeOAuthErrorMessage(QLatin1StringView(errorCode.toLatin1())));
            return;
        }
        // No error, no usable code: a truncated or malformed success response.
        // Treated as invalid rather than reported as a provider fault, because
        // that is what it is.
        fail(QStringLiteral("Sign-in callback is invalid or has expired"));
        return;
    }

    // Single ownership transfer: a re-entrant/replayed callback sees no transaction.
    auto consumed = std::exchange(transaction_, std::nullopt);
    Q_UNUSED(consumed);
    deadlineTimer_->stop();
    listener_->stop();
    setState(State::CallbackReceived);
    emit callbackReceived();
}

void OAuthLoginService::cancel() {
    if (state_ == State::CallbackReceived || state_ == State::Authenticated) return;
    if (!transaction_) return;

    transaction_.reset();
    stopTransport();
    setState(State::Cancelled);
    emit cancelled();
}

void OAuthLoginService::setState(State state) {
    if (state_ == state) return;
    state_ = state;
    emit stateChanged(state_);
}

void OAuthLoginService::fail(const QString& safeMessage) {
    transaction_.reset();
    stopTransport();
    setState(State::Error);
    emit failed(safeMessage);
}

void OAuthLoginService::stopTransport() {
    deadlineTimer_->stop();
    listener_->stop();
}

bool OAuthLoginService::usableLaunch(const CaptureApprovedLaunch& launch) const {
    const QUrl& url = launch.authorizationUrl;
    const bool urlUsable = url.isValid() && !url.host().isEmpty() &&
        url.scheme().compare(QLatin1String("https"), Qt::CaseInsensitive) == 0 &&
        url.userInfo().isEmpty() && !url.hasFragment();
    const bool pathUsable = !launch.loopbackPath.isEmpty() &&
        launch.loopbackPath.startsWith(QLatin1Char('/'));
    return urlUsable && pathUsable;
}

} // namespace vc::suno::auth::oauth
