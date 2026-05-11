/**
 * @file SunoPersistentAuth.cpp
 * @brief Implementation of persistent authentication manager for Suno AI
 */

#include "SunoPersistentAuth.hpp"
#include "suno/SunoEndpoints.hpp"
#include <QStandardPaths>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QWebEnginePage>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
#include "core/Logger.hpp"

namespace vc::ui {

SunoPersistentAuth::SunoPersistentAuth(QObject* parent)
    : QObject(parent)
    , profile_(nullptr)
    , authCheckTimer_(new QTimer(this)) {
    
    authCheckTimer_->setInterval(1000);
    connect(authCheckTimer_, &QTimer::timeout, this, &SunoPersistentAuth::checkAuthStatus);
}

SunoPersistentAuth::~SunoPersistentAuth() {
    // Profile is child of this QObject, will be cleaned up automatically
}

void SunoPersistentAuth::initialize() {
    if (initialized_) {
        return;
    }

    setupPersistentStorage();
    initialized_ = true;

    // Check for existing session on initialization
    QTimer::singleShot(500, this, [this]() {
        syncCookies();
    });
}

void SunoPersistentAuth::setupPersistentStorage() {
    // Create persistent profile with unique name
    profile_ = new QWebEngineProfile("suno_persistent_auth", this);
    
    // Set storage paths in user's config directory
    QString storagePath = getStoragePath();
    QString cachePath = getCachePath();
    
    // Ensure directories exist
    QDir().mkpath(storagePath);
    QDir().mkpath(cachePath);
    
    // Configure persistent storage
    profile_->setPersistentStoragePath(storagePath);
    profile_->setCachePath(cachePath);
    
    // Set a modern, real-looking User Agent that matches the underlying Chromium major version (134)
    profile_->setHttpUserAgent("Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/134.0.0.0 Safari/537.36");
    
    // Inject Stealth Script to mock navigator properties
    QWebEngineScript script;
    script.setName("stealth_injection");
    script.setInjectionPoint(QWebEngineScript::DocumentCreation);
    script.setWorldId(QWebEngineScript::MainWorld);
    script.setRunsOnSubFrames(true);
    
    QString stealthJs = R"(
        (function() {
            Object.defineProperty(navigator, 'plugins', {
                get: () => {
                    return [
                        { name: "Chrome PDF Plugin", filename: "internal-pdf-viewer", description: "Portable Document Format" },
                        { name: "Chrome PDF Viewer", filename: "mhjfbmdgcfjbbpaeojofohoefgiehjai", description: "Portable Document Format" },
                        { name: "Native Client", filename: "internal-nacl-plugin", description: "" }
                    ];
                }
            });
            Object.defineProperty(navigator, 'webdriver', { get: () => undefined });
            Object.defineProperty(navigator, 'languages', { get: () => ['en-US', 'en'] });
            if (!window.chrome) {
                window.chrome = {
                    runtime: {},
                    loadTimes: function() {},
                    csi: function() {},
                    app: { isInstalled: false }
                };
            }
        })();
    )";
    script.setSourceCode(stealthJs);
    profile_->scripts()->insert(script);

    profile_->setPersistentCookiesPolicy(QWebEngineProfile::ForcePersistentCookies);
    
    QWebEngineCookieStore* cookieStore = profile_->cookieStore();
    connect(cookieStore, &QWebEngineCookieStore::cookieAdded, this, &SunoPersistentAuth::onCookieAdded);
    connect(cookieStore, &QWebEngineCookieStore::cookieRemoved, this, &SunoPersistentAuth::onCookieRemoved);
    
    cookieStore->loadAllCookies();
}

QString SunoPersistentAuth::getStoragePath() const {
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    if (configDir.isEmpty()) {
        configDir = QDir::homePath() + "/.config";
    }
    return configDir + "/chadvis-projectm-qt/webengine/suno/storage";
}

QString SunoPersistentAuth::getCachePath() const {
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    if (configDir.isEmpty()) {
        configDir = QDir::homePath() + "/.config";
    }
    return configDir + "/chadvis-projectm-qt/webengine/suno/cache";
}

