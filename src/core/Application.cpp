
#include "Application.hpp"
#include "CliArg.hpp"
#include "CliUtils.hpp"
#include "Config.hpp"
#include "ConfigParsers.hpp"
#include "Logger.hpp"
#include "audio/AudioEngine.hpp"
#include "lyrics/LyricsSync.hpp"
#include "qml_bridge/BridgeRegistration.hpp"
#include "qml_bridge/RecordingBridge.hpp"
#include "qml_bridge/SettingsBridge.hpp"
#include "recorder/VideoRecorderCore.hpp"
#include "suno/SunoModels.hpp"
#include "ui/controllers/SunoController.hpp"
#include "util/FileUtils.hpp"
#include "visualizer/PresetManager.hpp"
#include "visualizer/RatingManager.hpp"
#include "visualizer/VisualizerWindow.hpp"

#include <QQmlEngine>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSurfaceFormat>
#include <QtQuickControls2/QQuickStyle>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <utility>

#ifndef CHADVIS_VERSION
#define CHADVIS_VERSION "unknown"
#endif

namespace vc {

Application* Application::instance_ = nullptr;

Application::Application(int& argc, char** argv) : argc_(argc), argv_(argv) {
    instance_ = this;

    std::signal(SIGINT, [](int) {
        if (instance_) {
            QMetaObject::invokeMethod(instance_, "quit", Qt::QueuedConnection);
        } else {
            std::exit(0);
        }
    });
    std::signal(SIGTERM, [](int) {
        if (instance_) {
            QMetaObject::invokeMethod(instance_, "quit", Qt::QueuedConnection);
        } else {
            std::exit(0);
        }
    });
}

Application::~Application() {
    // Cleanup order: QML engine first, then visualizer, then Qt app
    qmlEngine_.reset();
    qml_bridge::RecordingBridge::setVisualizer(nullptr);
    visualizerWindow_.reset();

    videoRecorder_.reset();
    sunoController_.reset();
    lyricsSync_.reset();
    presetManager_.reset();
    audioEngine_.reset();
    qapp_.reset();

    Logger::shutdown();
    instance_ = nullptr;
}

// ─── Table-driven flag name list for findClosestMatch ──────────────────────
std::vector<std::string_view> Application::allFlagNames() {
    // T0207: expanded from CliArgs.inc via Cli::cliArgTable() — the table is
    // the single source of truth, so typo suggestions can never go stale.
    std::vector<std::string_view> names;
    for (const auto& arg : Cli::cliArgTable()) {
        names.push_back(arg.longName);
        if (!arg.negationName.empty()) {
            names.push_back(arg.negationName);
        }
    }
    return names;
}

Result<AppOptions> Application::parseArgs() { return parseArgsFrom(argc_, argv_); }

// Pure parsing seam: the same grammar as parseArgs(), but driven by explicit
// argv so tests can pin flag combinations without constructing an Application
// (whose destructor tears down the global logger mid-test-binary). The
// parameter names deliberately carry the member convention: the table-driven
// body below reads argc_/argv_ throughout, and in this static scope those are
// parameters, not members.
Result<AppOptions> Application::parseArgsFrom(int argc_, const char* const argv_[]) {
    AppOptions opts;

    for (int i = 1; i < argc_; ++i) {
        std::string_view arg(argv_[i]);

        // ─── Special cases (manual) ────────────────────────
        if (arg == "-h" || arg == "--help") {
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
        }
        if (arg == "-v" || arg == "--version") {
            printVersion();
            std::exit(0);
        }
        if (arg == "--help-topics") {
            HelpSystem::listTopics();
            std::exit(0);
        }
        if (arg == "--generate-completion") {
            if (i + 1 >= argc_) {
                Cli::printError("--generate-completion requires a shell argument (bash/zsh/fish)");
                std::exit(1);
            }
            Cli::generateCompletionScript(argv_[++i]);
            std::exit(0);
        }

        // ─── Table-driven flag parsing ─────────────────────
        //
        // Each per-type macro (CLI_BOOL, CLI_INT, etc.) expands to a
        // self-contained if-block that matches the flag and parses the
        // value with the correct type. Only the matching macro for each
        // entry's type is ever invoked — wrong-type macros don't exist
        // for that entry, so no invalid code is generated.

        bool argHandled = false;

#define CLI_SECTION(name)
#define CLI_BOOL(longName, shortName, helpText, helpDefault, negationName, optsField,              \
                 configAccessor)                                                                   \
    if (!argHandled &&                                                                             \
        (arg == longName || (!std::string_view(shortName).empty() && arg == shortName))) {         \
        argHandled = true;                                                                         \
        opts.optsField = true;                                                                     \
    }                                                                                              \
    if (!argHandled && !std::string_view(negationName).empty() && arg == negationName) {           \
        argHandled = true;                                                                         \
        opts.optsField = false;                                                                    \
    }

#define CLI_INT(longName, shortName, helpText, helpDefault, negationName, optsField,               \
                configAccessor)                                                                    \
    if (!argHandled &&                                                                             \
        (arg == longName || (!std::string_view(shortName).empty() && arg == shortName))) {         \
        argHandled = true;                                                                         \
        if (i + 1 >= argc_) {                                                                      \
            Cli::printError(std::string(longName) + " requires an argument");                      \
            return Result<AppOptions>::err("Missing argument");                                    \
        }                                                                                          \
        try {                                                                                      \
            opts.optsField = std::stoi(argv_[++i]);                                                \
        } catch (...) {                                                                            \
            Cli::printError("Invalid integer for " + std::string(longName));                       \
            return Result<AppOptions>::err("Invalid value");                                       \
        }                                                                                          \
    }

#define CLI_FLOAT(longName, shortName, helpText, helpDefault, negationName, optsField,             \
                  configAccessor)                                                                  \
    if (!argHandled &&                                                                             \
        (arg == longName || (!std::string_view(shortName).empty() && arg == shortName))) {         \
        argHandled = true;                                                                         \
        if (i + 1 >= argc_) {                                                                      \
            Cli::printError(std::string(longName) + " requires an argument");                      \
            return Result<AppOptions>::err("Missing argument");                                    \
        }                                                                                          \
        try {                                                                                      \
            opts.optsField = std::stof(argv_[++i]);                                                \
        } catch (...) {                                                                            \
            Cli::printError("Invalid float for " + std::string(longName));                         \
            return Result<AppOptions>::err("Invalid value");                                       \
        }                                                                                          \
    }

