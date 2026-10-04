/**
 * @file ConfigParsers.cpp
 * @brief TOML parsing and serialization logic.
 *
 * Field tables are the single source of truth: every TOML key appears exactly
 * once in a section table and expands into BOTH parse and serialize code via
 * VC_PARSE_FIELD / VC_SER_FIELD. Parse fallbacks are read from a
 * default-constructed config struct, so parser defaults can never drift from
 * the initializers in ConfigData.hpp (the source of truth).
 *
 * @section UnknownKeys
 * serialize REBUILDS the root table from the structs, so a key that is not in
 * a field table (and not one of the five hand-handled keys below) does not
 * survive a save. That is a deliberate, tested decision rather than an
 * accident -- see tests/unit/core/test_ConfigLoader.cpp. The keys handled by
 * hand are `visualizer.preset_path`, `visualizer.texture_paths`,
 * `recording.output_directory`, `suno.download_path` and
 * `suno.download_format`, each because its parse side needs different
 * fallback behaviour from a plain `get`.
 *
 * The corollary is the rule to remember when adding a field: a struct member
 * with no table entry is a value that vanishes on the next save, silently and
 * with no warning. Four such members shipped -- `VideoEncoderConfig::gopSize`
 * and `bFrames`, `AudioEncoderConfig::sampleRate` and `channels` -- and because
 * their defaults were already the values anyone would have configured, nothing
 * observable changed and the loss was invisible.
 *
 * @section Secrets
 * `suno.token` and `suno.cookie` are parse-only. See
 * CHADVIS_SUNO_LEGACY_SECRET_FIELDS for why they are not in the serialized
 * table at all.
 */
#include "ConfigParsers.hpp"
#include <algorithm>
#include <QDir>
#include "Logger.hpp"
#include "util/FileUtils.hpp"

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
    // QDir::homePath() instead of getenv("HOME"): honors USERPROFILE on
    // Windows and needs no environment variable to be set.
    const QByteArray home = QDir::homePath().toUtf8();
    std::string p(path);
    if (p == "~") {
        p = home.toStdString();
    } else if (p.starts_with("~/")) {
        p = home.toStdString() + p.substr(1);
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

// gop_size and b_frames were absent from this table for the life of the project,
// which meant ConfigParsers::serialize -- which rebuilds the whole root table
// from the structs, so anything not in a table does not survive a save --
// silently dropped both on every save. Same class of bug as `[recording.video]`
// being emitted empty, and the reason it went unnoticed is the same: a default
// value is indistinguishable from a value that round-tripped.
#define CHADVIS_VIDEO_FIELDS(X)                                                                    \
    X("codec", codec, STR)                                                                         \
    X("crf", crf, U32)                                                                             \
    X("preset", preset, STR)                                                                       \
    X("pixel_format", pixelFormat, STR)                                                            \
    X("width", width, U32)                                                                         \
    X("height", height, U32)                                                                       \
    X("fps", fps, U32)                                                                             \
    X("gop_size", gopSize, U32)                                                                    \
    X("b_frames", bFrames, U32)

// sample_rate and channels are the encoder's OUTPUT spec, not the source
// file's -- see the note on AudioEncoderConfig. Absent from this table for the
// same reason and with the same consequence as gop_size / b_frames above.
#define CHADVIS_REC_AUDIO_FIELDS(X)                                                                \
    X("codec", codec, STR)                                                                         \
    X("bitrate", bitrate, U32)                                                                     \
    X("sample_rate", sampleRate, U32)                                                              \
    X("channels", channels, U32)

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
    X("device_id", deviceId, STR)                 \
    X("auto_download", autoDownload, BOOL)        \
    X("save_lyrics", saveLyrics, BOOL)            \
    X("embed_metadata", embedMetadata, BOOL)      \
    X("debug_lyrics", debugLyrics, BOOL)          \
    X("debug_lyrics_file", debugLyricsFile, PATH)

// Legacy Clerk credential -- READ ONLY, and deliberately not part of
// CHADVIS_SUNO_FIELDS.
//
// This table is expanded by parseSuno and by nothing else. Splitting it out is
// what makes "the serializer cannot emit a secret" a structural property rather
// than a convention: serialize expands CHADVIS_SUNO_FIELDS, and there is no
// expansion left that reaches `token` or `cookie`. Do not merge the two tables
// back together to "simplify" them, and do not add a new secret here -- a new
// secret belongs in CredentialStore, never in config.toml.
//
// Values found here are used once, by SunoClient's startup migration into
// CredentialStore. Because serialize rebuilds the entire file, the first save
// after this fix removes them from disk; parseSuno logs a warning when it sees
// one so that is never silent.
#define CHADVIS_SUNO_LEGACY_SECRET_FIELDS(X) \
    X("token", token, STR)                    \
    X("cookie", cookie, STR)

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

    // preset_path intentionally keeps a runtime fallback instead of the empty
    // struct default. An absent key or an explicit empty string ('' in TOML)
    // both mean "auto-detect the system presets dir" via FileUtils; only a
    // non-empty configured path is used literally.
    const std::string rawPresetPath =
            t ? get(*t, "preset_path", std::string()) : std::string();
    cfg.presetPath =
            rawPresetPath.empty() ? file::presetsDir() : expandPath(rawPresetPath);

    if (!t)
        return;

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
        // 0 means "let the encoder choose" for gop_size -- VideoRecorderFFmpeg
        // substitutes fps * 2 -- so 0 must survive untouched and only an absurd
        // value is pulled back to auto. b_frames is bounded absolutely rather
        // than against gop_size: gop_size 0 means "auto", and clamping against it
        // would silently disable B-frames for every user who left the GOP alone,
        // which is a behaviour change masquerading as validation. 16 is generous
        // (x264's own default is 3) and beyond it no decoder gains anything.
        cfg.video.gopSize = cfg.video.gopSize > 600u ? 0u : cfg.video.gopSize;
        cfg.video.bFrames = std::min(cfg.video.bFrames, 16u);
    }

    if (auto* at = (*t)["audio"].as_table()) {
        // Struct default bitrate is 320 (previously diverged: parser said 192).
        const AudioEncoderConfig def{};
        auto& obj = cfg.audio;
        auto* t = at;
        CHADVIS_REC_AUDIO_FIELDS(VC_PARSE_FIELD)

        cfg.audio.bitrate = std::clamp(cfg.audio.bitrate, 64u, 640u);
        // The output spec, so these are the rates and layouts the encoder is
        // *opened* at rather than anything read off an input -- see the note on
        // AudioEncoderConfig. Two is the channel cap and it is not a taste
        // decision: AudioQueue::AudioFrame is stereo-only, so a wider stream
        // could not reach a consumer intact.
        //
        // The rate bounds follow the material this project actually renders:
        // AudioQueue feeds 48 kHz, the lowest rate the decoder is tested against
        // is 8 kHz, and nothing above 192 kHz is supported by any codec in the
        // output set. Below 8 kHz a value is a typo rather than an intention --
        // swr would accept 1000 Hz and produce a file no one can use.
        cfg.audio.sampleRate = std::clamp(cfg.audio.sampleRate, 8000u, 192000u);
        cfg.audio.channels = std::clamp(cfg.audio.channels, 1u, 2u);
    }
}

