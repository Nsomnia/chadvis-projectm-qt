/**
 * @file SunoPersistentAuth.cpp
 * @brief Implementation of persistent authentication manager for Suno AI
 */

#include "SunoPersistentAuth.hpp"
#include <QStandardPaths>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QWebEnginePage>

namespace chadvis {

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
    
    // CRITICAL: Enable persistent cookies
    profile_->setPersistentCookiesPolicy(QWebEngineProfile::ForcePersistentCookies);
    profile_->setOffTheRecord(false);
    
    // Monitor cookies for session extraction
    QWebEngineCookieStore* cookieStore = profile_->cookieStore();
    
    connect(cookieStore, &QWebEngineCookieStore::cookieAdded,
            this, &SunoPersistentAuth::onCookieAdded);
    connect(cookieStore, &QWebEngineCookieStore::cookieRemoved,
            this, &SunoPersistentAuth::onCookieRemoved);
    
    // Load all existing cookies from persistent storage
    cookieStore->loadAllCookies();
}

QString SunoPersistentAuth::getStoragePath() const {
    // Use config directory for persistent storage
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
        view->load(QUrl("https://suno.com/login"));
    }
}

void SunoPersistentAuth::navigateToSuno(QWebEngineView* view) {
    if (view && view->page()) {
        view->load(QUrl("https://suno.com"));
    }
}

void SunoPersistentAuth::logout() {
    // Clear auth state
    authState_ = SunoAuthState{};
    
    // Clear all cookies from persistent storage
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
    
    // Force reload of cookies from storage
    profile_->cookieStore()->loadAllCookies();
    
    // Check auth status after a short delay to allow cookies to be processed
    QTimer::singleShot(100, this, &SunoPersistentAuth::checkAuthStatus);
}

void SunoPersistentAuth::onCookieAdded(const QNetworkCookie& cookie) {
    const QString name = QString::fromUtf8(cookie.name());
    const QString value = QString::fromUtf8(cookie.value());
    
    if (name == "__session") {
        authState_.sessionCookie = value;
        
        // Extract JWT if it starts with eyJ (base64 encoded JWT header)
        if (value.startsWith("eyJ")) {
            authState_.bearerToken = value;
            extractJwtFromCookie(value);
        }
        
        // Start checking for auth completion
        if (!authCheckTimer_->isActive()) {
            authCheckTimer_->start();
        }
    } else if (name == "__client") {
        authState_.clientCookie = value;
        
        if (!authCheckTimer_->isActive()) {
            authCheckTimer_->start();
        }
    }
}

void SunoPersistentAuth::onCookieRemoved(const QNetworkCookie& cookie) {
    const QString name = QString::fromUtf8(cookie.name());
    
    if (name == "__session") {
        authState_.sessionCookie.clear();
        authState_.bearerToken.clear();
    } else if (name == "__client") {
        authState_.clientCookie.clear();
    }
    
    // Emit state change if we're no longer authenticated
    if (!authState_.isValid() && authCheckTimer_->isActive()) {
        authCheckTimer_->stop();
        emit authStateChanged(authState_);
    }
}

void SunoPersistentAuth::extractJwtFromCookie(const QString& cookieValue) {
    // JWT structure: header.payload.signature
    QStringList parts = cookieValue.split('.');
    if (parts.size() != 3) {
        return;
    }
    
    // Decode payload (middle part)
    QByteArray payload = QByteArray::fromBase64(
        parts[1].toUtf8(), 
        QByteArray::Base64UrlEncoding
    );
    
    QJsonDocument doc = QJsonDocument::fromJson(payload);
    if (doc.isNull() || !doc.isObject()) {
        return;
    }
    
    QJsonObject obj = doc.object();
    
    // Extract user ID from 'sub' claim
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
    
    // Extract user ID from token
    if (token.startsWith("eyJ")) {
        extractJwtFromCookie(token);
    }
    
    emit authStateChanged(authState_);
}

} // namespace chadvis