#define CLI_STRING(longName, shortName, helpText, helpDefault, negationName, optsField,            \
                   configAccessor)                                                                 \
    if (!argHandled &&                                                                             \
        (arg == longName || (!std::string_view(shortName).empty() && arg == shortName))) {         \
        argHandled = true;                                                                         \
        if (i + 1 >= argc_) {                                                                      \
            Cli::printError(std::string(longName) + " requires an argument");                      \
            return Result<AppOptions>::err("Missing argument");                                    \
        }                                                                                          \
        opts.optsField = argv_[++i];                                                               \
    }

#define CLI_PATH(longName, shortName, helpText, helpDefault, negationName, optsField,              \
                 configAccessor)                                                                   \
    if (!argHandled &&                                                                             \
        (arg == longName || (!std::string_view(shortName).empty() && arg == shortName))) {         \
        argHandled = true;                                                                         \
        if (i + 1 >= argc_) {                                                                      \
            Cli::printError(std::string(longName) + " requires an argument");                      \
            return Result<AppOptions>::err("Missing argument");                                    \
        }                                                                                          \
        opts.optsField = fs::path(argv_[++i]);                                                     \
    }

#include "CliArgs.inc"
#undef CLI_SECTION
#undef CLI_BOOL
#undef CLI_INT
#undef CLI_FLOAT
#undef CLI_STRING
#undef CLI_PATH

        if (argHandled) continue;

        // ─── Positional input file ─────────────────────────
        if (arg[0] != '-') {
            opts.inputFiles.push_back(fs::path(arg));
            continue;
        }

        // ─── Unknown option with suggestions ───────────────
        auto flagNames = allFlagNames();
        auto suggestion = Cli::findClosestMatch(arg, {flagNames.data(), flagNames.size()});
        if (suggestion) {
            Cli::printUnknownFlagError(arg, {suggestion.value()});
        } else {
            Cli::printError(std::string("Unknown option: ") + std::string(arg));
        }
        return Result<AppOptions>::err(std::string("Unknown option: ") + std::string(arg));
    }

    // ─── Post-parse validation ────────────────────────────
    // Recording is wired to the visualizer window's GL context at init time
    // (frameCaptured -> VideoRecorder, RecordingBridge::setVisualizer — all
    // inside `if (!opts.headless)`), so --headless + --record cannot produce
    // a file: no window, no frames. Refuse the combination at parse time
    // rather than launching a batch session that silently never records.
    if (opts.headless && opts.startRecording) {
        Cli::printError("--headless and --record are mutually exclusive: recording "
                        "needs the visualizer window for its frames");
        return Result<AppOptions>::err("Incompatible flags");
    }

    return Result<AppOptions>::ok(std::move(opts));
}

