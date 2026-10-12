#pragma once

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
        std::function<bool()> installCredentials;
        std::function<void()> clearLocalCredentials;
        std::function<bool()> secureBackendAvailable;
    };

    explicit AuthCoordinator(vc::suno::SunoClient* client, QObject* parent = nullptr);
    AuthCoordinator(vc::suno::SunoClient* client, Dependencies dependencies,
                    QObject* parent = nullptr);
    ~AuthCoordinator() override = default;

    [[nodiscard]] bool isSecureBackendAvailable() const;
    /// Open the system browser on Suno's sign-in page (T0181).
    ///
    /// This is the whole of "sign in with Google" now: Clerk owns the OAuth
    /// exchange server-side, so the client's only job is to get the user to a
    /// browser. Returns false when a browser could not be launched, with
    /// `safeError` naming why. It NEVER waits for a callback -- the credential
    /// arrives afterwards through installCredentials(), which is the paste
    /// path, and that is a property of the flow rather than a fallback.
    bool beginGoogleSignIn(QString* safeError = nullptr);
    void signOutSuno();

    [[nodiscard]] QString googleLoginState() const { return state_; }
    [[nodiscard]] QString googleLoginError() const { return error_; }
    [[nodiscard]] bool googleLoginAvailable() const;

signals:
    void stateChanged(QString state);
    void errorChanged(QString error);

private:
    void setPolicyError(const QString& safeMessage);

    vc::suno::SunoClient* client_;
    std::function<bool()> installCredentials_;
    std::function<void()> clearLocalCredentials_;
    std::function<bool()> secureBackendAvailable_;
    QString state_ = QStringLiteral("signedOut");
    QString error_;
    bool callbackInstallationClaimed_ = false;
};

} // namespace vc::suno::auth