void ConfigParsers::parseOverlay(const toml::table& tbl, OverlayConfig& cfg) {
    cfg.elements.clear();
    auto* overlay = tbl["overlay"].as_table();
    if (!overlay)
        return;

    const OverlayConfig def{};
    cfg.enabled = get(*overlay, "enabled", def.enabled);

    if (auto elementsArr = (*overlay)["elements"].as_array()) {
        for (const auto& elem : *elementsArr) {
            if (auto elemTbl = elem.as_table()) {
                OverlayElementConfig element;
                element.id = get(*elemTbl, "id", std::string("element"));
                element.text = get(*elemTbl, "text", std::string(""));
                if (auto pos = (*elemTbl)["position"].as_table())
                    element.position = parseVec2(*pos);
                element.fontSize = get(*elemTbl, "font_size", 32u);
                element.color = Color::fromHex(
                        get(*elemTbl, "color", std::string("#FFFFFF")));
                element.opacity = get(*elemTbl, "opacity", 1.0f);
                element.animation =
                        get(*elemTbl, "animation", std::string("none"));
                element.animationSpeed =
                        get(*elemTbl, "animation_speed", 1.0f);
                element.anchor = get(*elemTbl, "anchor", std::string("left"));
                element.visible = get(*elemTbl, "visible", true);
                cfg.elements.push_back(std::move(element));
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

    // Read-only. The serializer has no expansion reaching these two members,
    // so this is the only place a raw Clerk credential is ever read.
    CHADVIS_SUNO_LEGACY_SECRET_FIELDS(VC_PARSE_FIELD)
    if (!cfg.token.empty() || !cfg.cookie.empty()) {
        LOG_WARN("Config [suno] still carries a legacy {}: it is used once to migrate into "
                 "secure storage, then the next settings save removes it from this file",
                 cfg.cookie.empty() ? "token" : "cookie");
    }

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
        const OverlayConfig& overlay,
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
        // VC_SER_* expands to `out.insert(...)`, so the destination table has to
        // be named `out` -- which is why the section table is `recordingOut` and
        // each group shadows the name. This used to build the two encoder
        // sub-tables in tables named `videoOut`/`audioOut`, which the macro never
        // touched: both sub-tables were written EMPTY, and every encoder key went
        // flat into [recording] instead, where video.codec and audio.codec
        // collided on one key. The parser then read an empty [recording.video]
        // and reset every encoder setting to its struct default, so no codec,
        // crf, resolution, fps, preset or bitrate survived a save. Pinned by
        // TestConfigLoader::roundTripPreservesEveryField.
        toml::table recordingOut;
        {
            toml::table out;
            auto& obj = recording.video;
            CHADVIS_VIDEO_FIELDS(VC_SER_FIELD)
            recordingOut.insert("video", std::move(out));
        }
        {
            toml::table out;
            auto& obj = recording.audio;
            CHADVIS_REC_AUDIO_FIELDS(VC_SER_FIELD)
            recordingOut.insert("audio", std::move(out));
        }
        {
            toml::table out;
            auto& obj = recording;
            CHADVIS_RECORDING_FIELDS(VC_SER_FIELD)
            out.insert("output_directory", recording.outputDirectory.string());
            for (const auto& entry : out)
                recordingOut.insert(entry.first, entry.second);
        }
        root.insert("recording", std::move(recordingOut));
    }

    toml::array elementsArr;
    for (const auto& elem : overlay.elements) {
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
                toml::table{{"enabled", overlay.enabled},
                            {"elements", elementsArr}});

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
