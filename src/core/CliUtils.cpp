/**
 * @file CliUtils.cpp
 * @brief Implementation of CLI utilities with color support.
 */

#include "CliUtils.hpp"
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <sstream>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif
#include <iomanip>
#include <QtGlobal>

namespace vc {

namespace {

// Portable stdout-TTY probe: _isatty on Windows, isatty elsewhere.
bool stdoutIsTerminal() {
#ifdef _WIN32
    return _isatty(_fileno(stdout)) != 0;
#else
    return ::isatty(STDOUT_FILENO) != 0;
#endif
}

} // namespace

bool CliColor::shouldUseColor() {
    // Respect NO_COLOR environment variable
    if (const char* noColor = std::getenv("NO_COLOR")) {
        if (noColor[0] != '\0') {
            return false;
        }
    }

    // Check if stdout is a TTY
    if (!stdoutIsTerminal()) {
        return false;
    }

    return true;
}

namespace Cli {

// Injected by the build from version.txt; the fallback only matters for
// IDE/foreign-TU builds that do not carry the compile definition.
#ifndef CHADVIS_VERSION
#define CHADVIS_VERSION "unknown"
#endif

std::string_view version() {
    return CHADVIS_VERSION;
}

std::string versionBanner() {
    using namespace CliColor;
    std::string banner;
    banner += '\n';
    banner += brightCyan();
    banner += bold();
    banner += "╔═══════════════════════════════════════════╗\n";
    banner += "║ ChadVis Audio Player ║\n";
    banner += "╚═══════════════════════════════════════════╝";
    banner += reset();
    banner += "\n Version: ";
    banner += brightGreen();
    banner.append(version());
    banner += reset();
    banner += "\n Built with Qt: ";
    banner += brightGreen();
    banner += qVersion();
    banner += reset();
    banner += "\n ";
    banner += dim();
    banner += "\"I use Arch btw\"";
    banner += reset();
    banner += "\n\n";
    return banner;
}

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

namespace {
// T0211: Damerau-Levenshtein (optimal string alignment): adjacent
// transpositions cost 1, not 2. The old Hamming+length-diff scored
// `--recrod` vs `--record` as 2; a typo'd flag should suggest at distance 1.
size_t editDistance(std::string_view a, std::string_view b) {
    const size_t n = a.size(), m = b.size();
    if (n == 0) return m;
    if (m == 0) return n;
    // Two rows suffice for OSA.
    std::vector<size_t> prev(m + 1), cur(m + 1);
    for (size_t j = 0; j <= m; ++j) prev[j] = j;
    for (size_t i = 1; i <= n; ++i) {
        cur[0] = i;
        for (size_t j = 1; j <= m; ++j) {
            const size_t cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
            size_t best = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost});
            if (i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1]) {
                best = std::min(best, prev[j - 2] + 1); // transposition
            }
            cur[j] = best;
        }
        prev.swap(cur);
    }
    return prev[m];
}
} // namespace

std::optional<std::string> findClosestMatch(
	std::string_view input,
	std::initializer_list<std::string_view> candidates) {

	std::optional<std::string> bestMatch;
	size_t bestDistance = std::numeric_limits<size_t>::max();

	for (const auto& candidate : candidates) {
		const size_t distance = editDistance(input, candidate);

		// Only suggest if reasonably close (max 3 edits for typical flags)
		if (distance < bestDistance && distance <= 3) {
			bestDistance = distance;
			bestMatch = std::string(candidate);
		}
	}

	return bestMatch;
}