// ─── CLI override helpers ──────────────────────────────────────────────────
namespace {

/// Apply an optional CLI override to a config field, with logging.
/// Separate template params for optional value type and config field type
/// to handle int→u32 and float→f32 conversions.
template <typename OptT, typename CfgT>
void applyOverride(const std::optional<OptT>& optVal, CfgT& configField, std::string_view name) {
    if (optVal) {
        configField = static_cast<CfgT>(*optVal);
        LOG_INFO("CLI override: {} = {}", name, *optVal);
    }
}

/// Overload for fs::path (needs .string() for fmt)
void applyOverride(const std::optional<fs::path>& optVal, fs::path& configField,
                   std::string_view name) {
    if (optVal) {
        configField = *optVal;
        LOG_INFO("CLI override: {} = {}", name, optVal->string());
    }
}

/// Overload for std::string (avoid char* decay ambiguity)
void applyOverride(const std::optional<std::string>& optVal, std::string& configField,
                   std::string_view name) {
    if (optVal) {
        configField = *optVal;
        LOG_INFO("CLI override: {} = {}", name, *optVal);
    }
}

/// Overload for bool (no static_cast needed, and fmt formats bool as 0/1 without it)
void applyOverride(const std::optional<bool>& optVal, bool& configField, std::string_view name) {
    if (optVal) {
        configField = *optVal;
        LOG_INFO("CLI override: {} = {}", name, *optVal);
    }
}

/// Warn when a CLI override landed outside the parser's legal range and was
/// pulled back by the clamp helpers below. Without this, --visualizer-fps 0
/// quietly becoming 10 would just be a quieter version of the old bug, where
/// it quietly stayed 60.
void warnIfClamped(const std::optional<int>& optVal, u32 effective, std::string_view name) {
    if (optVal && *optVal != static_cast<int>(effective)) {
        LOG_WARN("CLI override {} = {} is out of range, clamped to {} "
                 "(the same bounds the config file enforces)",
                 name, *optVal, effective);
    }
}

} // anonymous namespace

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
            LOG_WARN("Failed to load default config: {}", result.error().message);
            // Continue with defaults
        }
    }

    // Ensure logger respects final debug state if it changed after loading config
    if (!opts.debug && CONFIG.debug()) {
        Logger::init("chadvis-projectm-qt", true);
    }

    // T0205: --default-preset is std::optional<bool>, applied only when the
    // flag was actually passed. A plain bool could not distinguish "flag
    // absent" from "flag false", so every launch without --default-preset
    // clobbered a saved `true` back to false.
    if (opts.useDefaultPreset) {
        CONFIG.visualizer().useDefaultPreset = *opts.useDefaultPreset;
        LOG_INFO("CLI override: visualizer.useDefaultPreset = {}",
                 *opts.useDefaultPreset);
    }

    // Set debug lyrics from command line
    if (opts.testLyricsFile) {
        CONFIG.suno().debugLyrics = true;
        CONFIG.suno().debugLyricsFile = *opts.testLyricsFile;
        LOG_INFO("Debug lyrics enabled: {}", opts.testLyricsFile->string());
    }

    // ─── Apply CLI overrides to config ────────────────────────────────────
    // Audio overrides. parseAudio applies no range clamps to its fields, so
    // there is nothing to re-run after these the way there is below.
    applyOverride(opts.audioDevice, CONFIG.audio().device, "audio.device");
    applyOverride(opts.audioBufferSize, CONFIG.audio().bufferSize, "audio.bufferSize");
    applyOverride(opts.audioSampleRate, CONFIG.audio().sampleRate, "audio.sampleRate");

    // Visualizer overrides
    applyOverride(opts.visualizerFps, CONFIG.visualizer().fps, "visualizer.fps");
    applyOverride(opts.visualizerWidth, CONFIG.visualizer().width, "visualizer.width");
    applyOverride(opts.visualizerHeight, CONFIG.visualizer().height, "visualizer.height");
    applyOverride(opts.visualizerShuffle, CONFIG.visualizer().shufflePresets,
                  "visualizer.shufflePresets");
    // T0203: --preset was parsed but never consumed. It now feeds the existing
    // visualizer.forcePreset key, which pm::Bridge honors at init via
    // selectByName (useDefaultPreset still takes precedence there).
    applyOverride(opts.presetName, CONFIG.visualizer().forcePreset,
                  "visualizer.forcePreset");
    // Overrides write raw values into CONFIG after ConfigParsers has already
    // clamped the file-loaded values, so an out-of-range flag would sail past
    // every bound the parser enforces: --visualizer-fps 0 skipped the
    // `fps > 0` guard in VisualizerRenderer::initialize and silently kept the
    // 60 default, i.e. the flag did nothing. Re-run the parser's own clamp
    // helpers so a flag meets the same bounds as the file.
    ConfigParsers::clampVisualizer(CONFIG.visualizer());
    warnIfClamped(opts.visualizerFps, CONFIG.visualizer().fps, "visualizer.fps");
    warnIfClamped(opts.visualizerWidth, CONFIG.visualizer().width, "visualizer.width");
    warnIfClamped(opts.visualizerHeight, CONFIG.visualizer().height, "visualizer.height");

    // Recording overrides. Same rationale as the visualizer clamp above:
    // --recording-crf writes past parseRecording's clamp(crf, 0, 51).
    applyOverride(opts.recordingCodec, CONFIG.recording().video.codec, "recording.video.codec");
    applyOverride(opts.recordingCrf, CONFIG.recording().video.crf, "recording.video.crf");
    applyOverride(opts.recordingPreset, CONFIG.recording().video.preset, "recording.video.preset");
    ConfigParsers::clampVideoEncoder(CONFIG.recording().video);
    warnIfClamped(opts.recordingCrf, CONFIG.recording().video.crf, "recording.video.crf");

    // Suno overrides
    applyOverride(opts.sunoDownloadPath, CONFIG.suno().downloadPath, "suno.downloadPath");
    applyOverride(opts.sunoAutoDownload, CONFIG.suno().autoDownload, "suno.autoDownload");

    // Karaoke overrides
    applyOverride(opts.karaokeEnabled, CONFIG.karaoke().enabled, "karaoke.enabled");
    applyOverride(opts.karaokeFont, CONFIG.karaoke().fontFamily, "karaoke.fontFamily");
    applyOverride(opts.karaokeFontSize, CONFIG.karaoke().fontSize, "karaoke.fontSize");
    applyOverride(opts.karaokeYPosition, CONFIG.karaoke().yPosition, "karaoke.yPosition");

    // UI overrides
    //
    // T0192. --theme used to be applyOverride(opts.theme, CONFIG.ui().theme,
    // "ui.theme"), which writes into the live Config and is therefore persisted
    // by the next save. Harmless only while the flag did nothing; now that the
    // engine consumes ui().theme it would overwrite the user's saved theme on
    // every launch. So it goes to the session-only static instead, and the
    // user's persisted theme is left alone unless Settings says otherwise.
    // Empty (not "material") means no --theme was given, which is what lets the
    // saved theme through. A CLI_STRING parse yields std::optional<std::string>
    // so "absent" and "present" are distinguishable at all.
    qml_bridge::SettingsBridge::setSessionTheme(
        opts.theme ? QString::fromStdString(*opts.theme) : QString());

    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    format.setSwapInterval(1);
    format.setDepthBufferSize(24);
    format.setSamples(0);
    QSurfaceFormat::setDefaultFormat(format);

    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);

    // Create Qt application
    qapp_ = std::make_unique<QGuiApplication>(argc_, argv_);
    qapp_->setApplicationName("ChadVis");
    qapp_->setApplicationVersion(QStringLiteral(CHADVIS_VERSION));
    qapp_->setOrganizationName("ChadVis");
    qapp_->setOrganizationDomain("github.com/chadvis-projectm-qt");

    // Initialize components
    LOG_DEBUG("Initializing audio engine...");
    audioEngine_ = std::make_unique<AudioEngine>();
    // Explicit rather than letting init() reach for the Config singleton: this is
    // the only place the three --audio-* overrides have been folded into CONFIG,
    // so passing it here is what makes them reach the output device at all.
    if (auto result = audioEngine_->init(CONFIG.audio()); !result) {
        LOG_ERROR("Audio engine init failed: {}", result.error().message);
        return result;
    }

    // T0203: positional input files are loaded into the playlist at startup.
    // They were collected by the parser and never consumed — the first help
    // example (`chadvis-projectm-qt ~/Music/*.flac`) loaded nothing.
    if (!opts.inputFiles.empty()) {
        audioEngine_->playlist().addFiles(opts.inputFiles);
        LOG_INFO("Added {} input file(s) to the playlist", opts.inputFiles.size());
    }


    LOG_DEBUG("Initializing video recorder...");
    videoRecorder_ = std::make_unique<VideoRecorder>();
    // Feed the recorder from the engine's recording queue; without this the
    // queue pointer stays null and recorded video is silently mute.
    videoRecorder_->setAudioQueue(&audioEngine_->audioQueue());

    LOG_DEBUG("Initializing rating manager...");
    if (auto result = RatingManager::instance().load(); !result) {
        LOG_WARN("Failed to load preset ratings: {}", result.error().message);
    }

    // Create QML window (unless headless)
    if (!opts.headless) {
        LOG_DEBUG("Creating QML window...");

        // Create QML-specific managers
        LOG_DEBUG("Initializing preset manager for QML...");
        presetManager_ = std::make_unique<PresetManager>();
        // Favourites/blacklist state must be loaded BEFORE the async scan
        // launches: the worker snapshots the favourite name sets at request
        // time, so a scan started without them would publish a generation
        // that clobbers every favourite. This used to run inside pm::Bridge's
        // init on a second, privately-owned manager.
        if (auto result = presetManager_->loadState(file::configDir() / "preset_state.txt");
            !result) {
            LOG_WARN("Failed to load preset state: {}", result.error().message);
        }
        if (auto presetDir = CONFIG.visualizer().presetPath; !presetDir.empty()) {
            // Scanning a real preset library is thousands of stat() calls plus a
            // parse per file, which is far too slow to run before the window even
            // exists. scanAsync() walks the tree on the manager's scan worker and
            // publishes the finished list back on a GUI thread.
            //
            // qapp_ (created above) is the publish context: it is a QObject that
            // already lives in the main thread, and it outlives presetManager_ in
            // ~Application(), so the worker's queued hand-off can never target a
            // destroyed receiver. The result is delivered by a queued event, and
            // init() never spins an event loop, so nothing is published until
            // exec() starts — long after registerBridges() has attached
            // PresetBridge and main.qml has read the (empty) list once.
            presetManager_->setPublishContext(qapp_.get());
            presetManager_->scanFailed.connect([](const std::string& message) {
                LOG_WARN("Failed to scan presets: {}", message);
            });
            presetManager_->scanAsync(presetDir, true);
        }

        LOG_DEBUG("Creating VisualizerWindow for QML embedding...");
        visualizerWindow_ = std::make_unique<VisualizerWindow>(*presetManager_);
        visualizerWindow_->setMinimumSize(QSize(640, 480));
        // Renderer owns the visualizer PCM consumer; wire it to the engine queue.
        visualizerWindow_->renderer().setAudioQueue(&audioEngine_->audioQueue());

        // Composition root: this is the single frame hand-off into recording.
        // The lambda only moves the frame into VideoRecorder's bounded queue;
        // encoding and audio work stay owned by the recorder worker, so the
        // render thread never performs codec work or waits for the encoder.
        QObject::connect(visualizerWindow_.get(), &VisualizerWindow::frameCaptured,
                         [recorder = videoRecorder_.get()](std::vector<u8> data, u32 width,
                                                           u32 height, i64 timestamp) {
                             recorder->submitVideoFrame(std::move(data), width, height, timestamp);
                         });

        // The bridge needs the sibling visualizer to coordinate capture with
        // the encoder; this is the same native window registered with QML.
        qml_bridge::RecordingBridge::setVisualizer(visualizerWindow_.get());

        LOG_DEBUG("Initializing lyrics sync for QML...");
        lyricsSync_ = std::make_unique<LyricsSync>(audioEngine_.get());

        LOG_DEBUG("Initializing Suno controller for QML...");
        sunoController_ = std::make_unique<suno::SunoController>(audioEngine_.get(),
                                                                 lyricsSync_.get(), nullptr);

        // T0203: --suno-id plays a Suno clip by ID. It resolves against the
        // library/database, so an unknown ID warns instead of silently
        // doing nothing (sync the library first).
        if (opts.sunoId) {
            if (sunoController_->playClipById(*opts.sunoId)) {
                LOG_INFO("--suno-id: playing clip '{}'", *opts.sunoId);
            } else {
                LOG_WARN("--suno-id: clip '{}' not found in library; sync the library first",
                         *opts.sunoId);
            }
        }

        // Pin a non-native Quick Controls style BEFORE the engine exists. The
        // macOS native style silently discards the custom `background` /
        // `contentItem` on every control, so the themed Settings window and
        // panels would not render as designed — and each one logs a warning.
        QQuickStyle::setStyle("Fusion");

        qmlEngine_ = std::make_unique<QQmlApplicationEngine>();

        // Register all QML bridge singletons BEFORE loading main.qml;
        // without this the QML side sees no AudioBridge/SettingsBridge/etc.
        qml_bridge::registerBridges(qmlEngine_.get(), audioEngine_.get(), visualizerWindow_.get(),
                                    videoRecorder_.get(), presetManager_.get(), lyricsSync_.get(),
                                    sunoController_.get());

        // Connect to QML warnings for debugging
        QObject::connect(qmlEngine_.get(), &QQmlEngine::warnings,
                         [](const QList<QQmlError>& warnings) {
                             for (const auto& warning : warnings) {
                                 LOG_ERROR("QML Warning: {} (line {})",
                                           warning.description().toStdString(), warning.line());
                             }
                         });

        // Load main QML file from Qt resource system
        const QUrl url(QStringLiteral("qrc:/qt/qml/ChadVis/src/qml/main.qml"));

        QObject::connect(qmlEngine_.get(), &QQmlApplicationEngine::objectCreated, this,
                         [url](QObject* obj, const QUrl& objUrl) {
                             if (!obj && objUrl == url) {
                                 LOG_ERROR("Failed to create QML window");
                             } else if (obj) {
                                 LOG_INFO("QML window created successfully");
                             }
                         });

        qmlEngine_->load(url);
        const QList<QObject*> roots = qmlEngine_->rootObjects();
        if (roots.isEmpty()) {
            LOG_ERROR("QML root load returned no objects");
            return Result<void>::err("QML root load returned no objects");
        }
        auto* window = qobject_cast<QQuickWindow*>(roots.first());
        if (!window) {
            LOG_ERROR("QML root is not a QQuickWindow");
            return Result<void>::err("QML root is not a QQuickWindow");
        }
        if (!window->isVisible()) {
            window->show();
        }

        // T0203: --record now starts recording in GUI mode. (--headless +
        // --record is rejected at parse time: no window, no frames.) The
        // RecordingBridge singleton exists by now — main.qml references it,
        // so the load above constructed it — and setVisualizer wired the
        // frame source. An empty outputPath keeps the bridge's default naming.
        if (opts.startRecording) {
            const QString outputPath = opts.outputFile
                ? QString::fromStdString(opts.outputFile->string())
                : QString();
            if (auto* bridge = qml_bridge::RecordingBridge::instance()) {
                bridge->startRecording(outputPath);
                LOG_INFO("--record: recording started");
            } else {
                LOG_WARN("--record: RecordingBridge unavailable; recording not started");
            }
        } else if (opts.outputFile) {
            LOG_WARN("--output has no effect without --record");
        }
    }

    // Connect quit signal
    connect(qapp_.get(), &QGuiApplication::aboutToQuit, this, &Application::aboutToQuit);
    // Ratings live in a process-wide in-memory map from load() at init until
    // here. aboutToQuit is the one UNIVERSAL exit hook — a QML window close
    // and Qt.quit() end the loop without ever invoking Application::quit()
    // (that slot only runs from the SIGINT/SIGTERM handlers), so persisting in
    // quit() alone would still lose ratings on the most common exit path.
    // aboutToQuit fires after every one of them, including quit()-driven ones.
    connect(qapp_.get(), &QGuiApplication::aboutToQuit, this, []() {
        if (auto result = RatingManager::instance().save(); !result) {
            LOG_WARN("Failed to save preset ratings: {}", result.error().message);
        } else {
            LOG_DEBUG("Saved preset ratings");
        }
    });
    // T0206: persist the session playlist and dirty config on EVERY exit.
    // Window close and Qt.quit() never run quit(); aboutToQuit fires for all
    // of them (including quit()-driven ones, via qapp_->quit() below).
    connect(qapp_.get(), &QGuiApplication::aboutToQuit, this,
            [this]() { savePersistentState(); });

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

