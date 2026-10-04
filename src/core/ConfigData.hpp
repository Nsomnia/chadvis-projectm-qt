/**
 * @file ConfigData.hpp
 * @brief Configuration data structures.
 *
 * This file defines the POD (Plain Old Data) structs used to hold configuration
 * values. It is separated from the logic classes to keep headers lean and
 * avoid circular dependencies.
 */

#pragma once
#include <filesystem>
#include <string>
#include <vector>
#include "util/Types.hpp"

namespace vc {

namespace fs = std::filesystem;

// Karaoke configuration
struct KaraokeConfig {
    bool enabled{true}; // Force enabled default
    std::string fontFamily{"Arial"};
    u32 fontSize{32};
    bool bold{true};
    Color activeColor{Color::yellow()};
    Color inactiveColor{Color::white()};
    Color shadowColor{Color::black()};
    f32 yPosition{0.5f}; // Center it for visibility testing (was 0.85f)
};

// Text overlay element configuration
struct OverlayElementConfig {
    std::string id;
    std::string text;
    Vec2 position{0.5f, 0.5f};
    u32 fontSize{32};
    Color color{Color::white()};
    f32 opacity{1.0f};
    std::string animation{"none"};
    f32 animationSpeed{1.0f};
    std::string anchor{"left"}; // left, center, right
    bool visible{true};
};

// The [overlay] section: a master switch plus its ordered element list.
//
// `enabled` used to be written as a hardcoded literal `true` and never parsed,
// so it could not be false and did not round-trip. It is a real config value
// now, but note that no consumer reads it yet: OverlayBridge keeps its own
// JSON store and the visualizer renders overlay elements from its own sources.
// See TODO.md before assuming a `false` here hides anything on screen.
struct OverlayConfig {
    bool enabled{true};
    std::vector<OverlayElementConfig> elements;
};

// Video encoding settings
//
// These describe the OUTPUT file. Nothing here describes the input: a recording
// has no input file, only a stream of PCM arriving on AudioQueue, and the
// encoder converts that into exactly what is written below.
struct VideoEncoderConfig {
    std::string codec{"libx264"};
    u32 crf{18};
    std::string preset{"medium"};
    std::string pixelFormat{"yuv420p"};
    u32 width{1920};
    u32 height{1080};
    u32 fps{60};

    std::string codecName() const {
        return codec;
    }
    std::string presetName() const {
        return preset;
    }
    // Keyframe interval. 0 = let the encoder choose (VideoRecorderFFmpeg.cpp
    // substitutes fps * 2). Written to `[recording.video] gop_size`.
    u32 gopSize{0};
    // B-frames allowed in the output. 0 disables them, which is also what every
    // test fixture in the tree sets -- see the measured note at
    // VideoRecorderFFmpeg.cpp:460 that both codecs this project uses report
    // has_b_frames == 0 regardless. Written to `[recording.video] b_frames`.
    u32 bFrames{0};
};

// Audio encoding settings
//
// `sampleRate` and `channels` are the ENCODER'S OUTPUT spec, not the source
// file's. This was ambiguous here -- there was no comment at all, and a reader
// could reasonably take them for "what the input carries". Three call sites
// settle it:
//   - EncoderSettings::fromConfig() copies them into AudioSettings, and
//   - VideoRecorderFFmpeg::initAudioStream() sets
//     `audioCodecCtx_->sample_rate` from it, i.e. it is what the codec is
//     *opened* at, and
//   - the same value is passed to swr_alloc_set_opts2 as the resampler's OUT
//     rate.
// So 48000 here means "write a 48 kHz track", whatever the source was.
struct AudioEncoderConfig {
    std::string codec{"aac"};
    u32 bitrate{320};

    std::string codecName() const {
        return codec;
    }
    // Written to `[recording.audio] sample_rate`. Not `[audio] sample_rate`,
    // which is a different table describing the playback device
    // (AudioConfig::sampleRate) -- see parseAudio.
    u32 sampleRate{48000};
    // Written to `[recording.audio] channels`. Capped at 2 in practice: an
    // AudioFrame is stereo-only (AudioQueue.hpp:31), so a wider sink could not
    // reach a consumer intact.
    u32 channels{2};
};

// Recording configuration
struct RecordingConfig {
    bool enabled{true};
    bool autoRecord{false};
    bool recordEntireSong{false};
    bool restartTrackOnRecord{false};
    bool stopAtTrackEnd{false};
    fs::path outputDirectory;
    std::string defaultFilename{"chadvis-projectm-qt_{date}_{time}"};
    std::string container{"mp4"};
    VideoEncoderConfig video;
    AudioEncoderConfig audio;
};

// Visualizer configuration
struct VisualizerConfig {
    fs::path presetPath;
    u32 width{1920};
    u32 height{1080};
    u32 fps{60};
    f32 beatSensitivity{1.0f};
    u32 presetDuration{30};
    u32 smoothPresetDuration{5};
    f32 hardCutSensitivity{1.0f}; // Sensitivity for hard cuts
    bool aspectCorrection{true};
    bool shufflePresets{true};
    std::string forcePreset{};
    bool useDefaultPreset{false};
    u32 meshX{32}; // Grid size X
    u32 meshY{24}; // Grid size Y
    std::vector<fs::path> texturePaths;
};

// Audio configuration
struct AudioConfig {
    std::string device{"default"};
    u32 bufferSize{2048};
    u32 sampleRate{44100};
};

// UI configuration
struct UIConfig {
    std::string theme{"dark"};
    bool showPlaylist{true};
    bool showPresets{true};
    bool showDebugPanel{false};
    Color backgroundColor{Color::black()};
    Color accentColor{Color::fromHex("#00FF88")};

    // Sidebar & layout persistence
    std::string expandedPanel{"playback"};  // Which accordion panel is expanded
    i32 sidebarWidth{280};                  // Desktop sidebar width in px
    bool drawerOpen{false};                 // Mobile drawer open state
};

// Keyboard shortcuts
struct KeyboardConfig {
    std::string playPause{"Space"};
    std::string nextTrack{"N"};
    std::string prevTrack{"P"};
    std::string toggleRecord{"R"};
    std::string toggleFullscreen{"F"};
    std::string nextPreset{"Right"};
    std::string prevPreset{"Left"};
};

enum class SunoDownloadFormat {
    MP3,
    WAV
};

struct SunoConfig {
    // Non-secret install identity sent as the Device-Id header (plain UUID).
    std::string deviceId;
    // Legacy Clerk credentials, PARSE-ONLY. ConfigParsers reads them from a
    // table that the serializer never expands, so there is no code path that can
    // write them out; they exist only so SunoClient's one-time startup
    // migration can move them into CredentialStore. Because serialize rebuilds
    // the whole file, the first save after an upgrade deletes them from
    // config.toml -- deliberately, and with a warning logged at parse time.
    std::string token;
    std::string cookie;
    fs::path downloadPath;
    bool autoDownload{false};
    bool saveLyrics{true};
    bool embedMetadata{true};
    SunoDownloadFormat downloadFormat{SunoDownloadFormat::MP3};

    // Debugging
    bool debugLyrics{false};
    fs::path debugLyricsFile;
};

} // namespace vc