std::optional<std::string> findClosestMatch(
	std::string_view input,
	std::span<const std::string_view> candidates) {

	std::optional<std::string> bestMatch;
	size_t bestDistance = std::numeric_limits<size_t>::max();

	for (const auto& candidate : candidates) {
		const size_t distance = editDistance(input, candidate);

		// Only suggest if reasonably close (max 3 edits for typical flags)
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

// ─── T0207: CliArgs.inc as data ─────────────────────────────────────────────
// The table is the single source of truth for --help, --help <topic>, and
// shell completion. Every consumer below expands it; no hand-written flag
// list to drift when a flag is added or removed.

namespace {

std::string lowerFirst(std::string_view s) {
    std::string out(s);
    if (!out.empty()) {
        out[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(out[0])));
    }
    return out;
}

} // namespace

const std::vector<CliArg>& Cli::cliArgTable() {
    static const std::vector<CliArg> table = [] {
        std::vector<CliArg> out;
        std::string_view section;
#define CLI_SECTION(name) section = name;
#define CLI_BOOL(l, s, h, hd, n, f, c) out.push_back({l, s, CliArgType::Bool, h, hd, n, section});
#define CLI_INT(l, s, h, hd, n, f, c) out.push_back({l, s, CliArgType::Int, h, hd, n, section});
#define CLI_FLOAT(l, s, h, hd, n, f, c) out.push_back({l, s, CliArgType::Float, h, hd, n, section});
#define CLI_STRING(l, s, h, hd, n, f, c) out.push_back({l, s, CliArgType::String, h, hd, n, section});
#define CLI_PATH(l, s, h, hd, n, f, c) out.push_back({l, s, CliArgType::Path, h, hd, n, section});
#include "CliArgs.inc"
#undef CLI_SECTION
#undef CLI_BOOL
#undef CLI_INT
#undef CLI_FLOAT
#undef CLI_STRING
#undef CLI_PATH
        return out;
    }();
    return table;
}

std::string Cli::formatFlags(const CliArg& arg) {
    std::string s;
    if (!arg.shortName.empty()) {
        s += arg.shortName;
        s += ", ";
    }
    s += arg.longName;
    switch (arg.type) {
        case CliArgType::Int: s += " <n>"; break;
        case CliArgType::Float: s += " <n>"; break;
        case CliArgType::String: s += " <value>"; break;
        case CliArgType::Path: s += " <path>"; break;
        case CliArgType::Bool: break;
    }
    return s;
}

void Cli::printCliArg(const CliArg& arg) {
    printOption(formatFlags(arg), arg.helpText,
                arg.helpDefault.empty()
                    ? std::optional<std::string_view>{}
                    : std::optional<std::string_view>{arg.helpDefault});
    if (!arg.negationName.empty()) {
        // Derived from the flag it negates, so it can't drift.
        printOption(std::string(arg.negationName), "Disable " + lowerFirst(arg.helpText));
    }
}

void Cli::printOptionsForSection(std::string_view section) {
    for (const auto& arg : cliArgTable()) {
        if (arg.section == section) {
            printCliArg(arg);
        }
    }
}

void generateCompletionScript(std::string_view shell) {
    // T0207: every list below is expanded from CliArgs.inc via cliArgTable(),
    // so completion can never go stale when a flag is added. Meta flags
    // (--help/--version/--help-topics/--generate-completion) are handled as
    // special cases in parseArgsFrom, outside the table, and listed here.
    const auto& table = cliArgTable();

    std::string longOpts;
    std::string pathOpts; // flags taking a filesystem path (file completion)
    for (const auto& arg : table) {
        longOpts += arg.longName;
        longOpts += ' ';
        if (!arg.negationName.empty()) {
            longOpts += arg.negationName;
            longOpts += ' ';
        }
        if (arg.type == CliArgType::Path) {
            if (!pathOpts.empty()) pathOpts += '|';
            pathOpts += arg.longName;
        }
    }
    longOpts += "--help --version --help-topics --generate-completion";

    if (shell == "bash") {
        std::cout << R"(
# ChadVis Bash Completion
# Source this file: source /path/to/chadvis-completion.bash

_chadvis_complete() {
    local cur prev opts
    COMPREPLY=()
    cur="${COMP_WORDS[COMP_CWORD]}"
    prev="${COMP_WORDS[COMP_CWORD-1]}"
)" << "\n    opts=\"" << longOpts << "\"\n" << R"(
    case "${prev}" in
)" << "\n        " << pathOpts << ")\n" << R"(
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
)";
        std::cout << "    '(-h --help)'{-h,--help}'[Show help message]' \\\n";
        std::cout << "    '(-v --version)'{-v,--version}'[Show version information]' \\\n";
        for (const auto& arg : table) {
            std::string shorts;
            if (!arg.shortName.empty()) {
                shorts = "'(" + std::string(arg.shortName) + " " + std::string(arg.longName) + ")'" +
                         "{" + std::string(arg.shortName) + "," + std::string(arg.longName) + "}'";
            } else {
                shorts = "'"+ std::string(arg.longName) + "'";
            }
            std::string action;
            if (arg.type == CliArgType::Path) {
                action = ":path:_files";
            } else if (arg.type != CliArgType::Bool) {
                action = ":value:";
            }
            std::cout << "    " << shorts << "'[" << arg.helpText << "]'" << action << " \\\n";
            if (!arg.negationName.empty()) {
                std::cout << "    '" << arg.negationName << "[Disable "
                          << lowerFirst(arg.helpText) << "]' \\\n";
            }
        }
        std::cout << "    '--help-topics[List help topics]' \\\n";
        std::cout << "    '--generate-completion[Generate completion script]:shell:(bash zsh fish)'\n";
        std::cout << "    '*:audio file:_files'\n";
    } else if (shell == "fish") {
        std::cout << R"(
# ChadVis Fish Completion
# Place in ~/.config/fish/completions/chadvis-projectm-qt.fish

complete -c chadvis-projectm-qt -s h -l help -d "Show help message"
complete -c chadvis-projectm-qt -s v -l version -d "Show version information"
)";
        for (const auto& arg : table) {
            std::string line = "complete -c chadvis-projectm-qt";
            if (!arg.shortName.empty()) {
                line += " -s ";
                line += arg.shortName.substr(1); // "-d" -> "d"
            }
            // longName always starts with "--"
            line += " -l ";
            line += arg.longName.substr(2);
            line += " -d \"" + std::string(arg.helpText) + "\"";
            if (arg.type == CliArgType::Path) {
                line += " -r";
            }
            std::cout << line << "\n";
            if (!arg.negationName.empty()) {
                std::cout << "complete -c chadvis-projectm-qt -l "
                          << arg.negationName.substr(2) << " -d \"Disable "
                          << lowerFirst(arg.helpText) << "\"\n";
            }
        }
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
        // T0207: generated from CliArgs.inc.
        Cli::printOptionsForSection("General Options");
        Cli::printOptionsForSection("Config");
        
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
        // T0207: generated from CliArgs.inc.
        Cli::printOptionsForSection("Audio Options");
        
        Cli::printSection("Config File");
        std::cout << "  [audio]\n"
                  << "  device = \"default\"\n"
                  << "  buffer_size = 2048\n"
                  << "  sample_rate = 44100\n";
        break;
        
    case Visualizer:
        Cli::printHeader("ChadVis Help - Visualizer Configuration");
        Cli::printSection("CLI Options");
        // T0207: generated from CliArgs.inc.
        Cli::printOptionsForSection("Visualizer Options");
        
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
        // T0207: generated from CliArgs.inc.
        Cli::printOptionsForSection("Recording Options");
        
        Cli::printSection("Hardware Acceleration");
        std::cout << "  " << CliColor::brightGreen() << "NVENC" << CliColor::reset() << "  (NVIDIA)   - Use codec 'h264_nvenc' or 'hevc_nvenc'\n"
                  << "  " << CliColor::brightGreen() << "VAAPI" << CliColor::reset() << "  (AMD/Intel) - Use codec 'h264_vaapi' or 'hevc_vaapi'\n"
                  << "  " << CliColor::dim() << "Check available codecs: ffmpeg -encoders | grep -E '(nvenc|vaapi)'" << CliColor::reset() << "\n";
        break;
        
    case Suno:
        Cli::printHeader("ChadVis Help - Suno AI Integration");
        Cli::printSection("CLI Options");
        // T0207: generated from CliArgs.inc.
        Cli::printOptionsForSection("Suno Options");
        
        Cli::printSection("Authentication");
        std::cout << "  Suno uses browser-based authentication.\n"
                  << "  Use the Settings dialog or edit config.toml to set credentials.\n";
        break;
        
    case Karaoke:
        Cli::printHeader("ChadVis Help - Karaoke/Lyrics System");
        Cli::printSection("CLI Options");
        // T0207: generated from CliArgs.inc.
        Cli::printOptionsForSection("Karaoke Options");
        
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
