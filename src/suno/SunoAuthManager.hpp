#pragma once

#include <QObject>
#include <memory>
#include <string>

#include "core/Config.hpp"
#include "core/Logger.hpp"
#include "suno/SunoClient.hpp"
#include "ui/SunoPersistentAuth.hpp"
#include "ui/SystemBrowserAuth.hpp"
#include "util/Result.hpp"

namespace vc::suno {

class SunoAuthManager : public QObject {
Q_OBJECT

public:
	explicit SunoAuthManager(SunoClient* client, QObject* parent = nullptr);
	~SunoAuthManager() override;

	void initialize();
	void requestAuthentication();
	void startSystemBrowserAuth();

signals:
	void statusMessage(const std::string& message);
	void authenticationRequired();
	void authenticationSuccess();
	void authenticationFailed(const QString& reason);

private:
    SunoClient* client_;
    std::unique_ptr<vc::ui::SunoPersistentAuth> persistentAuth_;
    std::unique_ptr<vc::ui::SystemBrowserAuth> systemAuth_;
    bool isRefreshingToken_ = false;

    void onPersistentAuthRestored(const vc::ui::SunoAuthState& authState);
    void onSystemAuthSuccess(const QString& token);
    void onSystemAuthFailed(const QString& reason);
};

} // namespace vc::suno
