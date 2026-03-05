
#include "Application.hpp"
#include "CliUtils.hpp"
#include "Config.hpp"
#include "Logger.hpp"
#include "audio/AudioEngine.hpp"
#include "audio/AudioBridge.hpp"
#include "overlay/OverlayEngine.hpp"
#include "recorder/VideoRecorder.hpp"
#include "ui/MainWindow.hpp"
#include "util/FileUtils.hpp"
#include "util/GLIncludes.hpp"
#include "visualizer/RatingManager.hpp"
#include "ui/controllers/SunoController.hpp"
#include "suno/SunoModels.hpp"

#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QStyleFactory>
#include <iostream>
#include <cstdlib>

namespace vc {

Application* Application::instance_ = nullptr;

Application::Application(int& argc, char** argv) : argc_(argc), argv_(argv) {
    instance_ = this;
}

Application::~Application() {
    // Cleanup order matters
    mainWindow_.reset();
    videoRecorder_.reset();
    overlayEngine_.reset();
    audioEngine_.reset();
    qapp_.reset();

    Logger::shutdown();
    instance_ = nullptr;
}

Result<AppOptions> Application::parseArgs() {
    AppOptions opts;

    for (int i = 1; i < argc_; ++i) {
        std::string_view arg(argv_[i]);

        // Help system with topics
        if (arg == "-h" || arg == "--help") {
            // Check for topic help: --help <topic>
            if (i + 1 < argc_) {
                std::string_view nextArg(argv_[i + 1]);
                if (nextArg[0] != '-') {
                    auto topic = HelpSystem::parseTopic(std::string(nextArg));
                    if (topic) {
                        HelpSystem::printHelp(*topic);
                        std::exit(0);
                    }
                }
            }
            printHelp();
            std::exit(0);
        } else if (arg == "-v" || arg == "--version") {
            printVersion();
            std::exit(0);
        } else if (arg == "--help-topics") {
            HelpSystem::listTopics();
            std::exit(0);
        } else if (arg == "--generate-completion") {
            if (i + 1 >= argc_) {
                Cli::printError("--generate-completion requires a shell argument (bash/zsh/fish)");
                std::exit(1);
            }
            Cli::generateCompletionScript(argv_[++i]);
            std::exit(0);
        } else if (arg == "-d" || arg == "--debug") {
            opts.debug = true;
        } else if (arg == "--headless") {
            opts.headless = true;
        }
        // Recording options
        else if (arg == "-r" || arg == "--record") {
            opts.startRecording = true;
        } else if (arg == "-o" || arg == "--output") {
            if (i + 1 >= argc_) {
                Cli::printError("--output requires a path argument");
                return Result<AppOptions>::err("Missing required argument for --output");
            }
            opts.outputFile = fs::path(argv_[++i]);
        } else if (arg == "--recording-codec") {
            if (i + 1 >= argc_) {
                Cli::printError("--recording-codec requires a codec name");
                return Result<AppOptions>::err("Missing required argument for --recording-codec");
            }
            opts.recordingCodec = argv_[++i];
        } else if (arg == "--recording-crf") {
            if (i + 1 >= argc_) {
                Cli::printError("--recording-crf requires a quality value (0-51)");
                return Result<AppOptions>::err("Missing required argument for --recording-crf");
            }
            try {
                opts.recordingCrf = std::stoi(argv_[++i]);
            } catch (...) {
                Cli::printError("Invalid CRF value. Use a number between 0-51.");
                return Result<AppOptions>::err("Invalid CRF value");
            }
        } else if (arg == "--recording-preset") {
            if (i + 1 >= argc_) {
                Cli::printError("--recording-preset requires a preset name");
                return Result<AppOptions>::err("Missing required argument for --recording-preset");
            }
            opts.recordingPreset = argv_[++i];
        }
        // Config
        else if (arg == "-c" || arg == "--config") {
            if (i + 1 >= argc_) {
                Cli::printError("--config requires a path argument");
                return Result<AppOptions>::err("Missing required argument for --config");
            }
            opts.configFile = fs::path(argv_[++i]);
        }
        // Visualizer options
        else if (arg == "-p" || arg == "--preset") {
            if (i + 1 >= argc_) {
                Cli::printError("--preset requires a name argument");
                return Result<AppOptions>::err("Missing required argument for --preset");
            }
            opts.presetName = argv_[++i];
        } else if (arg == "--default-preset") {
            opts.useDefaultPreset = true;
        } else if (arg == "--visualizer-fps") {
            if (i + 1 >= argc_) {
                Cli::printError("--visualizer-fps requires a frame rate");
                return Result<AppOptions>::err("Missing required argument for --visualizer-fps");
            }
            try {
                opts.visualizerFps = std::stoi(argv_[++i]);
            } catch (...) {
                Cli::printError("Invalid FPS value. Use a number like 30, 60, 144.");
                return Result<AppOptions>::err("Invalid FPS value");
            }
        } else if (arg == "--visualizer-width") {
            if (i + 1 >= argc_) {
                Cli::printError("--visualizer-width requires a width in pixels");
                return Result<AppOptions>::err("Missing required argument for --visualizer-width");
            }
            try {
                opts.visualizerWidth = std::stoi(argv_[++i]);
            } catch (...) {
                Cli::printError("Invalid width value.");
                return Result<AppOptions>::err("Invalid width value");
            }
        } else if (arg == "--visualizer-height") {
            if (i + 1 >= argc_) {
                Cli::printError("--visualizer-height requires a height in pixels");
                return Result<AppOptions>::err("Missing required argument for --visualizer-height");
            }
            try {
                opts.visualizerHeight = std::stoi(argv_[++i]);
            } catch (...) {
                Cli::printError("Invalid height value.");
                return Result<AppOptions>::err("Invalid height value");
            }
        } else if (arg == "--visualizer-shuffle") {
            opts.visualizerShuffle = true;
        } else if (arg == "--no-visualizer-shuffle") {
            opts.visualizerShuffle = false;
        }
        // Audio options
        else if (arg == "--audio-device") {
            if (i + 1 >= argc_) {
                Cli::printError("--audio-device requires a device name");
                return Result<AppOptions>::err("Missing required argument for --audio-device");
            }
            opts.audioDevice = argv_[++i];
        } else if (arg == "--audio-buffer") {
            if (i + 1 >= argc_) {
                Cli::printError("--audio-buffer requires a buffer size");
                return Result<AppOptions>::err("Missing required argument for --audio-buffer");
            }
            try {
                opts.audioBufferSize = std::stoi(argv_[++i]);
            } catch (...) {
                Cli::printError("Invalid buffer size. Use 256, 512, 1024, 2048, etc.");
                return Result<AppOptions>::err("Invalid buffer size");
            }
        } else if (arg == "--audio-rate") {
            if (i + 1 >= argc_) {
                Cli::printError("--audio-rate requires a sample rate");
                return Result<AppOptions>::err("Missing required argument for --audio-rate");
            }
            try {
                opts.audioSampleRate = std::stoi(argv_[++i]);
            } catch (...) {
                Cli::printError("Invalid sample rate. Use 44100, 48000, etc.");
                return Result<AppOptions>::err("Invalid sample rate");
            }
        }
        // Suno options
        else if (arg == "--suno-id") {
            if (i + 1 >= argc_) {
                Cli::printError("--suno-id requires a UUID argument");
                return Result<AppOptions>::err("Missing required argument for --suno-id");
            }
            opts.sunoId = argv_[++i];
        } else if (arg == "--suno-download-path") {
            if (i + 1 >= argc_) {
                Cli::printError("--suno-download-path requires a path argument");
                return Result<AppOptions>::err("Missing required argument for --suno-download-path");
            }
            opts.sunoDownloadPath = fs::path(argv_[++i]);
        } else if (arg == "--suno-auto-download") {
            opts.sunoAutoDownload = true;
        } else if (arg == "--no-suno-auto-download") {
            opts.sunoAutoDownload = false;
        }
        // Karaoke options
        else if (arg == "--test-lyrics") {
            if (i + 1 >= argc_) {
                Cli::printError("--test-lyrics requires a path argument");
                return Result<AppOptions>::err("Missing required argument for --test-lyrics");
            }
            opts.testLyricsFile = fs::path(argv_[++i]);
        } else if (arg == "--karaoke-enabled") {
            opts.karaokeEnabled = true;
        } else if (arg == "--no-karaoke") {
            opts.karaokeEnabled = false;
        } else if (arg == "--karaoke-font") {
            if (i + 1 >= argc_) {
                Cli::printError("--karaoke-font requires a font family name");
                return Result<AppOptions>::err("Missing required argument for --karaoke-font");
            }
            opts.karaokeFont = argv_[++i];
        } else if (arg == "--karaoke-font-size") {
            if (i + 1 >= argc_) {
                Cli::printError("--karaoke-font-size requires a size in pixels");
                return Result<AppOptions>::err("Missing required argument for --karaoke-font-size");
            }
            try {
                opts.karaokeFontSize = std::stoi(argv_[++i]);
            } catch (...) {
                Cli::printError("Invalid font size. Use a number like 16, 24, 32.");
                return Result<AppOptions>::err("Invalid font size");
            }
        } else if (arg == "--karaoke-y-position") {
            if (i + 1 >= argc_) {
                Cli::printError("--karaoke-y-position requires a value 0.0-1.0");
                return Result<AppOptions>::err("Missing required argument for --karaoke-y-position");
            }
            try {
                opts.karaokeYPosition = std::stof(argv_[++i]);
            } catch (...) {
                Cli::printError("Invalid position. Use a number between 0.0 and 1.0.");
                return Result<AppOptions>::err("Invalid Y position");
            }
        }
        // UI options
        else if (arg == "--theme") {
            if (i + 1 >= argc_) {
                Cli::printError("--theme requires a theme name");
                return Result<AppOptions>::err("Missing required argument for --theme");
            }
            opts.theme = argv_[++i];
        }
        // Positional argument - input file
        else if (arg[0] != '-') {
            opts.inputFiles.push_back(fs::path(arg));
        }
        // Unknown option with suggestions
        else {
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

Result<void> Application::init(const AppOptions& opts) {
    // Initialize logging with final debug state
    bool debug = opts.debug || CONFIG.debug();
    Logger::init("chadvis-projectm-qt", debug);
    LOG_INFO("chadvis-projectm-qt starting up. I use Arch btw.");

    // Load configuration
    if (opts.configFile) {
        if (auto result = CONFIG.load(*opts.configFile); !result) {
            LOG_ERROR("Failed to load config: {}", result.error().message);
            return result;
        }
    } else {
        if (auto result = CONFIG.loadDefault(); !result) {
            LOG_WARN("Failed to load default config: {}",
                     result.error().message);
            // Continue with defaults
        }
    }

    // Ensure logger respects final debug state if it changed after loading config
    if (!opts.debug && CONFIG.debug()) {
        Logger::init("chadvis-projectm-qt", true);
    }

    // Override default preset from command line
    if (opts.useDefaultPreset) {
        CONFIG.visualizer().useDefaultPreset = true;
    } else {
        CONFIG.visualizer().useDefaultPreset = false;
    }

    // Set debug lyrics from command line
    if (opts.testLyricsFile) {
        CONFIG.suno().debugLyrics = true;
        CONFIG.suno().debugLyricsFile = *opts.testLyricsFile;
        LOG_INFO("Debug lyrics enabled: {}", opts.testLyricsFile->string());
    }
    
    // Apply CLI overrides to config (Settings System Unification)
    // Audio overrides
    if (opts.audioDevice) {
        CONFIG.audio().device = *opts.audioDevice;
        LOG_INFO("CLI override: audio.device = {}", *opts.audioDevice);
    }
    if (opts.audioBufferSize) {
        CONFIG.audio().bufferSize = *opts.audioBufferSize;
        LOG_INFO("CLI override: audio.bufferSize = {}", *opts.audioBufferSize);
    }
    if (opts.audioSampleRate) {
        CONFIG.audio().sampleRate = *opts.audioSampleRate;
        LOG_INFO("CLI override: audio.sampleRate = {}", *opts.audioSampleRate);
    }
    
    // Visualizer overrides
    if (opts.visualizerFps) {
        CONFIG.visualizer().fps = *opts.visualizerFps;
        LOG_INFO("CLI override: visualizer.fps = {}", *opts.visualizerFps);
    }
    if (opts.visualizerWidth) {
        CONFIG.visualizer().width = *opts.visualizerWidth;
        LOG_INFO("CLI override: visualizer.width = {}", *opts.visualizerWidth);
    }
    if (opts.visualizerHeight) {
        CONFIG.visualizer().height = *opts.visualizerHeight;
        LOG_INFO("CLI override: visualizer.height = {}", *opts.visualizerHeight);
    }
    if (opts.visualizerShuffle) {
        CONFIG.visualizer().shufflePresets = *opts.visualizerShuffle;
        LOG_INFO("CLI override: visualizer.shufflePresets = {}", *opts.visualizerShuffle);
    }
    
    // Recording overrides
    if (opts.recordingCodec) {
        CONFIG.recording().video.codec = *opts.recordingCodec;
        LOG_INFO("CLI override: recording.video.codec = {}", *opts.recordingCodec);
    }
    if (opts.recordingCrf) {
        CONFIG.recording().video.crf = *opts.recordingCrf;
        LOG_INFO("CLI override: recording.video.crf = {}", *opts.recordingCrf);
    }
    if (opts.recordingPreset) {
        CONFIG.recording().video.preset = *opts.recordingPreset;
        LOG_INFO("CLI override: recording.video.preset = {}", *opts.recordingPreset);
    }
    
    // Suno overrides
    if (opts.sunoDownloadPath) {
        CONFIG.suno().downloadPath = *opts.sunoDownloadPath;
        LOG_INFO("CLI override: suno.downloadPath = {}", opts.sunoDownloadPath->string());
    }
    if (opts.sunoAutoDownload) {
        CONFIG.suno().autoDownload = *opts.sunoAutoDownload;
        LOG_INFO("CLI override: suno.autoDownload = {}", *opts.sunoAutoDownload);
    }
    
    // Karaoke overrides
    if (opts.karaokeEnabled) {
        CONFIG.karaoke().enabled = *opts.karaokeEnabled;
        LOG_INFO("CLI override: karaoke.enabled = {}", *opts.karaokeEnabled);
    }
    if (opts.karaokeFont) {
        CONFIG.karaoke().fontFamily = *opts.karaokeFont;
        LOG_INFO("CLI override: karaoke.fontFamily = {}", *opts.karaokeFont);
    }
    if (opts.karaokeFontSize) {
        CONFIG.karaoke().fontSize = *opts.karaokeFontSize;
        LOG_INFO("CLI override: karaoke.fontSize = {}", *opts.karaokeFontSize);
    }
    if (opts.karaokeYPosition) {
        CONFIG.karaoke().yPosition = *opts.karaokeYPosition;
        LOG_INFO("CLI override: karaoke.yPosition = {}", *opts.karaokeYPosition);
    }
    
    // UI overrides
    if (opts.theme) {
        CONFIG.ui().theme = *opts.theme;
        LOG_INFO("CLI override: ui.theme = {}", *opts.theme);
    }

    // Create Qt application
    qapp_ = std::make_unique<QApplication>(argc_, argv_);
    qapp_->setApplicationName("ChadVis");
    qapp_->setApplicationVersion("1.0.0");
    qapp_->setOrganizationName("ChadVis");
    qapp_->setOrganizationDomain("github.com/chadvis-projectm-qt");

    // Setup styling
    setupStyle();

    // Initialize components
    LOG_DEBUG("Initializing audio engine...");
    audioEngine_ = std::make_unique<AudioEngine>();
    if (auto result = audioEngine_->init(); !result) {
        LOG_ERROR("Audio engine init failed: {}", result.error().message);
        return result;
    }

    LOG_DEBUG("Initializing audio bridge...");
    audioBridge_ = std::make_unique<audio::AudioBridge>();

    LOG_DEBUG("Initializing overlay engine...");
    overlayEngine_ = std::make_unique<OverlayEngine>();
    overlayEngine_->init();

    LOG_DEBUG("Initializing video recorder...");
    videoRecorder_ = std::make_unique<VideoRecorder>();

    LOG_DEBUG("Initializing rating manager...");
    if (auto result = RatingManager::instance().load(); !result) {
        LOG_WARN("Failed to load preset ratings: {}", result.error().message);
    }

    // Create main window (unless headless)
    if (!opts.headless) {
        LOG_DEBUG("Creating main window...");
        mainWindow_ = std::make_unique<MainWindow>();
        mainWindow_->show();

        if (!opts.inputFiles.empty()) {
            mainWindow_->audioEngine()->playlist().clear();
            
            for (const auto& file : opts.inputFiles) {
                if (fs::exists(file)) {
                    mainWindow_->addToPlaylist(file);
                } else {
                    LOG_WARN("File not found: {}", file.string());
                }
            }
        }

        // Auto-play if files were added
        if (!opts.inputFiles.empty()) {
            LOG_INFO("Auto-playing first track");
            mainWindow_->audioEngine()->play();
        }

        // Auto-start recording if requested
        if (opts.startRecording) {
            if (opts.outputFile) {
                mainWindow_->startRecording(*opts.outputFile);
            } else {
                mainWindow_->startRecording();
            }
        }

        // Set preset if specified
        if (opts.presetName) {
            LOG_INFO("Application: Requesting preset '{}'", *opts.presetName);
            mainWindow_->selectPreset(*opts.presetName);
        } else if (opts.useDefaultPreset) {
            // Don't select any preset - use projectM default
            LOG_INFO("Using default projectM visualizer (no preset selected)");
        }
    }

    // Connect quit signal
    connect(qapp_.get(),
            &QApplication::aboutToQuit,
            this,
            &Application::aboutToQuit);

    LOG_INFO("Initialization complete. Let's get this bread.");

    if (opts.sunoId && !opts.headless && mainWindow_) {
        LOG_INFO("Application: Fetching song ID {}", *opts.sunoId);
        auto* controller = mainWindow_->sunoController();
        if (controller) {
            suno::SunoClip clip;
            clip.id = *opts.sunoId;
            clip.is_public = true;
            controller->downloadAndPlay(clip);
        } else {
            LOG_ERROR("Application: Suno controller not available for --suno-id");
        }
    }

    return Result<void>::ok();
}

int Application::exec() {
    if (!qapp_) {
        LOG_ERROR("Application not initialized");
        return 1;
    }
    return qapp_->exec();
}

void Application::quit() {
    LOG_INFO("Shutting down...");

    // Stop recording if active
    if (videoRecorder_ && videoRecorder_->isRecording()) {
        videoRecorder_->stop();
    }

    // Stop audio
    if (audioEngine_) {
        // Save last session playlist
        auto lastSession = file::configDir() / "last_session.m3u";
        audioEngine_->playlist().saveM3U(lastSession);
        LOG_DEBUG("Saved session playlist to {}", lastSession.string());

        audioEngine_->stop();
    }

    // Save config if dirty
    if (CONFIG.isDirty()) {
        CONFIG.save(CONFIG.configPath());
    }

    if (qapp_) {
        qapp_->quit();
    }
    
    // Hard exit to ensure all threads (FFmpeg, etc) are terminated if they hang
    std::exit(0);
}

void Application::reloadTheme() {
    setupStyle();
}

void Application::setupStyle() {
    // Use Fusion style as base (looks good on Linux)
    qapp_->setStyle(QStyleFactory::create("Fusion"));

    // Load fonts
    QFontDatabase::addApplicationFont(":/fonts/liberation-sans.ttf");

    // Load stylesheet based on theme
    QString themeName = QString::fromStdString(CONFIG.ui().theme);
    QString stylePath = QString(":/styles/%1.qss").arg(themeName);

    QFile styleFile(stylePath);
    if (styleFile.open(QFile::ReadOnly | QFile::Text)) {
        QString style = styleFile.readAll();
        qapp_->setStyleSheet(style);
        LOG_DEBUG("Loaded theme: {}", themeName.toStdString());
    } else {
        if (themeName != "dark") {
            LOG_WARN("Theme not found: {}, using default dark theme", themeName.toStdString());
        } else {
             LOG_DEBUG("Dark theme requested, using built-in fallback");
        }
        // Fallback dark theme inline
        qapp_->setStyleSheet(R"(
            QMainWindow, QDialog, QWidget {
                background-color: #1e1e1e;
                color: #ffffff;
            }
            QPushButton {
                background-color: #3c3c3c;
                border: 1px solid #555555;
                border-radius: 4px;
                padding: 6px 12px;
                color: #ffffff;
            }
            QPushButton:hover {
                background-color: #4a4a4a;
            }
            QPushButton:pressed {
                background-color: #2a2a2a;
            }
            QListWidget, QTreeWidget, QTableWidget {
                background-color: #252525;
                border: 1px solid #3c3c3c;
                color: #ffffff;
            }
            QSlider::groove:horizontal {
                height: 4px;
                background: #3c3c3c;
            }
            QSlider::handle:horizontal {
                background: #00ff88;
                width: 12px;
                margin: -4px 0;
                border-radius: 6px;
            }
        )");
    }
}

void Application::printVersion() {
    using namespace CliColor;
    std::cout << "\n" << brightCyan() << bold()
              << "╔═══════════════════════════════════════════╗\n"
              << "║         ChadVis Audio Player              ║\n"
              << "╚═══════════════════════════════════════════╝" << reset() << "\n"
              << "  Version: " << brightGreen() << "1.0.0" << reset() << "\n"
              << "  Built with Qt: " << brightGreen() << qVersion() << reset() << "\n"
              << "  " << dim() << "\"I use Arch btw\"" << reset() << "\n\n";
}

void Application::printHelp() {
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
