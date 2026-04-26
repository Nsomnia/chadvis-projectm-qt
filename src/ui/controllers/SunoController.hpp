#pragma once
// SunoController.hpp - Logic for Suno AI integration
// Coordinates fetching, downloading, and metadata processing

#include <QObject>
#include <QVariantList>
#include <memory>
#include <vector>
#include <string>
#include <optional>
#include <unordered_map>

#include "suno/SunoClient.hpp"
#include "suno/SunoDatabase.hpp"
#include "suno/SunoLyrics.hpp"
#include "suno/SunoOrchestrator.hpp"

// Forward declarations
namespace vc {
class AudioEngine;

namespace suno {
class SunoAuthManager;
class SunoLibraryManager;
class SunoDownloader;
class SunoLyricsManager;
}
}

namespace vc::suno {

class SunoController : public QObject {
Q_OBJECT

public:
	explicit SunoController(AudioEngine* audioEngine,
		QObject* parent = nullptr);
	~SunoController() override;

  SunoClient* client() { return client_.get(); }
  SunoLibraryManager* libraryManager() { return libraryManager_.get(); }

	// Facade Methods (Delegated to Managers)
	void downloadAndPlay(const SunoClip& clip);
	Result<AlignedLyrics> getLyrics(const std::string& clipId);
	void refreshLibrary(int page = 1);
	void syncDatabase(bool forceAuth = false);

	// Auth - triggers signal for QML to handle
	Q_INVOKABLE void requestAuthentication();
	Q_INVOKABLE void startSystemBrowserAuth();
	Q_INVOKABLE void sendChatMessage(const QString& message, const QString& workspaceId = {});
	Q_INVOKABLE void fetchChatHistory();

	const std::vector<SunoClip>& clips() const;
	SunoDatabase& db() { return db_; }

	bool hasLyrics(const std::string& clipId) const {
		return db_.hasLyrics(clipId);
	}

	Q_INVOKABLE bool isAuthenticated() const {
		return client_ && client_->isAuthenticated();
	}

	void setDebugLyrics(const AlignedLyrics& lyrics);

signals:
	void libraryUpdated(const std::vector<SunoClip>& clips);
	void clipUpdated(const std::string& clipId);
	void statusMessage(const std::string& message);
	void authenticationRequired();
	void authenticationSuccess();
	void authenticationFailed(const QString& reason);
	void chatMessageReceived(const QString& response, const QString& workspaceId);
	void chatHistoryFetched(const QVariantList& sessions);
	void chatError(const QString& error);

private:
	void onTrackChanged();
	bool isCurrentlyPlaying(const std::string& clipId) const;
	std::string extractClipIdFromTrack() const;
	
    // Helper: Parse lyrics and immediately display to overlay
	std::optional<AlignedLyrics> parseAndDisplayLyrics(
		const std::string& clipId,
		const std::string& json,
		const QJsonDocument& doc);

	AudioEngine* audioEngine_;

	std::unique_ptr<SunoClient> client_;
	std::unique_ptr<vc::SunoOrchestrator> orchestrator_;
	SunoDatabase db_;
	
    // Managers
    std::unique_ptr<SunoAuthManager> authManager_;
    std::unique_ptr<SunoLibraryManager> libraryManager_;
    std::unique_ptr<SunoDownloader> downloader_;
    std::unique_ptr<SunoLyricsManager> lyricsManager_;

	// Direct mapping cache for recently fetched lyrics (survives track restarts)
	std::unordered_map<std::string, AlignedLyrics> directLyricsCache_;

	// Track the last requested ID for fallback mapping during transitions
	mutable std::string lastRequestedClipId_;
};

} // namespace vc::suno
