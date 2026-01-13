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
} // namespace

void ConfigParsers::parseAudio(const toml::table& tbl, AudioConfig& cfg) {
    if (auto audio = tbl["audio"].as_table()) {
        cfg.device = get(*audio, "device", std::string("default"));
        cfg.bufferSize = get(*audio, "buffer_size", 2048u);
        cfg.sampleRate = get(*audio, "sample_rate", 44100u);
    }
}

void ConfigParsers::parseVisualizer(const toml::table& tbl,
                                    VisualizerConfig& cfg) {
    if (auto viz = tbl["visualizer"].as_table()) {
        auto pathStr = get(*viz,
                           "preset_path",
                           std::string("/usr/share/projectM/presets"));
        cfg.presetPath = expandPath(pathStr);
        cfg.width = std::clamp(get(*viz, "width", 1280u), 320u, 7680u);
        cfg.height = std::clamp(get(*viz, "height", 720u), 200u, 4320u);
        cfg.fps = std::clamp(get(*viz, "fps", 30u), 10u, 240u);
        cfg.beatSensitivity =
                std::clamp(get(*viz, "beat_sensitivity", 1.0f), 0.1f, 10.0f);
        cfg.presetDuration = get(*viz, "preset_duration", 30u);
        cfg.smoothPresetDuration =
                std::clamp(get(*viz, "smooth_preset_duration", 5u), 0u, 30u);
        cfg.shufflePresets = get(*viz, "shuffle_presets", true);
        cfg.forcePreset = get(*viz, "force_preset", std::string());
        cfg.useDefaultPreset = get(*viz, "use_default_preset", false);
        cfg.lowResourceMode = get(*viz, "low_resource_mode", false);

        if (auto paths = (*viz)["texture_paths"].as_array()) {
            cfg.texturePaths.clear();
            for (const auto& p : *paths) {
                if (auto s = p.value<std::string>())
                    cfg.texturePaths.push_back(expandPath(*s));
            }
        }
    }
}

