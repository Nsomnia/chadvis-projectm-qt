#pragma once
// SunoWorkspace.hpp — local song-creation workspace for the ChadVis desktop frontend.
//
// This class manages the LOCAL creation pipeline: prompt authoring → Suno generation
// → clip retrieval → local editing (trim, stem separation, lyrics alignment) →
// final render (audio-only, video-with-visualizer, or karaoke-style).
//
// It is deliberately decoupled from QML so it can be unit-tested headlessly.
// QML consumes it through SunoWorkspaceBridge (see qml_bridge/).

#include "SunoModels.hpp"
#include "util/Result.hpp"
#include "util/Signal.hpp"

#include <QJsonObject>
#include <QJsonArray>
#include <QString>
#include <QTimer>

#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace vc::suno {

/// How a generated clip should be rendered locally.
enum class RenderMode {
    AudioOnly,    // WAV/MP3/M4A export
    Visualizer,   // projectM-reactive video
    Karaoke,      // synced lyrics overlay on visualizer
    StemExport    // separate stem tracks (drums, bass, vocals, etc.)
};

/// A single stem track extracted from a generated clip.
struct StemTrack {
    std::string type;       // "drums" | "bass" | "vocals" | "guitar" | ...
    std::string url;        // CDN URL for the stem audio
    std::string localPath;  // downloaded local path (empty until fetched)
    bool downloaded{false};
};

/// A trimmed region of a clip for use in a mashup or edit.
struct ClipRegion {
    std::string clipId;
    double startSeconds{0.0};
    double endSeconds{0.0};
    std::string label;
};

/// The full local workspace state for one creation session.
struct WorkspaceState {
    std::string currentPrompt;
    std::string currentTags;
    std::string currentModel{"chirp-v3.5"};
    bool makeInstrumental{false};

    std::vector<SunoClip> generatedClips;
    std::vector<ClipRegion> regions;         // trimmed regions for mashup
    std::vector<StemTrack> stems;            // extracted stems
    std::string lyricsText;                  // user-edited lyrics
    std::string alignedLyricsJson;           // raw aligned-lyrics JSON from Suno

    RenderMode renderMode{RenderMode::AudioOnly};
    double renderDuration{120.0};            // seconds
    bool renderIncludeVideo{false};
    bool renderIncludeKaraoke{false};

    bool isGenerating{false};
    bool isEditing{false};
    bool isRendering{false};
    std::string lastError;
};

/// Orchestrates local song creation on top of SunoClient.
class SunoWorkspace : public QObject {
    Q_OBJECT

public:
    explicit SunoWorkspace(QObject* parent = nullptr);
    ~SunoWorkspace() override;

    // ── Generation ──────────────────────────────────────────────────────
    /// Submit a generation prompt. Emits generationStarted → generationCompleted.
    void startGeneration(const std::string& prompt, const std::string& tags,
                         bool makeInstrumental, const std::string& model);
    /// Cancel the in-flight generation (best effort).
    void cancelGeneration();
    bool isGenerating() const { return state_.isGenerating; }

    // ── Editing ─────────────────────────────────────────────────────────
    /// Add a trimmed region from a clip to the mashup timeline.
    void addRegion(const std::string& clipId, double startS, double endS,
                   const std::string& label = {});
    void removeRegion(size_t index);
    void clearRegions();
    const std::vector<ClipRegion>& regions() const { return state_.regions; }

    /// Request stem separation for a clip (requires Suno plan feature: get_stems).
    void requestStems(const std::string& clipId);
    const std::vector<StemTrack>& stems() const { return state_.stems; }

    /// Update lyrics text after user edits.
    void setLyricsText(const std::string& text);
    const std::string& lyricsText() const { return state_.lyricsText; }

    // ── Rendering ───────────────────────────────────────────────────────
    /// Queue a render of the current workspace state.
    void startRender(RenderMode mode, double durationS, bool includeVideo,
                     bool includeKaraoke);
    void cancelRender();
    bool isRendering() const { return state_.isRendering; }
    RenderMode renderMode() const { return state_.renderMode; }

    // ── State access ─────────────────────────────────────────────────────
    const WorkspaceState& state() const { return state_; }
    const std::vector<SunoClip>& generatedClips() const { return state_.generatedClips; }

    // ── Persistence ──────────────────────────────────────────────────────
    /// Save workspace state to a JSON file for later resume.
    Result<void> saveWorkspace(const fs::path& path);
    /// Load a previously-saved workspace.
    Result<void> loadWorkspace(const fs::path& path);

signals:
    void generationStarted();
    void generationCompleted(const std::vector<SunoClip>& clips);
    void generationFailed(const std::string& error);
    void regionAdded(const ClipRegion& region);
    void regionsCleared();
    void stemsRequested(const std::string& clipId);
    void stemsReady(const std::vector<StemTrack>& stems);
    void lyrics_changed(const std::string& text);
    void renderStarted(RenderMode mode);
    void renderProgress(f32 percent, const std::string& stage);
    void renderCompleted(const fs::path& outputPath);
    void renderFailed(const std::string& error);
    void errorOccurred(const std::string& message);

private:
    WorkspaceState state_;
    std::string workspaceId_;
};

} // namespace vc::suno