#pragma once
#include "suno/SunoClip.hpp"
#include "suno/SunoLyrics.hpp"
#include "util/Result.hpp"
#include "util/Signal.hpp"
#include <QObject>
#include <QJsonDocument>
#include <optional>
#include <string>
#include <unordered_map>

namespace vc {

class AudioEngine;
class OverlayEngine;

namespace suno {

class SunoClient;
class SunoDatabase;

class SunoLyricsManager : public QObject {
    Q_OBJECT

public:
    explicit SunoLyricsManager(SunoClient* client, SunoDatabase& db, 
                               AudioEngine* audioEngine, OverlayEngine* overlayEngine);
    ~SunoLyricsManager() override = default;

    Result<AlignedLyrics> getLyrics(const std::string& clipId);
    void setClipsRef(const std::vector<SunoClip>* clips) { clipsRef_ = clips; }
    void cacheLyrics(const std::string& clipId, const AlignedLyrics& lyrics);
    void clearCache() { directLyricsCache_.clear(); }

    void onTrackChanged();
    void onLyricsFetched(const std::string& clipId, const std::string& json);

    void setDebugLyrics(const AlignedLyrics& lyrics);
    void setDebugMode(bool enabled) { debugMode_ = enabled; }

    Signal<const std::string&> lyricsReady;

private:
    bool isCurrentlyPlaying(const std::string& clipId) const;
    std::string extractClipIdFromTrack() const;
    std::optional<AlignedLyrics> parseAndDisplayLyrics(const std::string& clipId, const std::string& json, const QJsonDocument& doc);
    void saveLyricsSidecar(const std::string& clipId, const std::string& json, const QJsonDocument& doc);

    SunoClient* client_;
    SunoDatabase& db_;
    AudioEngine* audioEngine_;
    OverlayEngine* overlayEngine_;
    const std::vector<SunoClip>* clipsRef_{nullptr};
    
    std::unordered_map<std::string, AlignedLyrics> directLyricsCache_;
    mutable std::string lastRequestedClipId_;
    bool debugMode_{false};
};

} // namespace vc::suno
} // namespace vc
