/**
 * @file CliUtils.cpp
 * @brief Implementation of CLI utilities with color support.
 */

#include "CliUtils.hpp"
#include <cstdlib>
#include <algorithm>
#include <sstream>
#include <unistd.h>
#include <iomanip>

namespace vc {

bool CliColor::shouldUseColor() {
    // Respect NO_COLOR environment variable
    if (const char* noColor = std::getenv("NO_COLOR")) {
        if (noColor[0] != '\0') {
            return false;
        }
    }
    
    // Check if stdout is a TTY
    if (!isatty(STDOUT_FILENO)) {
        return false;
    }
    
    return true;
}

namespace Cli {

void printError(std::string_view message) {
    std::cerr << CliColor::brightRed() << "Error: " << CliColor::reset()
              << CliColor::red() << message << CliColor::reset() << "\n";
}

void printWarning(std::string_view message) {
    std::cerr << CliColor::brightYellow() << "Warning: " << CliColor::reset()
              << CliColor::yellow() << message << CliColor::reset() << "\n";
}

void printSuccess(std::string_view message) {
    std::cout << CliColor::brightGreen() << "✓ " << CliColor::reset()
              << message << "\n";
}

void printInfo(std::string_view message) {
    std::cout << CliColor::brightBlue() << "ℹ " << CliColor::reset()
              << message << "\n";
}

void printHeader(std::string_view title) {
    std::cout << "\n" << CliColor::brightCyan() << CliColor::bold()
              << "╔════════════════════════════════════════════════════════════╗" << CliColor::reset() << "\n"
              << CliColor::brightCyan() << CliColor::bold()
              << "║ " << CliColor::reset() << CliColor::brightWhite() << CliColor::bold()
              << std::left << std::setw(58) << title 
              << CliColor::reset() << CliColor::brightCyan() << CliColor::bold() << "║" << CliColor::reset() << "\n"
              << CliColor::brightCyan() << CliColor::bold()
              << "╚════════════════════════════════════════════════════════════╝" << CliColor::reset() << "\n";
}

void printSection(std::string_view title) {
    std::cout << "\n" << CliColor::brightMagenta() << CliColor::bold() << "▸ " 
              << CliColor::reset() << CliColor::bold() << title 
              << CliColor::reset() << "\n";
}

void printOption(std::string_view flags, std::string_view description,
                 std::optional<std::string_view> defaultVal) {
    std::cout << "  " << CliColor::brightGreen() << std::left << std::setw(25) << flags 
              << CliColor::reset() << " " << description;
    
    if (defaultVal) {
        std::cout << CliColor::dim() << " [default: " << *defaultVal << "]" << CliColor::reset();
    }
    
    std::cout << "\n";
}

void printUnknownFlagError(std::string_view flag,
                           std::initializer_list<std::string_view> suggestions) {
    printError(std::string("Unknown flag: ") + std::string(flag));
    
    if (suggestions.size() > 0) {
        std::cout << CliColor::yellow() << "  Did you mean:" << CliColor::reset() << "\n";
        for (const auto& suggestion : suggestions) {
            std::cout << "    " << CliColor::green() << suggestion << CliColor::reset() << "\n";
        }
    }
}

std::optional<std::string> findClosestMatch(
    std::string_view input,
    std::initializer_list<std::string_view> candidates) {
    
    std::optional<std::string> bestMatch;
    size_t bestDistance = std::numeric_limits<size_t>::max();
    
    for (const auto& candidate : candidates) {
        // Simple Levenshtein distance approximation for short strings
        size_t distance = 0;
        size_t minLen = std::min(input.length(), candidate.length());
        
        for (size_t i = 0; i < minLen; ++i) {
            if (input[i] != candidate[i]) {
                distance++;
            }
        }
        
        distance += std::abs(static_cast<int>(input.length()) - static_cast<int>(candidate.length()));
        
        // Only suggest if reasonably close (max 3 differences for typical flags)
        if (distance < bestDistance && distance <= 3) {
            bestDistance = distance;
            bestMatch = std::string(candidate);
        }
    }
    
    return bestMatch;
}

std::string formatPath(const std::filesystem::path& path) {
    return std::string(CliColor::brightCyan()) + path.string() + CliColor::reset();
}

void generateCompletionScript(std::string_view shell) {
    if (shell == "bash") {
        std::cout << R"(
# ChadVis Bash Completion
# Source this file: source /path/to/chadvis-completion.bash

_chadvis_complete() {
    local cur prev opts
    COMPREPLY=()
    cur="${COMP_WORDS[COMP_CWORD]}"
    prev="${COMP_WORDS[COMP_CWORD-1]}"
    
    opts="--help --version --debug --config --preset --default-preset
          --record --output --headless --test-lyrics --suno-id
          --audio-device --audio-buffer --visualizer-fps --visualizer-width
          --visualizer-height --recording-codec --recording-crf"
    
    case "${prev}" in
        --config|--output|--test-lyrics)
            COMPREPLY=( $(compgen -f -- "${cur}") )
            return 0
            ;;
        --preset)
            # Could complete preset names if available
            return 0
            ;;
        *)
            ;;
    esac
    
