#pragma once
// SunoController.hpp - Logic for Suno AI integration
// Coordinates fetching, downloading, and metadata processing

#include <QObject>
#include <memory>
#include <vector>
#include <string>
#include <optional>
#include <unordered_map>

#include "suno/SunoClient.hpp"
#include "suno/SunoDatabase.hpp"
#include "suno/SunoLyrics.hpp"
#include "util/Signal.hpp"

// Forward declarations
namespace vc {
    class AudioEngine;
    class OverlayEngine;
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
		OverlayEngine* overlayEngine,
		QObject* parent = nullptr);
	~SunoController() override;

	SunoClient* client() { return client_.get(); }

	// Facade Methods (Delegated to Managers)
	void downloadAndPlay(const SunoClip& clip);
	Result<AlignedLyrics> getLyrics(const std::string& clipId);
	void refreshLibrary(int page = 1);
	void syncDatabase(bool forceAuth = false);

	// Auth - triggers signal for QML to handle
	Q_INVOKABLE void requestAuthentication();
	Q_INVOKABLE void startSystemBrowserAuth();

	const std::vector<SunoClip>& clips() const;
	SunoDatabase& db() { return db_; }

	bool hasLyrics(const std::string& clipId) const {
		return db_.hasLyrics(clipId);
	}

	Q_INVOKABLE bool isAuthenticated() const {
		return client_ && client_->isAuthenticated();
	}

	void setDebugLyrics(const AlignedLyrics& lyrics);

	// Signals for UI (Aggregated from Managers)
	vc::Signal<const std::vector<SunoClip>&> libraryUpdated;
	vc::Signal<const std::string&> clipUpdated;
	vc::Signal<const std::string&> statusMessage;

signals:
	void authenticationRequired();
	void authenticationSuccess();
	void authenticationFailed(const QString& reason);

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
	OverlayEngine* overlayEngine_;
	std::unique_ptr<SunoClient> client_;
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
