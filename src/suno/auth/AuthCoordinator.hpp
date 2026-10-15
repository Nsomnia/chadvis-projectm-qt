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
    /// Import the Suno session from a local browser's cookie store (T0199).
    ///
    /// The complement to beginGoogleSignIn(), and the half that was missing:
    /// opening the browser could never return a credential, because Clerk
    /// delivers the code server-side. For a user who is already signed in,
    /// the durable credential (`__client`, ~1 year, HttpOnly) is sitting in
    /// the browser's on-disk store the entire time. This reads the Clerk
    /// cookie family for suno.com from it and hands the header to
    /// SunoClient::setCookie() -- the same call the paste path makes, which
    /// persists through CredentialStore and starts a bearer fetch.
    ///
    /// Returns false with `safeError` when no browser had a session.
    /// Reading a browser's Safe Storage secret prompts for permission on
    /// first use, which is why this exists as an explicit user action and
    /// is never called on startup.
    bool importSessionFromBrowser(QString* safeError = nullptr);
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