    COMPREPLY=( $(compgen -W "${opts}" -- "${cur}") )
    return 0
}

complete -F _chadvis_complete chadvis-projectm-qt
)";
    } else if (shell == "zsh") {
        std::cout << R"(
# ChadVis Zsh Completion
# Add to ~/.zshrc or place in $fpath

#compdef chadvis-projectm-qt

_arguments \
    '(-h --help)'{-h,--help}'[Show help message]' \
    '(-v --version)'{-v,--version}'[Show version information]' \
    '(-d --debug)'{-d,--debug}'[Enable debug logging]' \
    '--headless[Run without GUI]' \
    '(-c --config)'{-c,--config}'[Config file path]:config file:_files' \
    '(-p --preset)'{-p,--preset}'[Visualizer preset]:preset:' \
    '--default-preset[Use default projectM preset]' \
    '(-r --record)'{-r,--record}'[Start recording immediately]' \
    '(-o --output)'{-o,--output}'[Recording output path]:output file:_files' \
    '--test-lyrics[Test lyrics file]:lyrics file:_files' \
    '--suno-id[Fetch Suno song by ID]:Suno UUID:' \
    '--audio-device[Audio device]:device:' \
    '--audio-buffer[Audio buffer size]:size:' \
    '*:audio file:_files'
)";
    } else if (shell == "fish") {
        std::cout << R"(
# ChadVis Fish Completion
# Place in ~/.config/fish/completions/chadvis-projectm-qt.fish

complete -c chadvis-projectm-qt -s h -l help -d "Show help message"
complete -c chadvis-projectm-qt -s v -l version -d "Show version information"
complete -c chadvis-projectm-qt -s d -l debug -d "Enable debug logging"
complete -c chadvis-projectm-qt -l headless -d "Run without GUI"
complete -c chadvis-projectm-qt -s c -l config -d "Config file path" -r
complete -c chadvis-projectm-qt -s p -l preset -d "Visualizer preset"
complete -c chadvis-projectm-qt -l default-preset -d "Use default projectM preset"
complete -c chadvis-projectm-qt -s r -l record -d "Start recording immediately"
complete -c chadvis-projectm-qt -s o -l output -d "Recording output path" -r
complete -c chadvis-projectm-qt -l test-lyrics -d "Test lyrics file" -r
complete -c chadvis-projectm-qt -l suno-id -d "Fetch Suno song by ID"
)";
    } else {
        std::cerr << "Unknown shell: " << shell << "\n";
        std::cerr << "Supported shells: bash, zsh, fish\n";
    }
}

} // namespace Cli

