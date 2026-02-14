/**
 * @file HelpPrinter.cpp
 * @brief Help and version text printing implementation.
 */

#include "HelpPrinter.hpp"
#include "CliUtils.hpp"
#include <iostream>
#include <QtGlobal>

namespace vc {

void HelpPrinter::printVersion() {
    using namespace CliColor;
    std::cout << "\n" << brightCyan() << bold()
              << "╔═══════════════════════════════════════════╗\n"
              << "║         ChadVis Audio Player              ║\n"
              << "╚═══════════════════════════════════════════╝" << reset() << "\n"
              << "  Version: " << brightGreen() << "1.1.0-alpha" << reset() << "\n"
              << "  Built with Qt: " << brightGreen() << qVersion() << reset() << "\n"
              << "  " << dim() << "\"I use Arch btw\"" << reset() << "\n\n";
}

void HelpPrinter::printHelp() {
    using namespace CliColor;

    std::cout << "\n" << brightCyan() << bold()
              << "╔════════════════════════════════════════════════════════════╗\n"
              << "║     ChadVis - Chad-tier Audio Visualizer for Arch Linux    ║\n"
              << "╚════════════════════════════════════════════════════════════╝" << reset() << "\n\n";

    Cli::printSection("Usage");
    std::cout << "  chadvis-projectm-qt [options] [files...]\n"
              << "  " << dim() << "chadvis-projectm-qt --help <topic>    # Detailed topic help" << reset() << "\n"
              << "  " << dim() << "chadvis-projectm-qt --help-topics     # List help topics" << reset() << "\n\n";

    Cli::printSection("General Options");
    Cli::printOption("-h, --help [topic]", "Show this help or detailed topic help");
    Cli::printOption("--help-topics", "List available help topics");
    Cli::printOption("-v, --version", "Show version information");
    Cli::printOption("-d, --debug", "Enable debug logging", "no");
    Cli::printOption("-c, --config <path>", "Use custom config file");
    Cli::printOption("--headless", "Run without GUI (batch mode)");
    Cli::printOption("--generate-completion <shell>", "Generate shell completion script");

    Cli::printSection("Audio Options");
    Cli::printOption("--audio-device <name>", "Audio output device", "default");
    Cli::printOption("--audio-buffer <size>", "Buffer size in samples", "2048");
    Cli::printOption("--audio-rate <rate>", "Sample rate in Hz", "44100");

    Cli::printSection("Visualizer Options");
    Cli::printOption("-p, --preset <name>", "Start with specific preset");
    Cli::printOption("--default-preset", "Use projectM's default (no preset)");
    Cli::printOption("--visualizer-fps <n>", "Target frame rate", "60");
    Cli::printOption("--visualizer-width <n>", "Render width", "1920");
    Cli::printOption("--visualizer-height <n>", "Render height", "1080");
    Cli::printOption("--visualizer-shuffle", "Shuffle presets");
    Cli::printOption("--no-visualizer-shuffle", "Don't shuffle presets");

    Cli::printSection("Recording Options");
    Cli::printOption("-r, --record", "Start recording immediately");
    Cli::printOption("-o, --output <path>", "Recording output file");
    Cli::printOption("--recording-codec <codec>", "Video codec (libx264/nvenc/vaapi)");
    Cli::printOption("--recording-crf <n>", "Quality 0-51 (lower=better)", "18");
    Cli::printOption("--recording-preset <name>", "Encoding speed", "medium");

    Cli::printSection("Suno Options");
    Cli::printOption("--suno-id <uuid>", "Fetch and play Suno song by ID");
    Cli::printOption("--suno-download-path <path>", "Download directory");
    Cli::printOption("--suno-auto-download", "Auto-download when playing");
    Cli::printOption("--no-suno-auto-download", "Don't auto-download");

    Cli::printSection("Karaoke Options");
    Cli::printOption("--karaoke-enabled", "Enable karaoke display");
    Cli::printOption("--no-karaoke", "Disable karaoke display");
    Cli::printOption("--karaoke-font <name>", "Font family", "Arial");
    Cli::printOption("--karaoke-font-size <n>", "Font size in pixels", "32");
    Cli::printOption("--karaoke-y-position <0-1>", "Vertical position", "0.5");
    Cli::printOption("--test-lyrics <path>", "Test with SRT/LRC file");

    Cli::printSection("UI Options");
    Cli::printOption("--theme <name>", "UI theme (dark/gruvbox/nord)", "dark");

    Cli::printSection("Examples");
    std::cout << "  " << brightCyan() << "chadvis-projectm-qt ~/Music/*.flac" << reset() << "\n"
              << "  " << dim() << "# Play all FLAC files" << reset() << "\n\n"
              << "  " << brightCyan() << "chadvis-projectm-qt -r -o video.mp4 song.mp3" << reset() << "\n"
              << "  " << dim() << "# Record to video.mp4" << reset() << "\n\n"
              << "  " << brightCyan() << "chadvis-projectm-qt --recording-codec h264_nvenc -r" << reset() << "\n"
              << "  " << dim() << "# Use NVIDIA hardware encoding" << reset() << "\n\n"
              << "  " << brightCyan() << "chadvis-projectm-qt --help recording" << reset() << "\n"
              << "  " << dim() << "# Detailed recording help" << reset() << "\n\n";

    Cli::printSection("Keyboard Shortcuts");
    std::cout << "  " << brightYellow() << "Space" << reset() << "  Play/Pause    "
              << "  " << brightYellow() << "R" << reset() << "  Toggle recording\n"
              << "  " << brightYellow() << "N/P" << reset() << "    Next/Prev     "
              << "  " << brightYellow() << "F" << reset() << "  Fullscreen\n"
              << "  " << brightYellow() << "← →" << reset() << "    Prev/Next preset\n\n";

    std::cout << "Config: " << brightYellow() << "~/.config/chadvis-projectm-qt/config.toml" << reset() << "\n"
              << "Logs:   " << brightYellow() << "~/.cache/chadvis-projectm-qt/logs/" << reset() << "\n\n"
              << "Docs:   " << brightBlue() << "https://github.com/yourusername/chadvis-projectm-qt" << reset() << "\n"
              << dim() << "Or don't. We're not your mom." << reset() << "\n\n";
}

} // namespace vc
