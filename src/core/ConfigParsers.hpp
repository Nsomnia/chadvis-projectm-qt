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

    /// Rebuilds the whole document from the structs. Keys absent from the
    /// section field tables are NOT preserved -- see the @section UnknownKeys
    /// note in ConfigParsers.cpp. `suno.token` / `suno.cookie` are always
    /// omitted, whatever they hold.
    static toml::table serialize(
            const AudioConfig& audio,
            const VisualizerConfig& visualizer,
            const RecordingConfig& recording,
            const UIConfig& ui,
            const KeyboardConfig& keyboard,
            const SunoConfig& suno,
            const KaraokeConfig& karaoke,
            const OverlayConfig& overlay,
            bool debug);
};

} // namespace vc
