/**
 * @file ConfigParsers.hpp
 * @brief TOML parsing and serialization logic.
 *
 * This file defines the ConfigParsers class which handles the conversion
 * between TOML data structures and the application's C++ configuration structs.
 *
 * @section Dependencies
 * - toml++
 * - ConfigData
 */

#pragma once
#include <toml++/toml.h>
#include "ConfigData.hpp"

namespace vc {

class ConfigParsers {
public:
    static void parseAudio(const toml::table& tbl, AudioConfig& cfg);
    static void parseVisualizer(const toml::table& tbl, VisualizerConfig& cfg);
    static void parseRecording(const toml::table& tbl, RecordingConfig& cfg);
    static void parseOverlay(const toml::table& tbl, OverlayConfig& cfg);
    static void parseUI(const toml::table& tbl, UIConfig& cfg);
    static void parseKeyboard(const toml::table& tbl, KeyboardConfig& cfg);
    static void parseSuno(const toml::table& tbl, SunoConfig& cfg);
    static void parseKaraoke(const toml::table& tbl, KaraokeConfig& cfg);

    /// Range-clamps [visualizer] with the exact bounds parseVisualizer
    /// enforces. Extracted from the parse path so the CLI override lane
    /// (Application::init) can re-apply them after --visualizer-fps/-width/
    /// -height write raw values into CONFIG, past the parser. Idempotent:
    /// already-legal values pass through unchanged.
    static void clampVisualizer(VisualizerConfig& cfg);

    /// Range-clamps [recording.video] with the exact bounds parseRecording
    /// enforces (crf, even-aligned width/height, fps, gop_size, b_frames).
    /// Same rationale as clampVisualizer: --recording-crf writes past the
    /// parser. Idempotent.
    static void clampVideoEncoder(VideoEncoderConfig& cfg);

    /// T0209: range-clamps [audio] (buffer_size, sample_rate). Idempotent.
    static void clampAudio(AudioConfig& cfg);

    /// T0209: range-clamps [karaoke] (font_size 8-256, y_position 0-1).
    /// Idempotent.
    static void clampKaraoke(KaraokeConfig& cfg);

    /// T0209: warns on unknown [recording] strings (container, video codec /
    /// preset / pixel_format) against the sets the encoder actually accepts.
    /// Warning only -- values are left intact.
    static void validateRecording(RecordingConfig& cfg);

    /// T0209: warns on empty [keyboard] shortcuts (they silently never fire).
    static void validateKeyboard(KeyboardConfig& cfg);

    /// Rebuilds the whole document from the structs. Keys absent from the
    /// section field tables are NOT preserved -- see the @section UnknownKeys
    /// note in ConfigParsers.cpp. `suno.token` / `suno.cookie` are always
    /// omitted, whatever they hold.
    static toml::table serialize(const AudioConfig& audio, const VisualizerConfig& visualizer,
                                 const RecordingConfig& recording, const UIConfig& ui,
                                 const KeyboardConfig& keyboard, const SunoConfig& suno,
                                 const KaraokeConfig& karaoke, const OverlayConfig& overlay,
                                 bool debug);
};

} // namespace vc
