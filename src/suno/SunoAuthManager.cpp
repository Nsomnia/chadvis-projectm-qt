#include "suno/SunoAuthManager.hpp"
#include "core/Config.hpp"
#include "core/Logger.hpp"

namespace vc::suno {

SunoAuthManager::SunoAuthManager(SunoClient* client, QObject* parent)
    : QObject(parent),
      client_(client),
      persistentAuth_(std::make_unique<chadvis::SunoPersistentAuth>(nullptr)),
      systemAuth_(std::make_unique<chadvis::SystemBrowserAuth>(nullptr)) {
    
    // Connect Persistent Auth Signals
    connect(persistentAuth_.get(), &chadvis::SunoPersistentAuth::authenticated, 
            this, &SunoAuthManager::onPersistentAuthRestored);

    // Connect System Auth Signals
    connect(systemAuth_.get(), &chadvis::SystemBrowserAuth::authSuccess, 
            this, &SunoAuthManager::onSystemAuthSuccess);
    
    connect(systemAuth_.get(), &chadvis::SystemBrowserAuth::authFailed, 
            this, &SunoAuthManager::onSystemAuthFailed);
}

SunoAuthManager::~SunoAuthManager() = default;

void SunoAuthManager::initialize() {
    persistentAuth_->initialize();
}

void SunoAuthManager::requestAuthentication() {
    emit authenticationRequired();
}

void SunoAuthManager::startSystemBrowserAuth() {
    LOG_INFO("SunoAuthManager: Starting system browser auth");
    systemAuth_->startAuth();
}

void SunoAuthManager::onPersistentAuthRestored(const chadvis::SunoAuthState& authState) {
    LOG_INFO("SunoAuthManager: Persistent auth session restored");
    
    if (!authState.bearerToken.isEmpty()) {
        client_->setToken(authState.bearerToken.toStdString());
        CONFIG.suno().token = authState.bearerToken.toStdString();
    }
    
    QString cookieStr;
    if (!authState.clientCookie.isEmpty()) cookieStr += "__client=" + authState.clientCookie + "; ";
    if (!authState.sessionCookie.isEmpty()) cookieStr += "__session=" + authState.sessionCookie;
    
    if (!cookieStr.isEmpty()) {
         client_->setCookie(cookieStr.toStdString());
         CONFIG.suno().cookie = cookieStr.toStdString();
	}
	CONFIG.save(CONFIG.configPath());

	emit statusMessage("Authentication restored from persistent session");
	emit authenticationSuccess();
}

void SunoAuthManager::onSystemAuthSuccess(const QString& token) {
    LOG_INFO("SunoAuthManager: System auth success");
    
    // If it's a bearer token (JWT)
	if (token.startsWith("eyJ")) {
		client_->setToken(token.toStdString());
		CONFIG.suno().token = token.toStdString();
		CONFIG.save(CONFIG.configPath());
		emit statusMessage("System authentication successful");
		emit authenticationSuccess();
	} else {
		LOG_INFO("SunoAuthManager: Received token from system auth: {}", token.left(10).toStdString());
		emit authenticationFailed("Received non-JWT token from browser auth");
	}
}

void SunoAuthManager::onSystemAuthFailed(const QString& reason) {
	LOG_ERROR("SunoAuthManager: System auth failed: {}", reason.toStdString());
	emit statusMessage("System Login Failed: " + reason.toStdString());
	emit authenticationFailed(reason);
}

} // namespace vc::suno