// HelpSystem implementation
void HelpSystem::printHelp(Topic topic) {
    using enum Topic;
    
    switch (topic) {
    case General:
        Cli::printHeader("ChadVis Help - General Usage");
        std::cout << "\n" << CliColor::bold() << "Basic Usage:" << CliColor::reset() << "\n"
                  << "  chadvis-projectm-qt [options] [files...]\n";
        
        Cli::printSection("Common Options");
        Cli::printOption("-h, --help", "Show help message");
        Cli::printOption("-v, --version", "Show version information");
        Cli::printOption("-d, --debug", "Enable debug logging");
        Cli::printOption("-c, --config <path>", "Use custom config file");
        Cli::printOption("--headless", "Run without GUI (batch mode)");
        
        Cli::printSection("Keyboard Shortcuts");
        std::cout << "  " << CliColor::brightYellow() << "Space" << CliColor::reset() << "  Play/Pause\n"
                  << "  " << CliColor::brightYellow() << "N" << CliColor::reset() << "      Next track\n"
                  << "  " << CliColor::brightYellow() << "P" << CliColor::reset() << "      Previous track\n"
                  << "  " << CliColor::brightYellow() << "R" << CliColor::reset() << "      Toggle recording\n"
                  << "  " << CliColor::brightYellow() << "F" << CliColor::reset() << "      Toggle fullscreen\n"
                  << "  " << CliColor::brightYellow() << "← →" << CliColor::reset() << "    Previous/Next preset\n";
        break;
        
    case Audio:
        Cli::printHeader("ChadVis Help - Audio Configuration");
        Cli::printSection("CLI Options");
        Cli::printOption("--audio-device <name>", "Audio output device", "default");
        Cli::printOption("--audio-buffer <size>", "Buffer size in samples", "2048");
        Cli::printOption("--audio-rate <rate>", "Sample rate in Hz", "44100");
        
        Cli::printSection("Config File");
        std::cout << "  [audio]\n"
                  << "  device = \"default\"\n"
                  << "  buffer_size = 2048\n"
                  << "  sample_rate = 44100\n";
        break;
        
    case Visualizer:
        Cli::printHeader("ChadVis Help - Visualizer Configuration");
        Cli::printSection("CLI Options");
        Cli::printOption("-p, --preset <name>", "Load specific preset on startup");
        Cli::printOption("--default-preset", "Use projectM default (no preset file)");
        Cli::printOption("--visualizer-fps <n>", "Target frame rate", "60");
        Cli::printOption("--visualizer-width <n>", "Render width", "1920");
        Cli::printOption("--visualizer-height <n>", "Render height", "1080");
        Cli::printOption("--visualizer-shuffle", "Shuffle presets randomly", "yes");
        
        Cli::printSection("Config File");
        std::cout << "  [visualizer]\n"
                  << "  preset_path = \"/usr/share/projectM/presets\"\n"
                  << "  fps = 60\n"
                  << "  width = 1920\n"
                  << "  height = 1080\n"
                  << "  shuffle_presets = true\n";
        break;
        
    case Recording:
        Cli::printHeader("ChadVis Help - Video Recording");
        Cli::printSection("CLI Options");
        Cli::printOption("-r, --record", "Start recording immediately on launch");
        Cli::printOption("-o, --output <path>", "Output file path");
        Cli::printOption("--recording-codec <codec>", "Video codec (libx264/libx265/nvenc)", "libx264");
        Cli::printOption("--recording-crf <n>", "Quality (0-51, lower=better)", "18");
        Cli::printOption("--recording-preset <name>", "Encoding speed (ultrafast to veryslow)", "medium");
        
        Cli::printSection("Hardware Acceleration");
        std::cout << "  " << CliColor::brightGreen() << "NVENC" << CliColor::reset() << "  (NVIDIA)   - Use codec 'h264_nvenc' or 'hevc_nvenc'\n"
                  << "  " << CliColor::brightGreen() << "VAAPI" << CliColor::reset() << "  (AMD/Intel) - Use codec 'h264_vaapi' or 'hevc_vaapi'\n"
                  << "  " << CliColor::dim() << "Check available codecs: ffmpeg -encoders | grep -E '(nvenc|vaapi)'" << CliColor::reset() << "\n";
        break;
        
    case Suno:
        Cli::printHeader("ChadVis Help - Suno AI Integration");
        Cli::printSection("CLI Options");
        Cli::printOption("--suno-id <uuid>", "Fetch and play a Suno song by ID");
        Cli::printOption("--suno-download-path <path>", "Download directory for Suno songs");
        Cli::printOption("--suno-auto-download", "Auto-download when playing", "no");
        
        Cli::printSection("Authentication");
        std::cout << "  Suno uses cookie-based authentication via QWebEngineView.\n"
                  << "  Use the Settings dialog or edit config.toml to set credentials.\n";
        break;
        
    case Karaoke:
        Cli::printHeader("ChadVis Help - Karaoke/Lyrics System");
        Cli::printSection("CLI Options");
        Cli::printOption("--karaoke-enabled", "Enable karaoke lyrics display", "yes");
        Cli::printOption("--karaoke-font <name>", "Font family", "Arial");
        Cli::printOption("--karaoke-font-size <n>", "Font size in pixels", "32");
        Cli::printOption("--karaoke-y-position <0-1>", "Vertical position (0=top, 1=bottom)", "0.5");
        Cli::printOption("--test-lyrics <path>", "Test with local SRT/LRC file");
        
        Cli::printSection("Features");
        std::cout << "  • Word-level time-synced lyrics highlighting\n"
                  << "  • 60fps smooth animations with glow effects\n"
                  << "  • Click-to-seek on lyrics panel\n"
                  << "  • Search within lyrics\n"
                  << "  • SRT/LRC subtitle export\n";
        break;
        
    case Examples:
        Cli::printHeader("ChadVis Help - Usage Examples");
        Cli::printSection("Basic Playback");
        std::cout << "  " << CliColor::brightCyan() << "chadvis-projectm-qt ~/Music/*.flac" << CliColor::reset() << "\n"
                  << "  " << CliColor::dim() << "# Play all FLAC files in Music directory" << CliColor::reset() << "\n\n"
                  << "  " << CliColor::brightCyan() << "chadvis-projectm-qt playlist.m3u" << CliColor::reset() << "\n"
                  << "  " << CliColor::dim() << "# Load M3U playlist" << CliColor::reset() << "\n";
        
        Cli::printSection("Recording");
        std::cout << "  " << CliColor::brightCyan() << "chadvis-projectm-qt -r -o video.mp4 song.mp3" << CliColor::reset() << "\n"
                  << "  " << CliColor::dim() << "# Record song to video.mp4" << CliColor::reset() << "\n\n"
                  << "  " << CliColor::brightCyan() << "chadvis-projectm-qt --recording-codec h264_nvenc -r" << CliColor::reset() << "\n"
                  << "  " << CliColor::dim() << "# Use NVIDIA hardware encoding" << CliColor::reset() << "\n";
        
        Cli::printSection("Visualizer");
        std::cout << "  " << CliColor::brightCyan() << "chadvis-projectm-qt -p \"Aderrasi - Airhandler\"" << CliColor::reset() << "\n"
                  << "  " << CliColor::dim() << "# Start with specific preset" << CliColor::reset() << "\n\n"
                  << "  " << CliColor::brightCyan() << "chadvis-projectm-qt --default-preset" << CliColor::reset() << "\n"
                  << "  " << CliColor::dim() << "# Use projectM's built-in default" << CliColor::reset() << "\n";
        
        Cli::printSection("Suno Integration");
        std::cout << "  " << CliColor::brightCyan() << "chadvis-projectm-qt --suno-id abc123-def456" << CliColor::reset() << "\n"
                  << "  " << CliColor::dim() << "# Fetch and play specific Suno song" << CliColor::reset() << "\n";
        break;
        
    case Config:
        Cli::printHeader("ChadVis Help - Configuration Files");
        Cli::printSection("Config Locations");
        std::cout << "  " << CliColor::brightYellow() << "User config:" << CliColor::reset() << " ~/.config/chadvis-projectm-qt/config.toml\n"
                  << "  " << CliColor::brightYellow() << "Data dir:" << CliColor::reset() << "    ~/.local/share/chadvis-projectm-qt/\n"
                  << "  " << CliColor::brightYellow() << "Cache dir:" << CliColor::reset() << "   ~/.cache/chadvis-projectm-qt/\n"
                  << "  " << CliColor::brightYellow() << "Logs:" << CliColor::reset() << "        ~/.cache/chadvis-projectm-qt/logs/\n";
        
        Cli::printSection("Creating Custom Config");
        std::cout << "  1. Copy default config:\n"
                  << "     " << CliColor::dim() << "cp /usr/share/chadvis-projectm-qt/config/default.toml ~/.config/chadvis-projectm-qt/config.toml" << CliColor::reset() << "\n\n"
                  << "  2. Edit with your preferred settings\n\n"
                  << "  3. Launch with custom config:\n"
                  << "     " << CliColor::dim() << "chadvis-projectm-qt -c ~/.config/chadvis-projectm-qt/config.toml" << CliColor::reset() << "\n";
        break;
        
    case All:
        printHelp(General);
        printHelp(Audio);
        printHelp(Visualizer);
        printHelp(Recording);
        printHelp(Suno);
        printHelp(Karaoke);
        printHelp(Examples);
        printHelp(Config);
        break;
    }
    
    std::cout << "\n" << CliColor::dim() << "For more help: chadvis-projectm-qt --help <topic>" << CliColor::reset() << "\n"
              << CliColor::dim() << "Available topics: audio, visualizer, recording, suno, karaoke, examples, config" << CliColor::reset() << "\n\n";
}

