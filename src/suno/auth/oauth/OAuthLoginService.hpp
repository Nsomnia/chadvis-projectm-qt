#pragma once

#include "LoopbackListener.hpp"
#include "OAuthTransaction.hpp"

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QUrl>

#include <expected>
#include <functional>
#include <optional>

namespace vc::suno::auth::oauth {

enum class State {
    SignedOut,
    BrowserOpen,
    CallbackReceived,
    Authenticated,
    Error,
    Cancelled,
};

[[nodiscard]] QString stateToString(State state);

/// A launch may be constructed only from a future human-approved capture.
/// `loopbackPath` remains unavailable in production until that capture exists.
struct CaptureApprovedLaunch {
    QUrl authorizationUrl;
    QString loopbackPath;
};

/// Owns one callback transaction. It performs no credential exchange.
class OAuthLoginService : public QObject {
    Q_OBJECT

public:
    using OpenBrowser = std::function<bool(const QUrl&)>;
    using Clock = std::function<QDateTime()>;
    using TransactionFactory = std::function<std::expected<OAuthTransaction, QString>(
        const QString&, quint16, const QDateTime&)>;

    static constexpr int kLoginTimeoutMs = 7 * 60 * 1000;

    explicit OAuthLoginService(OpenBrowser openBrowser, QObject* parent = nullptr);
    OAuthLoginService(OpenBrowser openBrowser, Clock clock, TransactionFactory transactionFactory,
                      QObject* parent = nullptr);
    ~OAuthLoginService() override = default;

    void begin(const CaptureApprovedLaunch& launch);
    void acceptCallback(const QUrl& callbackUrl);
    void cancel();

    [[nodiscard]] State state() const { return state_; }

signals:
    void browserOpened();
    void callbackReceived();
    void stateChanged(State state);
    void failed(QString safeMessage);
    void cancelled();

private:
    void setState(State state);
    void fail(const QString& safeMessage);
    void stopTransport();
    [[nodiscard]] bool usableLaunch(const CaptureApprovedLaunch& launch) const;

    OpenBrowser openBrowser_;
    Clock clock_;
    TransactionFactory transactionFactory_;
    LoopbackListener* listener_;
    QTimer* deadlineTimer_;
    std::optional<OAuthTransaction> transaction_;
    State state_ = State::SignedOut;
};

} // namespace vc::suno::auth::oauth
