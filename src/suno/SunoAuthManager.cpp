#include "suno/SunoAuthManager.hpp"
#include "core/Config.hpp"
#include "core/Logger.hpp"

namespace vc::suno {

SunoAuthManager::SunoAuthManager(SunoClient* client, QObject* parent)
    : QObject(parent),
      client_(client),
      systemAuth_(std::make_unique<vc::ui::SystemBrowserAuth>(nullptr)) {
    // Connect System Auth Signals
    connect(systemAuth_.get(), &vc::ui::SystemBrowserAuth::authSuccess, 
            this, &SunoAuthManager::onSystemAuthSuccess);
    
    connect(systemAuth_.get(), &vc::ui::SystemBrowserAuth::authFailed, 
            this, &SunoAuthManager::onSystemAuthFailed);
}

SunoAuthManager::~SunoAuthManager() = default;

void SunoAuthManager::initialize() {
    restorePersistedSession();
}

void SunoAuthManager::requestAuthentication() {
    emit authenticationRequired();
}

void SunoAuthManager::startSystemBrowserAuth() {
    LOG_INFO("SunoAuthManager: Starting system browser auth");
    systemAuth_->startAuth();
}

void SunoAuthManager::restorePersistedSession() {
    const auto& suno = CONFIG.suno();
    if (suno.token.empty() && suno.cookie.empty()) return;

    LOG_INFO("SunoAuthManager: Restoring persisted session from config");

    if (!suno.token.empty()) client_->setToken(suno.token);
    if (!suno.cookie.empty()) client_->setCookie(suno.cookie);

    emit statusMessage("Authentication restored from persisted session");
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
