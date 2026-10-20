#include "AuthCoordinator.hpp"

#include "BrowserCookieImport.hpp"
#include "CredentialStore.hpp"
#include "suno/SunoClient.hpp"

#include <QDesktopServices>
#include <QUrl>

#include <exception>
#include <utility>

namespace vc::suno::auth {
namespace {
/// The URL the system browser is opened on (T0181).
///
/// Derived from the captured flow, not invented: `LOGIN-CAPTURE-FINALIZATION.md`
/// records `POST /v1/client/sign_ins` with
/// `redirect_url=https://suno.com/sso-callback?...` and the browser chain
/// landing on `suno.com/create`, so `/create` with `redirected_from=signin` is
/// the entry point a returning user sees the Google button on. The page itself
/// is where Clerk's server-side OAuth begins.
///
/// https only, and the fragment is rejected because a fragment never reaches
/// the server -- it would only confuse a user who pasted one.
QUrl signInPageUrl() {
    return QUrl(QStringLiteral("https://suno.com/create?redirected_from=signin"));
}

/// Validate a URL we are about to hand to the user's browser.
///
/// https, a host, and nothing that could carry credentials or a fragment. The
/// userinfo check is the important one: a URL with `user@` in it is a phishing
/// shape and this program is about to open it in the user's real browser on
/// their real session, so it gets refused rather than passed through.
bool isSafeBrowserTarget(const QUrl& url) {
    return url.isValid() && !url.host().isEmpty() &&
           url.scheme().compare(QLatin1String("https"), Qt::CaseInsensitive) == 0 &&
           url.userInfo().isEmpty() && !url.hasFragment();
}

AuthCoordinator::Dependencies defaultDependencies(vc::suno::SunoClient* client) {
    AuthCoordinator::Dependencies dependencies;
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
      installCredentials_(std::move(dependencies.installCredentials)),
      clearLocalCredentials_(std::move(dependencies.clearLocalCredentials)),
      secureBackendAvailable_(std::move(dependencies.secureBackendAvailable)),
      launchBrowser_(std::move(dependencies.launchBrowser)) {
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
    if (!launchBrowser_) {
        launchBrowser_ = [](const QUrl& url) { return QDesktopServices::openUrl(url); };
    }
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

    // T0181. Open the browser. That is the entire client-side half of Google
    // sign-in, and it is deliberately the last thing this function does: the
    // credential is NOT obtained here, and cannot be.
    //
    // The previous implementation gated on a `CaptureApprovedLaunch` whose
    // `loopbackPath` could never be populated, because Clerk's flow sends the
    // OAuth `code` to `auth.suno.com/social/complete/` and never to 127.0.0.1
    // (LOGIN-CAPTURE-FINALIZATION.md: "No `127.0.0.1` address appears anywhere
    // in the 1379-entry HAR"). The gate therefore failed 100% of the time and
    // the feature was unreachable with no diagnostic naming why.
    //
    // The session arrives afterwards through installCredentials_ -- the paste
    // path -- which is a property of a server-side-callback flow rather than a
    // degraded mode, and the UI now says so.
    const QUrl target = signInPageUrl();
    if (!isSafeBrowserTarget(target)) {
        // Unreachable for a compile-time constant, and kept because the
        // constant is not the only thing that can reach this function.
        setPolicyError(QStringLiteral("The sign-in page address was refused"));
        if (safeError) *safeError = error_;
        return false;
    }
    if (!launchBrowser_(target)) {
        setPolicyError(QStringLiteral("Could not open your web browser"));
        if (safeError) *safeError = error_;
        return false;
    }

    state_ = QStringLiteral("browserOpen");
    emit stateChanged(state_);
    return true;
}

void AuthCoordinator::cancelGoogleSignIn() {
    // Nothing is running and nothing is owned by this process -- the browser
    // is the user's own. A cancel is therefore only a state reset, and it is
    // valid from any state: "browserOpen" (the abandoned wait), "error" (a
    // dismissed failure) and even success (a user who immediately signs out).
    // Clearing error_ here is what lets the same card be reused for a fresh
    // attempt without showing yesterday's verdict.
    state_ = QStringLiteral("signedOut");
    error_.clear();
    emit stateChanged(state_);
    emit errorChanged(error_);
}

void AuthCoordinator::signOutSuno() {
    // State first: if clearing the credential trips a NeedsReauth/Disconnect
    // transition that the controller feeds back into this coordinator, the
    // reset below must already have happened, not be overwritten by it.
    state_ = QStringLiteral("signedOut");
    error_.clear();
    emit stateChanged(state_);
    emit errorChanged(error_);
    if (clearLocalCredentials_) clearLocalCredentials_();
}

void AuthCoordinator::noteVerificationFailed() {
    if (state_ != QStringLiteral("browserImported")) {
        return;
    }
    setPolicyError(QStringLiteral(
        "The imported session was not accepted by Suno. Sign in in your browser, then press Import again."));
}

bool AuthCoordinator::googleLoginAvailable() const {
    // No longer gated on a capture-approved launch: there is nothing to
    // capture. A secure backend to store the credential is the whole
    // precondition, and it is the one that actually protects the user's
    // session credential.
    return isSecureBackendAvailable();
}

bool AuthCoordinator::importSessionFromBrowser(QString* safeError) {
    error_.clear();

    // T0199. The paste path's own precondition applies here unchanged: a
    // harvested cookie is the same secret a pasted one is, and storing it in
    // the plaintext fallback would defeat the reason CredentialStore exists.
    if (!isSecureBackendAvailable()) {
        setPolicyError(
            QStringLiteral("Secure credential storage is required before a browser session can be imported"));
        if (safeError) *safeError = error_;
        return false;
    }

    const auto harvest = BrowserCookieImport::harvest();
    if (harvest.cookieHeader.isEmpty()) {
        setPolicyError(harvest.reason.isEmpty()
                           ? QStringLiteral("No Suno session was found in any installed browser")
                           : harvest.reason);
        if (safeError) *safeError = error_;
        return false;
    }

    // The durability bar, restated at the handover: harvest() already
    // gatekeeps this, but the header crosses a module boundary here and the
    // failure of the first shipped build was precisely a credential-less
    // header sailing through. Names only — this never logs or stores the
    // header itself.
    if (!BrowserCookieImport::headerCarriesUsableCredential(harvest.cookieHeader)) {
        setPolicyError(QStringLiteral(
            "The session that was read carried no usable Suno credential"));
        if (safeError) *safeError = error_;
        return false;
    }

    if (!client_) {
        setPolicyError(QStringLiteral("The Suno client is not available to receive the imported session"));
        if (safeError) *safeError = error_;
        return false;
    }

    // The same terminal call commitSunoCredential() makes for a paste. It
    // classifies the header (the Clerk cookie families are accepted by name
    // prefix), persists through CredentialStore, and kicks the bearer fetch
    // that turns `__client` into a live `__session` JWT.
    client_->setCookie(harvest.cookieHeader.toStdString());
    // And a forced re-verification on top: setCookie() no-ops when the
    // header is identical to the one already installed, so a re-import
    // after a failed verification would otherwise kick no exchange at all
    // and strand the card on "Imported. Verifying". reverify is a no-op
    // when an exchange is already in flight, so the fresh-import path
    // pays for one early return, not a second exchange.
    client_->reverifyStoredCredential();

    // The browser name is the one fact worth surfacing: it tells the user
    // which of their browsers just surrendered the session, which is the
    // consent question they would otherwise have to guess at.
    state_ = QStringLiteral("browserImported");
    error_.clear();
    emit stateChanged(state_);
    return true;
}

void AuthCoordinator::setPolicyError(const QString& safeMessage) {
    error_ = safeMessage;
    state_ = QStringLiteral("error");
    emit stateChanged(state_);
    emit errorChanged(error_);
}

} // namespace vc::suno::auth
