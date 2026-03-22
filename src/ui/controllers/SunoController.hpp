#pragma once
// SunoController.hpp - Logic for Suno AI integration
// Coordinates fetching, downloading, and metadata processing

#include <QNetworkAccessManager>
#include <QJsonDocument>
#include <QObject>
#include <deque>
#include <memory>
#include <optional>
#include <unordered_map>
#include "suno/SunoClient.hpp"
#include "suno/SunoDatabase.hpp"
#include "suno/SunoLyrics.hpp"
#include "ui/SunoPersistentAuth.hpp"
#include "ui/SystemBrowserAuth.hpp"
#include "util/Signal.hpp"

namespace vc {

class AudioEngine;
class OverlayEngine;

namespace suno {

class SunoController : public QObject {
	Q_OBJECT

public:
	explicit SunoController(AudioEngine* audioEngine,
		OverlayEngine* overlayEngine,
		QObject* parent = nullptr);
	~SunoController() override;

	SunoClient* client() {
		return client_.get();
	}

	// Logic
	void downloadAndPlay(const SunoClip& clip);

	Result<AlignedLyrics> getLyrics(const std::string& clipId);

	void refreshLibrary(int page = 1);
	void syncDatabase(bool forceAuth = false);

	// Auth - triggers signal for QML to handle
	Q_INVOKABLE void requestAuthentication();

	// System browser auth (alternative method)
	Q_INVOKABLE void startSystemBrowserAuth();

	const std::vector<SunoClip>& clips() const {
		return accumulatedClips_;
	}

	SunoDatabase& db() {
		return db_;
	}

	bool hasLyrics(const std::string& clipId) const {
		return db_.hasLyrics(clipId);
	}

	// Check if authenticated
	Q_INVOKABLE bool isAuthenticated() const {
		return client_ && client_->isAuthenticated();
	}

	// Debug
	void setDebugLyrics(const AlignedLyrics& lyrics);

	// Signal for UI
	Signal<const std::vector<SunoClip>&> libraryUpdated;
	Signal<const std::string&> clipUpdated; // id
	Signal<const std::string&> statusMessage;

signals:
	// Emitted when auth is needed - QML should show auth UI
	void authenticationRequired();
	void authenticationSuccess();
	void authenticationFailed(const QString& reason);

public slots:
	void onLibraryFetched(const std::vector<SunoClip>& clips);
	void onAlignedLyricsFetched(const std::string& clipId,
		const std::string& json);
	void onWavConversionReady(const std::string& clipId,
		const std::string& wavUrl);
	void onError(const std::string& message);

private:
	void downloadAudio(const SunoClip& clip);
	void processDownloadedFile(const SunoClip& clip, const fs::path& path);
	void processLyricsQueue();
	void onTrackChanged();

	// Helper: Check if clipId matches currently playing track
	bool isCurrentlyPlaying(const std::string& clipId) const;

	// Helper: Parse lyrics and immediately display to overlay
	std::optional<AlignedLyrics> parseAndDisplayLyrics(
		const std::string& clipId,
		const std::string& json,
		const QJsonDocument& doc);

	// Helper: Save lyrics sidecar files (.json and .srt)
	void saveLyricsSidecar(const std::string& clipId,
		const std::string& json,
		const QJsonDocument& doc);

	// Helper: Save metadata sidecar file (.txt)
	void saveMetadataSidecar(const SunoClip& clip);

	// Helper: Extract clip ID from track with aggressive UUID detection
	std::string extractClipIdFromTrack() const;

	// Helper: Download audio from URL
	void downloadAudioFromUrl(const std::string& clipId, const std::string& url, const std::string& extension);

	AudioEngine* audioEngine_;
	OverlayEngine* overlayEngine_;
	std::unique_ptr<SunoClient> client_;
	SunoDatabase db_;
	QNetworkAccessManager* networkManager_;

	// Auth Managers
	std::unique_ptr<chadvis::SunoPersistentAuth> persistentAuth_;
	std::unique_ptr<chadvis::SystemBrowserAuth> systemAuth_;

	fs::path downloadDir_;
	std::deque<std::string> lyricsQueue_;
	int activeLyricsRequests_{0};
	int currentSyncPage_{1};
	bool isSyncing_{false};
	bool isRefreshingToken_{false};
	size_t totalLyricsToFetch_{0};
	std::chrono::steady_clock::time_point lyricsSyncStartTime_;
	std::vector<SunoClip> accumulatedClips_;

	// Direct mapping cache for recently fetched lyrics (survives track restarts)
	std::unordered_map<std::string, AlignedLyrics> directLyricsCache_;

	// Track the last requested ID for fallback mapping during transitions
	mutable std::string lastRequestedClipId_;
};

} // namespace suno
} // namespace vc
