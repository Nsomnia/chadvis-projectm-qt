
#include "Application.hpp"
#include "Config.hpp"
#include "Logger.hpp"
#include "audio/AudioEngine.hpp"
#include "overlay/OverlayEngine.hpp"
#include "recorder/VideoRecorder.hpp"
#include "ui/MainWindow.hpp"
#include "util/FileUtils.hpp"
#include "util/GLIncludes.hpp"
#include "visualizer/RatingManager.hpp"

#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QStyleFactory>
#include <iostream>

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

        if (arg == "-h" || arg == "--help") {
            printHelp();
            std::exit(0);
        } else if (arg == "-v" || arg == "--version") {
            printVersion();
            std::exit(0);
        } else if (arg == "-d" || arg == "--debug") {
            opts.debug = true;
        } else if (arg == "--headless") {
            opts.headless = true;
        } else if (arg == "-r" || arg == "--record") {
            opts.startRecording = true;
        } else if (arg == "-o" || arg == "--output") {
            if (i + 1 >= argc_) {
                return Result<AppOptions>::err(
                        "--output requires a path argument");
            }
            opts.outputFile = fs::path(argv_[++i]);
        } else if (arg == "-c" || arg == "--config") {
            if (i + 1 >= argc_) {
                return Result<AppOptions>::err(
                        "--config requires a path argument");
            }
            opts.configFile = fs::path(argv_[++i]);
        } else if (arg == "-p" || arg == "--preset") {
            if (i + 1 >= argc_) {
                return Result<AppOptions>::err(
                        "--preset requires a name argument");
            }
            opts.presetName = argv_[++i];
        } else if (arg == "--default-preset") {
            opts.useDefaultPreset = true;
        } else if (arg[0] != '-') {
            // Positional argument - input file
            opts.inputFiles.push_back(fs::path(arg));
        } else {
            return Result<AppOptions>::err(std::string("Unknown option: ") +
                                           std::string(arg));
        }
    }

    return Result<AppOptions>::ok(std::move(opts));
}

Result<void> Application::init(const AppOptions& opts) {
    // Initialize logging first
    Logger::init("chadvis-projectm-qt", opts.debug);
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

    // Override debug from command line
    if (opts.debug) {
        CONFIG.setDebug(true);
    }

    // Override default preset from command line
    if (opts.useDefaultPreset) {
        CONFIG.visualizer().useDefaultPreset = true;
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

        // Load last session playlist if no files specified
        if (opts.inputFiles.empty()) {
            auto lastSession = file::configDir() / "last_session.m3u";
            if (fs::exists(lastSession)) {
                LOG_INFO("Loading last session playlist...");
                audioEngine_->playlist().loadM3U(lastSession);
            }
        } else {
            // Add input files to playlist
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
        LOG_WARN("Theme not found: {}, using default", themeName.toStdString());
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
    std::cout << "ChadVis Audio Player v1.0.0\n";
    std::cout << "Built with Qt " << qVersion() << "\n";
    std::cout << "\"I use Arch btw\"\n";
}

void Application::printHelp() {
    std::cout << R"(
ChadVis - Chad-tier Audio Visualizer for Arch Linux

Usage: chadvis-projectm-qt [options] [files...]

Options:
   -h, --help              Show this help message
   -v, --version           Show version information
   -d, --debug             Enable debug logging
   -c, --config <path>     Use custom config file
   -p, --preset <name>     Start with specific visualizer preset
   --default-preset        Use projectM's default visualizer (no preset)
   -r, --record            Start recording immediately
   -o, --output <path>     Output file for recording
   --headless              Run without GUI (for batch processing)

Examples:
   chadvis-projectm-qt ~/Music/*.flac
   chadvis-projectm-qt --record --output video.mp4 song.mp3
   chadvis-projectm-qt --preset "Aderrasi - Airhandler" playlist.m3u
   chadvis-projectm-qt --default-preset song.mp3

Config: ~/.config/chadvis-projectm-qt/config.toml
Logs:   ~/.cache/chadvis-projectm-qt/logs/

Pro tips:
  - Drag and drop files onto the window
  - Press F for fullscreen
  - Press R to toggle recording
  - Press Space to play/pause

Report bugs at: https://github.com/yourusername/chadvis-projectm-qt/issues
Or don't. We're not your mom.
)";
}

} // namespace vc
