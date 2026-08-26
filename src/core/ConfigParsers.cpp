/**
 * @file ConfigParsers.cpp
 * @brief TOML parsing and serialization logic.
 *
 * Field tables are the single source of truth: every TOML key appears exactly
 * once in a section table and expands into BOTH parse and serialize code via
 * VC_PARSE_FIELD / VC_SER_FIELD. Parse fallbacks are read from a
 * default-constructed config struct, so parser defaults can never drift from
 * the initializers in ConfigData.hpp (the source of truth).
 */
#include "ConfigParsers.hpp"
#include <algorithm>
#include "Logger.hpp"

namespace vc {

namespace {
template <typename T>
T get(const toml::table& tbl, std::string_view key, T defaultVal) {
    if (auto node = tbl[key]) {
        if constexpr (std::is_same_v<T, std::string>) {
            if (auto val = node.value<std::string>())
                return *val;
        } else if constexpr (std::is_same_v<T, bool>) {
            if (auto val = node.value<bool>())
                return *val;
        } else if constexpr (std::is_same_v<T, f32>) {
            if (auto val = node.value<double>())
                return static_cast<f32>(*val);
        } else if constexpr (std::is_integral_v<T>) {
            if (auto val = node.value<i64>())
                return static_cast<T>(*val);
        }
    }
    return defaultVal;
}

Vec2 parseVec2(const toml::table& tbl, Vec2 defaultVal = {}) {
    return {get(tbl, "x", defaultVal.x), get(tbl, "y", defaultVal.y)};
}

fs::path expandPath(std::string_view path) {
    std::string p(path);
    if (p.starts_with("~/")) {
        if (const char* home = std::getenv("HOME")) {
            p = std::string(home) + p.substr(1);
        }
    }
    return fs::path(p);
}

// ─────────────────────────────────────────────────────────────────────
// Per-section field tables.
//
// Contract for expansion sites:
//   - `t`   : pointer to this section's toml::table (parse only)
//   - `obj` : the config object being read/written
//   - `def` : default-constructed instance of obj's type (parse only)
//
// TYPE tags: STR BOOL U32 I32 F32 PATH COLOR
// ─────────────────────────────────────────────────────────────────────

#define CHADVIS_AUDIO_FIELDS(X)          \
    X("device",      device,     STR)    \
    X("buffer_size", bufferSize, U32)    \
    X("sample_rate", sampleRate, U32)

// preset_path and texture_paths handled manually (see parseVisualizer).
#define CHADVIS_VISUALIZER_FIELDS(X)              \
    X("width", width, U32)                        \
    X("height", height, U32)                      \
    X("fps", fps, U32)                            \
    X("beat_sensitivity", beatSensitivity, F32)   \
    X("preset_duration", presetDuration, U32)     \
    X("smooth_preset_duration", smoothPresetDuration, U32) \
    X("hard_cut_sensitivity", hardCutSensitivity, F32)     \
    X("aspect_correction", aspectCorrection, BOOL)         \
    X("shuffle_presets", shufflePresets, BOOL)            \
    X("force_preset", forcePreset, STR)                   \
    X("use_default_preset", useDefaultPreset, BOOL)       \
    X("mesh_x", meshX, U32)                               \
    X("mesh_y", meshY, U32)

// output_directory handled manually (see parseRecording).
#define CHADVIS_RECORDING_FIELDS(X)             \
    X("enabled", enabled, BOOL)                 \
    X("auto_record", autoRecord, BOOL)          \
    X("record_entire_song", recordEntireSong, BOOL)      \
    X("restart_track_on_record", restartTrackOnRecord, BOOL) \
    X("stop_at_track_end", stopAtTrackEnd, BOOL)         \
    X("default_filename", defaultFilename, STR)          \
    X("container", container, STR)

#define CHADVIS_VIDEO_FIELDS(X)     \
    X("codec", codec, STR)          \
    X("crf", crf, U32)              \
    X("preset", preset, STR)        \
    X("pixel_format", pixelFormat, STR) \
    X("width", width, U32)          \
    X("height", height, U32)        \
    X("fps", fps, U32)

#define CHADVIS_REC_AUDIO_FIELDS(X) \
    X("codec",   codec,   STR)      \
    X("bitrate", bitrate, U32)

#define CHADVIS_UI_FIELDS(X)                          \
    X("theme", theme, STR)                            \
    X("show_playlist", showPlaylist, BOOL)            \
    X("show_presets", showPresets, BOOL)              \
    X("show_debug_panel", showDebugPanel, BOOL)       \
    X("visualizer_background", backgroundColor, COLOR) \
    X("accent_color", accentColor, COLOR)             \
    X("expanded_panel", expandedPanel, STR)           \
    X("sidebar_width", sidebarWidth, I32)             \
    X("drawer_open", drawerOpen, BOOL)

#define CHADVIS_KEYBOARD_FIELDS(X)   \
    X("play_pause", playPause, STR)  \
    X("next_track", nextTrack, STR)  \
    X("prev_track", prevTrack, STR)  \
    X("toggle_record", toggleRecord, STR)       \
    X("toggle_fullscreen", toggleFullscreen, STR) \
    X("next_preset", nextPreset, STR)           \
    X("prev_preset", prevPreset, STR)

#define CHADVIS_KARAOKE_FIELDS(X)                     \
    X("enabled", enabled, BOOL)                       \
    X("font_family", fontFamily, STR)                 \
    X("font_size", fontSize, U32)                     \
    X("bold", bold, BOOL)                             \
    X("y_position", yPosition, F32)                   \
    X("active_color", activeColor, COLOR)             \
    X("inactive_color", inactiveColor, COLOR)         \
    X("shadow_color", shadowColor, COLOR)

// download_path and download_format handled manually (see parseSuno).
#define CHADVIS_SUNO_FIELDS(X)                    \
    X("token", token, STR)                        \
    X("cookie", cookie, STR)                      \
    X("auto_download", autoDownload, BOOL)        \
    X("save_lyrics", saveLyrics, BOOL)            \
    X("embed_metadata", embedMetadata, BOOL)      \
    X("debug_lyrics", debugLyrics, BOOL)          \
    X("debug_lyrics_file", debugLyricsFile, PATH)

// ── Parse expansion ──────────────────────────────────────────────────
#define VC_PARSE_FIELD(key, member, TYPE) VC_PARSE_##TYPE(key, member)
#define VC_PARSE_STR(key, member)   obj.member = get(*t, key, def.member);
#define VC_PARSE_BOOL(key, member)  obj.member = get(*t, key, def.member);
#define VC_PARSE_U32(key, member)   obj.member = get(*t, key, def.member);
#define VC_PARSE_I32(key, member)   obj.member = get(*t, key, def.member);
#define VC_PARSE_F32(key, member)   obj.member = get(*t, key, def.member);
#define VC_PARSE_PATH(key, member)  \
    obj.member = expandPath(get(*t, key, def.member.string()));
#define VC_PARSE_COLOR(key, member) \
    obj.member = Color::fromHex(get(*t, key, def.member.toHex()));

// ── Serialize expansion ──────────────────────────────────────────────
#define VC_SER_FIELD(key, member, TYPE) VC_SER_##TYPE(key, member)
#define VC_SER_STR(key, member)   out.insert(key, obj.member);
#define VC_SER_BOOL(key, member)  out.insert(key, obj.member);
#define VC_SER_U32(key, member)   out.insert(key, (i64)obj.member);
#define VC_SER_I32(key, member)   out.insert(key, (i64)obj.member);
#define VC_SER_F32(key, member)   out.insert(key, (double)obj.member);
#define VC_SER_PATH(key, member)  out.insert(key, obj.member.string());
#define VC_SER_COLOR(key, member) out.insert(key, obj.member.toHex());

} // namespace

void ConfigParsers::parseAudio(const toml::table& tbl, AudioConfig& cfg) {
    auto* t = tbl["audio"].as_table();
    if (!t)
        return;
    const AudioConfig def{};
    auto& obj = cfg;
    CHADVIS_AUDIO_FIELDS(VC_PARSE_FIELD)
}

void ConfigParsers::parseVisualizer(const toml::table& tbl,
                                    VisualizerConfig& cfg) {
    auto* t = tbl["visualizer"].as_table();
    if (!t)
        return;

    // preset_path intentionally keeps a runtime fallback instead of the empty
    // struct default: an absent key means "resolve the system presets dir".
    cfg.presetPath =
            expandPath(get(*t,
                           "preset_path",
                           std::string("/usr/share/projectM/presets")));

    const VisualizerConfig def{};
    auto& obj = cfg;
    CHADVIS_VISUALIZER_FIELDS(VC_PARSE_FIELD)

    cfg.width = std::clamp(cfg.width, 160u, 7680u);
    cfg.height = std::clamp(cfg.height, 120u, 4320u);
    cfg.fps = std::clamp(cfg.fps, 10u, 240u);
    cfg.beatSensitivity = std::clamp(cfg.beatSensitivity, 0.1f, 10.0f);
    cfg.smoothPresetDuration = std::clamp(cfg.smoothPresetDuration, 0u, 30u);
    cfg.hardCutSensitivity = std::clamp(cfg.hardCutSensitivity, 0.1f, 10.0f);
    cfg.meshX = std::clamp(cfg.meshX, 8u, 512u);
    cfg.meshY = std::clamp(cfg.meshY, 8u, 512u);

    if (auto paths = (*t)["texture_paths"].as_array()) {
        cfg.texturePaths.clear();
        for (const auto& p : *paths) {
            if (auto s = p.value<std::string>())
                cfg.texturePaths.push_back(expandPath(*s));
        }
    }
}

void ConfigParsers::parseRecording(const toml::table& tbl,
                                   RecordingConfig& cfg) {
    auto* t = tbl["recording"].as_table();
    if (!t)
        return;

    {
        const RecordingConfig def{};
        auto& obj = cfg;
        CHADVIS_RECORDING_FIELDS(VC_PARSE_FIELD)
    }

    // output_directory intentionally keeps its legacy fallback ("~/Videos/
    // ChadVis"); the struct default is empty because loadDefault() resolves
    // a real path at first-run time.
    cfg.outputDirectory =
            expandPath(get(*t, "output_directory", std::string("~/Videos/ChadVis")));

    if (auto* vt = (*t)["video"].as_table()) {
        // Defaults come straight from ConfigData.hpp: crf 18, "medium",
        // 1920x1080@60 (previously diverged: 23/"ultrafast"/1280x720@30).
        const VideoEncoderConfig def{};
        auto& obj = cfg.video;
        auto* t = vt;
        CHADVIS_VIDEO_FIELDS(VC_PARSE_FIELD)

        cfg.video.crf = std::clamp(cfg.video.crf, 0u, 51u);
        cfg.video.width = (std::clamp(cfg.video.width, 160u, 7680u) + 1) & ~1u;
        cfg.video.height = (std::clamp(cfg.video.height, 120u, 4320u) + 1) & ~1u;
        cfg.video.fps = std::clamp(cfg.video.fps, 10u, 120u);
    }

    if (auto* at = (*t)["audio"].as_table()) {
        // Struct default bitrate is 320 (previously diverged: parser said 192).
        const AudioEncoderConfig def{};
        auto& obj = cfg.audio;
        auto* t = at;
        CHADVIS_REC_AUDIO_FIELDS(VC_PARSE_FIELD)

        cfg.audio.bitrate = std::clamp(cfg.audio.bitrate, 64u, 640u);
    }
}

void ConfigParsers::parseOverlay(const toml::table& tbl,
                                 std::vector<OverlayElementConfig>& elements) {
    elements.clear();
    if (auto overlay = tbl["overlay"].as_table()) {
        if (auto elementsArr = (*overlay)["elements"].as_array()) {
            for (const auto& elem : *elementsArr) {
                if (auto elemTbl = elem.as_table()) {
                    OverlayElementConfig cfg;
                    cfg.id = get(*elemTbl, "id", std::string("element"));
                    cfg.text = get(*elemTbl, "text", std::string(""));
                    if (auto pos = (*elemTbl)["position"].as_table())
                        cfg.position = parseVec2(*pos);
                    cfg.fontSize = get(*elemTbl, "font_size", 32u);
                    cfg.color = Color::fromHex(
                            get(*elemTbl, "color", std::string("#FFFFFF")));
                    cfg.opacity = get(*elemTbl, "opacity", 1.0f);
                    cfg.animation =
                            get(*elemTbl, "animation", std::string("none"));
                    cfg.animationSpeed = get(*elemTbl, "animation_speed", 1.0f);
                    cfg.anchor = get(*elemTbl, "anchor", std::string("left"));
                    cfg.visible = get(*elemTbl, "visible", true);
                    elements.push_back(std::move(cfg));
                }
            }
        }
    }
}

void ConfigParsers::parseUI(const toml::table& tbl, UIConfig& cfg) {
    auto* t = tbl["ui"].as_table();
    if (!t)
        return;
    const UIConfig def{};
    auto& obj = cfg;
    CHADVIS_UI_FIELDS(VC_PARSE_FIELD)

    cfg.sidebarWidth = std::clamp(cfg.sidebarWidth, 200, 400);
}

void ConfigParsers::parseKeyboard(const toml::table& tbl, KeyboardConfig& cfg) {
    auto* t = tbl["keyboard"].as_table();
    if (!t)
        return;
    const KeyboardConfig def{};
    auto& obj = cfg;
    CHADVIS_KEYBOARD_FIELDS(VC_PARSE_FIELD)
}

void ConfigParsers::parseKaraoke(const toml::table& tbl, KaraokeConfig& cfg) {
    auto* t = tbl["karaoke"].as_table();
    if (!t)
        return;
    const KaraokeConfig def{};
    auto& obj = cfg;
    CHADVIS_KARAOKE_FIELDS(VC_PARSE_FIELD)
}

void ConfigParsers::parseSuno(const toml::table& tbl, SunoConfig& cfg) {
    auto* t = tbl["suno"].as_table();
    if (!t)
        return;

    const SunoConfig def{};
    auto& obj = cfg;
    CHADVIS_SUNO_FIELDS(VC_PARSE_FIELD)

    auto pathStr = get(*t, "download_path", std::string());
    if (!pathStr.empty())
        cfg.downloadPath = expandPath(pathStr);

    auto fmtStr = get(*t, "download_format", std::string("mp3"));
    cfg.downloadFormat = (fmtStr == "wav") ? SunoDownloadFormat::WAV
                                           : SunoDownloadFormat::MP3;
}

toml::table ConfigParsers::serialize(
        const AudioConfig& audio,
        const VisualizerConfig& visualizer,
        const RecordingConfig& recording,
        const UIConfig& ui,
        const KeyboardConfig& keyboard,
        const SunoConfig& suno,
        const KaraokeConfig& karaoke,
        const std::vector<OverlayElementConfig>& overlayElements,
        bool debug) {
    toml::table root;
    root.insert("general", toml::table{{"debug", debug}});

    {
        toml::table out;
        auto& obj = audio;
        CHADVIS_AUDIO_FIELDS(VC_SER_FIELD)
        root.insert("audio", std::move(out));
    }

    {
        toml::table out;
        auto& obj = visualizer;
        CHADVIS_VISUALIZER_FIELDS(VC_SER_FIELD)
        toml::array pathsArr;
        for (const auto& p : visualizer.texturePaths)
            pathsArr.push_back(p.string());
        out.insert("preset_path", visualizer.presetPath.string());
        out.insert("texture_paths", pathsArr);
        root.insert("visualizer", std::move(out));
    }

    {
        toml::table recOut;
        {
            toml::table out;
            auto& obj = recording.video;
            CHADVIS_VIDEO_FIELDS(VC_SER_FIELD)
            recOut.insert("video", std::move(out));
        }
        {
            toml::table out;
            auto& obj = recording.audio;
            CHADVIS_REC_AUDIO_FIELDS(VC_SER_FIELD)
            recOut.insert("audio", std::move(out));
        }
        auto& obj = recording;
        CHADVIS_RECORDING_FIELDS(VC_SER_FIELD)
        recOut.insert("output_directory", recording.outputDirectory.string());
        root.insert("recording", std::move(recOut));
    }

    toml::array elementsArr;
    for (const auto& elem : overlayElements) {
        elementsArr.push_back(toml::table{
                {"id", elem.id},
                {"text", elem.text},
                {"position",
                 toml::table{{"x", elem.position.x}, {"y", elem.position.y}}},
                {"font_size", (i64)elem.fontSize},
                {"color", elem.color.toHex()},
                {"opacity", (double)elem.opacity},
                {"animation", elem.animation},
                {"animation_speed", (double)elem.animationSpeed},
                {"anchor", elem.anchor},
                {"visible", elem.visible}});
    }
    root.insert("overlay",
                toml::table{{"enabled", true}, {"elements", elementsArr}});

    {
        toml::table out;
        auto& obj = ui;
        CHADVIS_UI_FIELDS(VC_SER_FIELD)
        root.insert("ui", std::move(out));
    }

    {
        toml::table out;
        auto& obj = keyboard;
        CHADVIS_KEYBOARD_FIELDS(VC_SER_FIELD)
        root.insert("keyboard", std::move(out));
    }

    {
        toml::table out;
        auto& obj = suno;
        CHADVIS_SUNO_FIELDS(VC_SER_FIELD)
        out.insert("download_path", suno.downloadPath.string());
        out.insert("download_format",
                   suno.downloadFormat == SunoDownloadFormat::WAV ? "wav"
                                                                  : "mp3");
        root.insert("suno", std::move(out));
    }

    {
        toml::table out;
        auto& obj = karaoke;
        CHADVIS_KARAOKE_FIELDS(VC_SER_FIELD)
        root.insert("karaoke", std::move(out));
    }

    return root;
}

} // namespace vc