void ConfigParsers::parseRecording(const toml::table& tbl,
                                   RecordingConfig& cfg) {
    if (auto rec = tbl["recording"].as_table()) {
        cfg.enabled = get(*rec, "enabled", true);
        cfg.autoRecord = get(*rec, "auto_record", false);
        cfg.recordEntireSong = get(*rec, "record_entire_song", false);
        cfg.restartTrackOnRecord = get(*rec, "restart_track_on_record", false);
        cfg.stopAtTrackEnd = get(*rec, "stop_at_track_end", false);
        auto outDir =
                get(*rec, "output_directory", std::string("~/Videos/ChadVis"));
        cfg.outputDirectory = expandPath(outDir);
        cfg.defaultFilename =
                get(*rec,
                    "default_filename",
                    std::string("chadvis-projectm-qt_{date}_{time}"));
        cfg.container = get(*rec, "container", std::string("mp4"));

        if (auto video = (*rec)["video"].as_table()) {
            cfg.video.codec = get(*video, "codec", std::string("libx264"));
            cfg.video.crf = std::clamp(get(*video, "crf", 23u), 0u, 51u);
            cfg.video.preset = get(*video, "preset", std::string("ultrafast"));
            cfg.video.pixelFormat =
                    get(*video, "pixel_format", std::string("yuv420p"));
            cfg.video.width =
                    (std::clamp(get(*video, "width", 1280u), 320u, 7680u) + 1) &
                    ~1;
            cfg.video.height =
                    (std::clamp(get(*video, "height", 720u), 200u, 4320u) + 1) &
                    ~1;
            cfg.video.fps = std::clamp(get(*video, "fps", 30u), 10u, 120u);
        }

        if (auto audio = (*rec)["audio"].as_table()) {
            cfg.audio.codec = get(*audio, "codec", std::string("aac"));
            cfg.audio.bitrate =
                    std::clamp(get(*audio, "bitrate", 192u), 64u, 640u);
        }
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
    if (auto uiTbl = tbl["ui"].as_table()) {
        cfg.theme = get(*uiTbl, "theme", std::string("dark"));
        cfg.showPlaylist = get(*uiTbl, "show_playlist", true);
        cfg.showPresets = get(*uiTbl, "show_presets", true);
        cfg.showDebugPanel = get(*uiTbl, "show_debug_panel", false);
        cfg.backgroundColor = Color::fromHex(
                get(*uiTbl, "visualizer_background", std::string("#000000")));
        cfg.accentColor = Color::fromHex(
                get(*uiTbl, "accent_color", std::string("#00FF88")));
    }
}

void ConfigParsers::parseKeyboard(const toml::table& tbl, KeyboardConfig& cfg) {
    if (auto kb = tbl["keyboard"].as_table()) {
        cfg.playPause = get(*kb, "play_pause", std::string("Space"));
        cfg.nextTrack = get(*kb, "next_track", std::string("N"));
        cfg.prevTrack = get(*kb, "prev_track", std::string("P"));
        cfg.toggleRecord = get(*kb, "toggle_record", std::string("R"));
        cfg.toggleFullscreen = get(*kb, "toggle_fullscreen", std::string("F"));
        cfg.nextPreset = get(*kb, "next_preset", std::string("Right"));
        cfg.prevPreset = get(*kb, "prev_preset", std::string("Left"));
    }
}

void ConfigParsers::parseSuno(const toml::table& tbl, SunoConfig& cfg) {
    if (auto suno = tbl["suno"].as_table()) {
        cfg.token = get(*suno, "token", std::string());
        cfg.cookie = get(*suno, "cookie", std::string());
        auto pathStr = get(*suno, "download_path", std::string());
        if (!pathStr.empty())
            cfg.downloadPath = expandPath(pathStr);
        cfg.autoDownload = get(*suno, "auto_download", false);
        cfg.saveLyrics = get(*suno, "save_lyrics", true);
        cfg.embedMetadata = get(*suno, "embed_metadata", true);
    }
}

toml::table ConfigParsers::serialize(
        const AudioConfig& audio,
        const VisualizerConfig& visualizer,
        const RecordingConfig& recording,
        const UIConfig& ui,
        const KeyboardConfig& keyboard,
        const SunoConfig& suno,
        const std::vector<OverlayElementConfig>& overlayElements,
        bool debug) {
    toml::table root;
    root.insert("general", toml::table{{"debug", debug}});
    root.insert("audio",
                toml::table{{"device", audio.device},
                            {"buffer_size", (i64)audio.bufferSize},
                            {"sample_rate", (i64)audio.sampleRate}});

    toml::table vizTbl{
            {"preset_path", visualizer.presetPath.string()},
            {"width", (i64)visualizer.width},
            {"height", (i64)visualizer.height},
            {"fps", (i64)visualizer.fps},
            {"beat_sensitivity", (double)visualizer.beatSensitivity},
            {"preset_duration", (i64)visualizer.presetDuration},
            {"smooth_preset_duration", (i64)visualizer.smoothPresetDuration},
            {"shuffle_presets", visualizer.shufflePresets},
            {"force_preset", visualizer.forcePreset},
            {"use_default_preset", visualizer.useDefaultPreset},
            {"low_resource_mode", visualizer.lowResourceMode}};
    toml::array pathsArr;
    for (const auto& p : visualizer.texturePaths)
        pathsArr.push_back(p.string());
    vizTbl.insert("texture_paths", pathsArr);
    root.insert("visualizer", vizTbl);

    toml::table recVideo{{"codec", recording.video.codec},
                         {"crf", (i64)recording.video.crf},
                         {"preset", recording.video.preset},
                         {"pixel_format", recording.video.pixelFormat},
                         {"width", (i64)recording.video.width},
                         {"height", (i64)recording.video.height},
                         {"fps", (i64)recording.video.fps}};
    toml::table recAudio{{"codec", recording.audio.codec},
                         {"bitrate", (i64)recording.audio.bitrate}};
    root.insert(
            "recording",
            toml::table{
                    {"enabled", recording.enabled},
                    {"auto_record", recording.autoRecord},
                    {"record_entire_song", recording.recordEntireSong},
                    {"restart_track_on_record", recording.restartTrackOnRecord},
                    {"stop_at_track_end", recording.stopAtTrackEnd},
                    {"output_directory", recording.outputDirectory.string()},
                    {"default_filename", recording.defaultFilename},
                    {"container", recording.container},
                    {"video", recVideo},
                    {"audio", recAudio}});

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

    root.insert(
            "ui",
            toml::table{{"theme", ui.theme},
                        {"show_playlist", ui.showPlaylist},
                        {"show_presets", ui.showPresets},
                        {"show_debug_panel", ui.showDebugPanel},
                        {"visualizer_background", ui.backgroundColor.toHex()},
                        {"accent_color", ui.accentColor.toHex()}});

    root.insert("keyboard",
                toml::table{{"play_pause", keyboard.playPause},
                            {"next_track", keyboard.nextTrack},
                            {"prev_track", keyboard.prevTrack},
                            {"toggle_record", keyboard.toggleRecord},
                            {"toggle_fullscreen", keyboard.toggleFullscreen},
                            {"next_preset", keyboard.nextPreset},
                            {"prev_preset", keyboard.prevPreset}});

    root.insert("suno",
                toml::table{{"token", suno.token},
                            {"cookie", suno.cookie},
                            {"download_path", suno.downloadPath.string()},
                            {"auto_download", suno.autoDownload},
                            {"save_lyrics", suno.saveLyrics},
                            {"embed_metadata", suno.embedMetadata}});

    return root;
}

} // namespace vc
