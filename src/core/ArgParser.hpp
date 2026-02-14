/**
 * @file ArgParser.hpp
 * @brief Command-line argument parser.
 *
 * Single responsibility: Parse argc/argv into structured AppOptions.
 * Delegates error formatting to CliUtils.
 */

#pragma once
#include "util/Result.hpp"
#include "util/Types.hpp"
#include <optional>
#include <string>
#include <vector>

namespace vc {

struct AppOptions {
    bool debug{false};
    bool headless{false};
    std::optional<fs::path> configFile;
    std::vector<fs::path> inputFiles;

    std::optional<std::string> presetName;
    bool useDefaultPreset{false};
    std::optional<int> visualizerFps;
    std::optional<int> visualizerWidth;
    std::optional<int> visualizerHeight;
    std::optional<bool> visualizerShuffle;

    bool startRecording{false};
    std::optional<fs::path> outputFile;
    std::optional<std::string> recordingCodec;
    std::optional<int> recordingCrf;
    std::optional<std::string> recordingPreset;

    std::optional<std::string> audioDevice;
    std::optional<int> audioBufferSize;
    std::optional<int> audioSampleRate;

    std::optional<std::string> sunoId;
    std::optional<fs::path> sunoDownloadPath;
    std::optional<bool> sunoAutoDownload;

    std::optional<fs::path> testLyricsFile;
    std::optional<bool> karaokeEnabled;
    std::optional<std::string> karaokeFont;
    std::optional<int> karaokeFontSize;
    std::optional<float> karaokeYPosition;

    std::optional<std::string> theme;
};

class ArgParser {
public:
    static Result<AppOptions> parse(int argc, char** argv);

private:
    static bool tryParseInt(const char* str, int& out);
    static bool tryParseFloat(const char* str, float& out);
};

} // namespace vc
