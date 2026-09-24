#pragma once

#include "oauth/OAuthLoginService.hpp"

#include <QObject>
#include <QString>

#include <functional>
#include <memory>
#include <optional>

namespace vc::suno {
class SunoClient;
}

namespace vc::suno::auth {

/// Capture-gated policy between the desktop login scaffold and SunoClient.
class AuthCoordinator : public QObject {
    Q_OBJECT

public:
    struct Dependencies {
        std::unique_ptr<oauth::OAuthLoginService> loginService;
        std::function<bool()> installCredentials;
        std::function<void()> clearLocalCredentials;
        std::function<bool()> secureBackendAvailable;
    };

    explicit AuthCoordinator(vc::suno::SunoClient* client, QObject* parent = nullptr);
    AuthCoordinator(vc::suno::SunoClient* client, Dependencies dependencies,
                    QObject* parent = nullptr);
    ~AuthCoordinator() override = default;

    [[nodiscard]] bool isSecureBackendAvailable() const;
    bool beginGoogleSignIn(QString* safeError = nullptr);
    void cancelGoogleSignIn();
    void signOutSuno();

    [[nodiscard]] QString googleLoginState() const { return state_; }
    [[nodiscard]] QString googleLoginError() const { return error_; }
    [[nodiscard]] bool googleLoginAvailable() const;

    /// A future capture-integration lane is the only intended caller.
    void setCaptureApprovedLaunch(oauth::CaptureApprovedLaunch launch);
    void clearCaptureApprovedLaunch();

signals:
    void stateChanged(QString state);
    void errorChanged(QString error);
    void callbackReceived();

private:
    void setPolicyError(const QString& safeMessage);

    vc::suno::SunoClient* client_;
    std::unique_ptr<oauth::OAuthLoginService> service_;
    std::function<bool()> installCredentials_;
    std::function<void()> clearLocalCredentials_;
    std::function<bool()> secureBackendAvailable_;
    std::optional<oauth::CaptureApprovedLaunch> launch_;
    QString state_ = QStringLiteral("signedOut");
    QString error_;
    bool callbackInstallationClaimed_ = false;
};

} // namespace vc::suno::auth