// T0206: the single persistence path for every exit. aboutToQuit is the one
// universal exit hook — a QML window close or Qt.quit() ends the event loop
// without ever invoking Application::quit() (that slot only runs from the
// SIGINT/SIGTERM handlers), so the session playlist and dirty config are saved
// here. quit() reaches this too, through the aboutToQuit that qapp_->quit()
// fires, which is why quit() no longer persists inline.
void Application::savePersistentState() {
    if (audioEngine_) {
        auto lastSession = file::configDir() / "last_session.m3u";
        if (auto result = audioEngine_->playlist().saveM3U(lastSession); !result) {
            LOG_WARN("Failed to save session playlist to {}: {}", lastSession.string(),
                     result.error().message);
        } else {
            LOG_DEBUG("Saved session playlist to {}", lastSession.string());
        }
    }

    if (CONFIG.isDirty()) {
        (void)CONFIG.save(CONFIG.configPath());
    }
}

void Application::quit() {
    LOG_INFO("Shutting down...");

    // Stop both recording halves before finalizing the encoder.
    if (visualizerWindow_) {
        visualizerWindow_->stopRecording();
    }
    if (videoRecorder_ && videoRecorder_->isRecording()) {
        (void)videoRecorder_->stop();
    }

    // Stop audio. Persistence (session playlist, dirty config) is handled by
    // savePersistentState() on aboutToQuit, which qapp_->quit() fires below.
    if (audioEngine_) {
        audioEngine_->stop();
    }

    if (qapp_) {
        qapp_->quit();
    }
}

