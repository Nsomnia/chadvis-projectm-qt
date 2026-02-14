/**
 * @file ArgParser.cpp
 * @brief Command-line argument parser implementation.
 */

#include "ArgParser.hpp"
#include "CliUtils.hpp"
#include <cstdlib>
#include <string_view>

namespace vc {

Result<AppOptions> ArgParser::parse(int argc, char** argv) {
    AppOptions opts;

    for (int i = 1; i < argc; ++i) {
        std::string_view arg(argv[i]);

        if (arg == "-h" || arg == "--help") {
            if (i + 1 < argc) {
                std::string_view nextArg(argv[i + 1]);
                if (nextArg[0] != '-') {
                    auto topic = HelpSystem::parseTopic(std::string(nextArg));
                    if (topic) {
                        HelpSystem::printHelp(*topic);
                        std::exit(0);
                    }
                }
            }
            return Result<AppOptions>::err("--help");
        } else if (arg == "-v" || arg == "--version") {
            return Result<AppOptions>::err("--version");
        } else if (arg == "--help-topics") {
            HelpSystem::listTopics();
            std::exit(0);
        } else if (arg == "--generate-completion") {
            if (i + 1 >= argc) {
                Cli::printError("--generate-completion requires a shell argument (bash/zsh/fish)");
                std::exit(1);
            }
            Cli::generateCompletionScript(argv[++i]);
            std::exit(0);
        } else if (arg == "-d" || arg == "--debug") {
            opts.debug = true;
        } else if (arg == "--headless") {
            opts.headless = true;
        } else if (arg == "-r" || arg == "--record") {
            opts.startRecording = true;
        } else if (arg == "-o" || arg == "--output") {
            if (i + 1 >= argc) {
                Cli::printError("--output requires a path argument");
                return Result<AppOptions>::err("Missing required argument for --output");
            }
            opts.outputFile = fs::path(argv[++i]);
        } else if (arg == "--recording-codec") {
            if (i + 1 >= argc) {
                Cli::printError("--recording-codec requires a codec name");
                return Result<AppOptions>::err("Missing required argument for --recording-codec");
            }
            opts.recordingCodec = argv[++i];
        } else if (arg == "--recording-crf") {
            if (i + 1 >= argc) {
                Cli::printError("--recording-crf requires a quality value (0-51)");
                return Result<AppOptions>::err("Missing required argument for --recording-crf");
            }
            int val;
            if (!tryParseInt(argv[++i], val)) {
                Cli::printError("Invalid CRF value. Use a number between 0-51.");
                return Result<AppOptions>::err("Invalid CRF value");
            }
            opts.recordingCrf = val;
        } else if (arg == "--recording-preset") {
            if (i + 1 >= argc) {
                Cli::printError("--recording-preset requires a preset name");
                return Result<AppOptions>::err("Missing required argument for --recording-preset");
            }
            opts.recordingPreset = argv[++i];
        } else if (arg == "-c" || arg == "--config") {
            if (i + 1 >= argc) {
                Cli::printError("--config requires a path argument");
                return Result<AppOptions>::err("Missing required argument for --config");
            }
            opts.configFile = fs::path(argv[++i]);
        } else if (arg == "-p" || arg == "--preset") {
            if (i + 1 >= argc) {
                Cli::printError("--preset requires a name argument");
                return Result<AppOptions>::err("Missing required argument for --preset");
            }
            opts.presetName = argv[++i];
        } else if (arg == "--default-preset") {
            opts.useDefaultPreset = true;
        } else if (arg == "--visualizer-fps") {
            if (i + 1 >= argc) {
                Cli::printError("--visualizer-fps requires a frame rate");
                return Result<AppOptions>::err("Missing required argument for --visualizer-fps");
            }
            int val;
            if (!tryParseInt(argv[++i], val)) {
                Cli::printError("Invalid FPS value. Use a number like 30, 60, 144.");
                return Result<AppOptions>::err("Invalid FPS value");
            }
            opts.visualizerFps = val;
        } else if (arg == "--visualizer-width") {
            if (i + 1 >= argc) {
                Cli::printError("--visualizer-width requires a width in pixels");
                return Result<AppOptions>::err("Missing required argument for --visualizer-width");
            }
            int val;
            if (!tryParseInt(argv[++i], val)) {
                Cli::printError("Invalid width value.");
                return Result<AppOptions>::err("Invalid width value");
            }
            opts.visualizerWidth = val;
        } else if (arg == "--visualizer-height") {
            if (i + 1 >= argc) {
                Cli::printError("--visualizer-height requires a height in pixels");
                return Result<AppOptions>::err("Missing required argument for --visualizer-height");
            }
            int val;
            if (!tryParseInt(argv[++i], val)) {
                Cli::printError("Invalid height value.");
                return Result<AppOptions>::err("Invalid height value");
            }
            opts.visualizerHeight = val;
        } else if (arg == "--visualizer-shuffle") {
            opts.visualizerShuffle = true;
        } else if (arg == "--no-visualizer-shuffle") {
            opts.visualizerShuffle = false;
        } else if (arg == "--audio-device") {
            if (i + 1 >= argc) {
                Cli::printError("--audio-device requires a device name");
                return Result<AppOptions>::err("Missing required argument for --audio-device");
            }
            opts.audioDevice = argv[++i];
        } else if (arg == "--audio-buffer") {
            if (i + 1 >= argc) {
                Cli::printError("--audio-buffer requires a buffer size");
                return Result<AppOptions>::err("Missing required argument for --audio-buffer");
            }
            int val;
            if (!tryParseInt(argv[++i], val)) {
                Cli::printError("Invalid buffer size. Use 256, 512, 1024, 2048, etc.");
                return Result<AppOptions>::err("Invalid buffer size");
            }
            opts.audioBufferSize = val;
        } else if (arg == "--audio-rate") {
            if (i + 1 >= argc) {
                Cli::printError("--audio-rate requires a sample rate");
                return Result<AppOptions>::err("Missing required argument for --audio-rate");
            }
            int val;
            if (!tryParseInt(argv[++i], val)) {
                Cli::printError("Invalid sample rate. Use 44100, 48000, etc.");
                return Result<AppOptions>::err("Invalid sample rate");
            }
            opts.audioSampleRate = val;
        } else if (arg == "--suno-id") {
            if (i + 1 >= argc) {
                Cli::printError("--suno-id requires a UUID argument");
                return Result<AppOptions>::err("Missing required argument for --suno-id");
            }
            opts.sunoId = argv[++i];
        } else if (arg == "--suno-download-path") {
            if (i + 1 >= argc) {
                Cli::printError("--suno-download-path requires a path argument");
                return Result<AppOptions>::err("Missing required argument for --suno-download-path");
            }
            opts.sunoDownloadPath = fs::path(argv[++i]);
        } else if (arg == "--suno-auto-download") {
            opts.sunoAutoDownload = true;
        } else if (arg == "--no-suno-auto-download") {
            opts.sunoAutoDownload = false;
        } else if (arg == "--test-lyrics") {
            if (i + 1 >= argc) {
                Cli::printError("--test-lyrics requires a path argument");
                return Result<AppOptions>::err("Missing required argument for --test-lyrics");
            }
            opts.testLyricsFile = fs::path(argv[++i]);
        } else if (arg == "--karaoke-enabled") {
            opts.karaokeEnabled = true;
        } else if (arg == "--no-karaoke") {
            opts.karaokeEnabled = false;
        } else if (arg == "--karaoke-font") {
            if (i + 1 >= argc) {
                Cli::printError("--karaoke-font requires a font family name");
                return Result<AppOptions>::err("Missing required argument for --karaoke-font");
            }
            opts.karaokeFont = argv[++i];
        } else if (arg == "--karaoke-font-size") {
            if (i + 1 >= argc) {
                Cli::printError("--karaoke-font-size requires a size in pixels");
                return Result<AppOptions>::err("Missing required argument for --karaoke-font-size");
            }
            int val;
            if (!tryParseInt(argv[++i], val)) {
                Cli::printError("Invalid font size. Use a number like 16, 24, 32.");
                return Result<AppOptions>::err("Invalid font size");
            }
            opts.karaokeFontSize = val;
        } else if (arg == "--karaoke-y-position") {
            if (i + 1 >= argc) {
                Cli::printError("--karaoke-y-position requires a value 0.0-1.0");
                return Result<AppOptions>::err("Missing required argument for --karaoke-y-position");
            }
            float val;
            if (!tryParseFloat(argv[++i], val)) {
                Cli::printError("Invalid position. Use a number between 0.0 and 1.0.");
                return Result<AppOptions>::err("Invalid Y position");
            }
            opts.karaokeYPosition = val;
        } else if (arg == "--theme") {
            if (i + 1 >= argc) {
                Cli::printError("--theme requires a theme name");
                return Result<AppOptions>::err("Missing required argument for --theme");
            }
            opts.theme = argv[++i];
        } else if (arg[0] != '-') {
            opts.inputFiles.push_back(fs::path(arg));
        } else {
            auto suggestion = Cli::findClosestMatch(arg, {
                "--help", "--version", "--debug", "--headless",
                "--config", "--preset", "--default-preset",
                "--record", "--output", "--recording-codec",
                "--visualizer-fps", "--visualizer-width", "--visualizer-height",
                "--audio-device", "--audio-buffer", "--audio-rate",
                "--suno-id", "--suno-download-path",
                "--karaoke-enabled", "--karaoke-font", "--karaoke-font-size",
                "--theme", "--test-lyrics"
            });

            if (suggestion) {
                Cli::printUnknownFlagError(arg, {*suggestion});
            } else {
                Cli::printError(std::string("Unknown option: ") + std::string(arg));
            }
            return Result<AppOptions>::err(std::string("Unknown option: ") + std::string(arg));
        }
    }

    return Result<AppOptions>::ok(std::move(opts));
}

bool ArgParser::tryParseInt(const char* str, int& out) {
    try {
        out = std::stoi(str);
        return true;
    } catch (...) {
        return false;
    }
}

bool ArgParser::tryParseFloat(const char* str, float& out) {
    try {
        out = std::stof(str);
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace vc
