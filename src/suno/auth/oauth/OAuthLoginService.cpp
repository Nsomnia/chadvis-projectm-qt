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
