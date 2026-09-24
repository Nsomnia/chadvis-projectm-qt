#include "AuthCoordinator.hpp"

#include "CredentialStore.hpp"
#include "oauth/OAuthLoginService.hpp"
#include "suno/SunoClient.hpp"

#include <QDesktopServices>
#include <QUrl>

#include <exception>
#include <utility>

namespace vc::suno::auth {
namespace {

bool usableLaunch(const oauth::CaptureApprovedLaunch& launch) {
    const QUrl& url = launch.authorizationUrl;
    return url.isValid() && !url.host().isEmpty() &&
           url.scheme().compare(QLatin1String("https"), Qt::CaseInsensitive) == 0 &&
           url.userInfo().isEmpty() && !url.hasFragment() &&
           launch.loopbackPath.startsWith(QLatin1Char('/'));
}

AuthCoordinator::Dependencies defaultDependencies(vc::suno::SunoClient* client) {
    AuthCoordinator::Dependencies dependencies;
    dependencies.loginService = std::make_unique<oauth::OAuthLoginService>(
        [](const QUrl& url) { return QDesktopServices::openUrl(url); });
    dependencies.clearLocalCredentials = [client] {
        if (client) client->clearLocalCredentials();
    };
    dependencies.secureBackendAvailable = [] {
        const CredentialStore store;
        return store.isSecureBackend();
    };
    return dependencies;
}

} // namespace

AuthCoordinator::AuthCoordinator(vc::suno::SunoClient* client, QObject* parent)
    : AuthCoordinator(client, defaultDependencies(client), parent) {}

AuthCoordinator::AuthCoordinator(vc::suno::SunoClient* client, Dependencies dependencies,
                                 QObject* parent)
    : QObject(parent),
      client_(client),
      service_(std::move(dependencies.loginService)),
      installCredentials_(std::move(dependencies.installCredentials)),
      clearLocalCredentials_(std::move(dependencies.clearLocalCredentials)),
      secureBackendAvailable_(std::move(dependencies.secureBackendAvailable)) {
    if (!service_) {
        service_ = std::make_unique<oauth::OAuthLoginService>(
            [](const QUrl& url) { return QDesktopServices::openUrl(url); });
    }
    if (!clearLocalCredentials_) {
        clearLocalCredentials_ = [client] {
            if (client) client->clearLocalCredentials();
        };
    }
    if (!secureBackendAvailable_) {
        secureBackendAvailable_ = [] {
            const CredentialStore store;
            return store.isSecureBackend();
        };
    }

    connect(service_.get(), &oauth::OAuthLoginService::stateChanged, this,
            [this](oauth::State state) {
                state_ = oauth::stateToString(state);
                emit stateChanged(state_);
            });
    connect(service_.get(), &oauth::OAuthLoginService::failed, this,
            [this](const QString& safeMessage) {
                error_ = safeMessage;
                emit errorChanged(error_);
            });
    connect(service_.get(), &oauth::OAuthLoginService::callbackReceived, this,
            [this] {
                emit callbackReceived();
                if (callbackInstallationClaimed_) return;
                callbackInstallationClaimed_ = true;
                if (!installCredentials_) return;
                try {
                    if (installCredentials_ && installCredentials_()) {
                        state_ = oauth::stateToString(oauth::State::Authenticated);
                        emit stateChanged(state_);
                    } else {
                        setPolicyError(QStringLiteral("Unable to install captured credentials"));
                    }
                } catch (const std::exception&) {
                    setPolicyError(QStringLiteral("Unable to install captured credentials"));
                } catch (...) {
                    setPolicyError(QStringLiteral("Unable to install captured credentials"));
                }
            });
}

bool AuthCoordinator::isSecureBackendAvailable() const {
    return secureBackendAvailable_ && secureBackendAvailable_();
}

bool AuthCoordinator::beginGoogleSignIn(QString* safeError) {
    error_.clear();
    if (!isSecureBackendAvailable()) {
        setPolicyError(
            QStringLiteral("Secure credential storage is required before desktop sign-in can start"));
        if (safeError) *safeError = error_;
        return false;
    }
    if (!launch_ || !usableLaunch(*launch_)) {
        setPolicyError(QStringLiteral("Desktop sign-in is not enabled in this build"));
        if (safeError) *safeError = error_;
        return false;
    }

    callbackInstallationClaimed_ = false;
    service_->begin(*launch_);
    if (state_ == oauth::stateToString(oauth::State::Error) && safeError) {
        *safeError = error_;
    }
    return state_ != oauth::stateToString(oauth::State::Error);
}

void AuthCoordinator::cancelGoogleSignIn() {
    service_->cancel();
}

void AuthCoordinator::signOutSuno() {
    if (clearLocalCredentials_) clearLocalCredentials_();
}

bool AuthCoordinator::googleLoginAvailable() const {
    return isSecureBackendAvailable() && launch_ && usableLaunch(*launch_);
}

void AuthCoordinator::setCaptureApprovedLaunch(oauth::CaptureApprovedLaunch launch) {
    launch_ = std::move(launch);
}

void AuthCoordinator::clearCaptureApprovedLaunch() {
    launch_.reset();
}

void AuthCoordinator::setPolicyError(const QString& safeMessage) {
    error_ = safeMessage;
    state_ = oauth::stateToString(oauth::State::Error);
    emit stateChanged(state_);
    emit errorChanged(error_);
}

} // namespace vc::suno::auth