std::optional<HelpSystem::Topic> HelpSystem::parseTopic(std::string_view name) {
    using enum Topic;
    
    if (name == "general" || name == "usage") return General;
    if (name == "audio") return Audio;
    if (name == "visualizer" || name == "viz") return Visualizer;
    if (name == "recording" || name == "record") return Recording;
    if (name == "suno") return Suno;
    if (name == "karaoke" || name == "lyrics") return Karaoke;
    if (name == "examples" || name == "ex") return Examples;
    if (name == "config" || name == "configuration") return Config;
    if (name == "all") return All;
    
    return std::nullopt;
}

std::string_view HelpSystem::topicName(Topic topic) {
    using enum Topic;
    switch (topic) {
    case General: return "general";
    case Audio: return "audio";
    case Visualizer: return "visualizer";
    case Recording: return "recording";
    case Suno: return "suno";
    case Karaoke: return "karaoke";
    case Examples: return "examples";
    case Config: return "config";
    case All: return "all";
    }
    return "unknown";
}

void HelpSystem::listTopics() {
    Cli::printSection("Available Help Topics");
    std::cout << "  Use: chadvis-projectm-qt --help <topic>\n\n"
              << "  " << CliColor::brightGreen() << "general" << CliColor::reset() << "    Basic usage and common options\n"
              << "  " << CliColor::brightGreen() << "audio" << CliColor::reset() << "      Audio device and buffer settings\n"
              << "  " << CliColor::brightGreen() << "visualizer" << CliColor::reset() << " Presets, FPS, and rendering\n"
              << "  " << CliColor::brightGreen() << "recording" << CliColor::reset() << "  Video encoding and hardware accel\n"
              << "  " << CliColor::brightGreen() << "suno" << CliColor::reset() << "       Suno AI integration\n"
              << "  " << CliColor::brightGreen() << "karaoke" << CliColor::reset() << "    Lyrics and subtitle display\n"
              << "  " << CliColor::brightGreen() << "examples" << CliColor::reset() << "   Usage examples\n"
              << "  " << CliColor::brightGreen() << "config" << CliColor::reset() << "     Configuration files and paths\n"
              << "  " << CliColor::brightGreen() << "all" << CliColor::reset() << "        Show all topics\n";
}

} // namespace vc