void Application::printVersion() { std::cout << Cli::versionBanner(); }

void Application::printHelp() {
    using namespace CliColor;

    std::cout << "\n"
              << brightCyan() << bold()
              << "╔════════════════════════════════════════════════════════════╗\n"
              << "║ ChadVis - Chad-tier Audio Visualizer ║\n"
              << "╚════════════════════════════════════════════════════════════╝" << reset()
              << "\n\n";

    Cli::printSection("Usage");
    std::cout << " chadvis-projectm-qt [options] [files...]\n"
              << " " << dim() << "chadvis-projectm-qt --help <topic> # Detailed topic help"
              << reset() << "\n"
              << " " << dim() << "chadvis-projectm-qt --help-topics # List help topics" << reset()
              << "\n\n";

    // T0207: every option below is generated from CliArgs.inc via
    // Cli::cliArgTable(). Adding a flag to the table is sufficient — no
    // hand-written list to drift. Meta flags handled as special cases in
    // parseArgsFrom (outside the table) are listed explicitly.
    Cli::printOption("-h, --help [topic]", "Show this help or detailed topic help");
    Cli::printOption("--help-topics", "List available help topics");
    Cli::printOption("-v, --version", "Show version information");
    Cli::printOption("--generate-completion <shell>", "Generate shell completion script");
    {
        std::string_view lastSection;
        for (const auto& arg : Cli::cliArgTable()) {
            if (arg.section != lastSection) {
                Cli::printSection(arg.section);
                lastSection = arg.section;
            }
            Cli::printCliArg(arg);
        }
    }

    Cli::printSection("Examples");
    std::cout << " " << brightCyan() << "chadvis-projectm-qt ~/Music/*.flac" << reset() << "\n"
              << " " << dim() << "# Play all FLAC files" << reset() << "\n\n"
              << " " << brightCyan() << "chadvis-projectm-qt -r -o video.mp4 song.mp3" << reset()
              << "\n"
              << " " << dim() << "# Record to video.mp4" << reset() << "\n\n"
              << " " << brightCyan() << "chadvis-projectm-qt --recording-codec h264_nvenc -r"
              << reset() << "\n"
              << " " << dim() << "# Use NVIDIA hardware encoding" << reset() << "\n\n"
              << " " << brightCyan() << "chadvis-projectm-qt --help recording" << reset() << "\n"
              << " " << dim() << "# Detailed recording help" << reset() << "\n\n";

    Cli::printSection("Keyboard Shortcuts");
    std::cout << " " << brightYellow() << "Space" << reset() << " Play/Pause "
              << " " << brightYellow() << "R" << reset() << " Toggle recording\n"
              << " " << brightYellow() << "N/P" << reset() << " Next/Prev "
              << " " << brightYellow() << "F" << reset() << " Fullscreen\n"
              << " " << brightYellow() << "← →" << reset() << " Prev/Next preset\n\n";

    std::cout << "Config: " << brightYellow() << "~/.config/chadvis-projectm-qt/config.toml"
              << reset() << "\n"
              << "Logs: " << brightYellow() << "~/.cache/chadvis-projectm-qt/logs/" << reset()
              << "\n\n"
              << "Docs: " << brightBlue() << "https://github.com/Nsomnia/chadvis-projectm-qt"
              << reset() << "\n"
              << dim() << "Or don't. We're not your mom." << reset() << "\n\n";
}

} // namespace vc