bool SunoPersistentAuth::isAuthenticated() const {
    return authState_.isValid();
}

QWebEngineView* SunoPersistentAuth::createWebView(QWidget* parent) {
    if (!initialized_) {
        initialize();
    }
    
    QWebEngineView* view = new QWebEngineView(parent);
    QWebEnginePage* page = new QWebEnginePage(profile_, view);
    view->setPage(page);
    
    return view;
}

void SunoPersistentAuth::navigateToLogin(QWebEngineView* view) {
    if (view && view->page()) {
        view->load(QUrl(QString::fromUtf8(vc::suno::endpoints::WEB_BASE.data(), static_cast<int>(vc::suno::endpoints::WEB_BASE.size())) +
                        QString::fromUtf8(vc::suno::endpoints::LOGIN.data(), static_cast<int>(vc::suno::endpoints::LOGIN.size()))));
    }
}

void SunoPersistentAuth::navigateToSuno(QWebEngineView* view) {
    if (view && view->page()) {
        view->load(QUrl(QString::fromUtf8(vc::suno::endpoints::WEB_BASE.data(), static_cast<int>(vc::suno::endpoints::WEB_BASE.size()))));
    }
}

void SunoPersistentAuth::clearSession() {
    authState_ = SunoAuthState{};
    if (profile_) {
        profile_->cookieStore()->deleteAllCookies();
    }
    emit loggedOut();
    emit statusMessage("Logged out - authentication cleared");
}

void SunoPersistentAuth::syncCookies() {
    if (!profile_) {
        return;
    }
    profile_->cookieStore()->loadAllCookies();
    QTimer::singleShot(100, this, &SunoPersistentAuth::checkAuthStatus);
}

void SunoPersistentAuth::onCookieAdded(const QNetworkCookie& cookie) {
    const QString name = QString::fromUtf8(cookie.name());
    const QString value = QString::fromUtf8(cookie.value());
    
    if (name.startsWith("__session")) {
        authState_.sessionCookie = value;
        if (value.startsWith("eyJ")) {
            authState_.bearerToken = value;
            extractJwtFromCookie(value);
        }
        if (!authCheckTimer_->isActive()) {
            authCheckTimer_->start();
        }
    } else if (name.startsWith("__client")) {
        authState_.clientCookie = value;
        if (!authCheckTimer_->isActive()) {
            authCheckTimer_->start();
        }
    }
}

void SunoPersistentAuth::onCookieRemoved(const QNetworkCookie& cookie) {
    const QString name = QString::fromUtf8(cookie.name());
    if (name.startsWith("__session")) {
        authState_.sessionCookie.clear();
        authState_.bearerToken.clear();
    } else if (name.startsWith("__client")) {
        authState_.clientCookie.clear();
    }
    if (!authState_.isValid() && authCheckTimer_->isActive()) {
        authCheckTimer_->stop();
        emit authStateChanged(authState_);
    }
}

void SunoPersistentAuth::extractJwtFromCookie(const QString& cookieValue) {
    QStringList parts = cookieValue.split('.');
    if (parts.size() != 3) {
        return;
    }
    QByteArray payload = QByteArray::fromBase64(parts[1].toUtf8(), QByteArray::Base64UrlEncoding);
    QJsonDocument doc = QJsonDocument::fromJson(payload);
    if (doc.isNull() || !doc.isObject()) {
        return;
    }
    QJsonObject obj = doc.object();
    QString userId = obj["sub"].toString();
    if (!userId.isEmpty()) {
        authState_.userId = userId;
    }
}

void SunoPersistentAuth::checkAuthStatus() {
    if (authState_.isValid()) {
        authCheckTimer_->stop();
        emit authenticated(authState_);
        emit authStateChanged(authState_);
        emit statusMessage("Authentication successful - session active");
    }
}

void SunoPersistentAuth::setBearerToken(const QString& token) {
    authState_.bearerToken = token;
    if (token.startsWith("eyJ")) {
        extractJwtFromCookie(token);
    }
    emit authStateChanged(authState_);
}

} // namespace vc::ui
