
#include "Application.hpp"
#include "CliArg.hpp"
#include "CliUtils.hpp"
#include "Config.hpp"
#include "Logger.hpp"
#include "audio/AudioEngine.hpp"
#include "recorder/VideoRecorder.hpp"
#include "util/FileUtils.hpp"
#include "util/GLIncludes.hpp"
#include "visualizer/RatingManager.hpp"
#include "visualizer/PresetManager.hpp"
#include "visualizer/VisualizerWindow.hpp"
#include "qml_bridge/VisualizerItem.hpp"
#include "qml_bridge/VisualizerQFBO.hpp"
#include "lyrics/LyricsSync.hpp"
#include "ui/controllers/SunoController.hpp"
#include "suno/SunoModels.hpp"

#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QStyleFactory>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSurfaceFormat>
#include <iostream>
#include <cstdlib>
#include <csignal>

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
	visualizerWindow_.reset();

	videoRecorder_.reset();
	audioEngine_.reset();
	qapp_.reset();

	Logger::shutdown();
	instance_ = nullptr;
}

// ─── Table-driven flag name list for findClosestMatch ──────────────────────
std::vector<std::string_view> Application::allFlagNames() {
	std::vector<std::string_view> names;
	// Per-type macros: all just collect the long name (and negation for bools)
#define CLI_BOOL(longName, shortName, helpText, helpDefault, negationName, optsField, configAccessor) \
	names.push_back(longName); \
	if (!std::string_view(negationName).empty()) { names.push_back(negationName); }
#define CLI_INT(longName, shortName, helpText, helpDefault, negationName, optsField, configAccessor) \
	names.push_back(longName);
#define CLI_FLOAT(longName, shortName, helpText, helpDefault, negationName, optsField, configAccessor) \
	names.push_back(longName);
#define CLI_STRING(longName, shortName, helpText, helpDefault, negationName, optsField, configAccessor) \
	names.push_back(longName);
#define CLI_PATH(longName, shortName, helpText, helpDefault, negationName, optsField, configAccessor) \
	names.push_back(longName);
#include "CliArgs.inc"
#undef CLI_BOOL
#undef CLI_INT
#undef CLI_FLOAT
#undef CLI_STRING
#undef CLI_PATH
	return names;
}

Result<AppOptions> Application::parseArgs() {
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

#define CLI_BOOL(longName, shortName, helpText, helpDefault, negationName, optsField, configAccessor) \
		if (!argHandled && (arg == longName || (!std::string_view(shortName).empty() && arg == shortName))) { \
			argHandled = true; \
			opts.optsField = true; \
		} \
		if (!argHandled && !std::string_view(negationName).empty() && arg == negationName) { \
			argHandled = true; \
			opts.optsField = false; \
		}

#define CLI_INT(longName, shortName, helpText, helpDefault, negationName, optsField, configAccessor) \
		if (!argHandled && (arg == longName || (!std::string_view(shortName).empty() && arg == shortName))) { \
			argHandled = true; \
			if (i + 1 >= argc_) { \
				Cli::printError(std::string(longName) + " requires an argument"); \
				return Result<AppOptions>::err("Missing argument"); \
			} \
			try { opts.optsField = std::stoi(argv_[++i]); } \
			catch (...) { \
				Cli::printError("Invalid integer for " + std::string(longName)); \
				return Result<AppOptions>::err("Invalid value"); \
			} \
		}

#define CLI_FLOAT(longName, shortName, helpText, helpDefault, negationName, optsField, configAccessor) \
		if (!argHandled && (arg == longName || (!std::string_view(shortName).empty() && arg == shortName))) { \
			argHandled = true; \
			if (i + 1 >= argc_) { \
				Cli::printError(std::string(longName) + " requires an argument"); \
				return Result<AppOptions>::err("Missing argument"); \
			} \
			try { opts.optsField = std::stof(argv_[++i]); } \
			catch (...) { \
				Cli::printError("Invalid float for " + std::string(longName)); \
				return Result<AppOptions>::err("Invalid value"); \
			} \
		}

#define CLI_STRING(longName, shortName, helpText, helpDefault, negationName, optsField, configAccessor) \
		if (!argHandled && (arg == longName || (!std::string_view(shortName).empty() && arg == shortName))) { \
			argHandled = true; \
			if (i + 1 >= argc_) { \
				Cli::printError(std::string(longName) + " requires an argument"); \
				return Result<AppOptions>::err("Missing argument"); \
			} \
			opts.optsField = argv_[++i]; \
		}

#define CLI_PATH(longName, shortName, helpText, helpDefault, negationName, optsField, configAccessor) \
		if (!argHandled && (arg == longName || (!std::string_view(shortName).empty() && arg == shortName))) { \
			argHandled = true; \
			if (i + 1 >= argc_) { \
				Cli::printError(std::string(longName) + " requires an argument"); \
				return Result<AppOptions>::err("Missing argument"); \
			} \
			opts.optsField = fs::path(argv_[++i]); \
		}

#include "CliArgs.inc"
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

	return Result<AppOptions>::ok(std::move(opts));
}

// ─── CLI override helpers ──────────────────────────────────────────────────
namespace {

/// Apply an optional CLI override to a config field, with logging.
/// Separate template params for optional value type and config field type
/// to handle int→u32 and float→f32 conversions.
template<typename OptT, typename CfgT>
void applyOverride(const std::optional<OptT>& optVal, CfgT& configField, std::string_view name) {
	if (optVal) {
		configField = static_cast<CfgT>(*optVal);
		LOG_INFO("CLI override: {} = {}", name, *optVal);
	}
}

/// Overload for fs::path (needs .string() for fmt)
void applyOverride(const std::optional<fs::path>& optVal, fs::path& configField, std::string_view name) {
	if (optVal) {
		configField = *optVal;
		LOG_INFO("CLI override: {} = {}", name, optVal->string());
	}
}

/// Overload for std::string (avoid char* decay ambiguity)
void applyOverride(const std::optional<std::string>& optVal, std::string& configField, std::string_view name) {
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

	// ─── Apply CLI overrides to config ────────────────────────────────────
	// Audio overrides
	applyOverride(opts.audioDevice,      CONFIG.audio().device,      "audio.device");
	applyOverride(opts.audioBufferSize,  CONFIG.audio().bufferSize,  "audio.bufferSize");
	applyOverride(opts.audioSampleRate,  CONFIG.audio().sampleRate,  "audio.sampleRate");

	// Visualizer overrides
	applyOverride(opts.visualizerFps,     CONFIG.visualizer().fps,            "visualizer.fps");
	applyOverride(opts.visualizerWidth,   CONFIG.visualizer().width,          "visualizer.width");
	applyOverride(opts.visualizerHeight,  CONFIG.visualizer().height,         "visualizer.height");
	applyOverride(opts.visualizerShuffle, CONFIG.visualizer().shufflePresets, "visualizer.shufflePresets");

	// Recording overrides
	applyOverride(opts.recordingCodec,  CONFIG.recording().video.codec,  "recording.video.codec");
	applyOverride(opts.recordingCrf,    CONFIG.recording().video.crf,    "recording.video.crf");
	applyOverride(opts.recordingPreset, CONFIG.recording().video.preset, "recording.video.preset");

	// Suno overrides
	applyOverride(opts.sunoDownloadPath, CONFIG.suno().downloadPath,  "suno.downloadPath");
	applyOverride(opts.sunoAutoDownload, CONFIG.suno().autoDownload,  "suno.autoDownload");

	// Karaoke overrides
	applyOverride(opts.karaokeEnabled,   CONFIG.karaoke().enabled,    "karaoke.enabled");
	applyOverride(opts.karaokeFont,      CONFIG.karaoke().fontFamily, "karaoke.fontFamily");
	applyOverride(opts.karaokeFontSize,  CONFIG.karaoke().fontSize,   "karaoke.fontSize");
	applyOverride(opts.karaokeYPosition, CONFIG.karaoke().yPosition,  "karaoke.yPosition");

	// UI overrides
	applyOverride(opts.theme, CONFIG.ui().theme, "ui.theme");

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


	LOG_DEBUG("Initializing video recorder...");
	videoRecorder_ = std::make_unique<VideoRecorder>();

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
		if (auto presetDir = CONFIG.visualizer().presetPath; !presetDir.empty()) {
			presetManager_->scan(presetDir, true);
		}

		LOG_DEBUG("Creating VisualizerWindow for QML embedding...");
		visualizerWindow_ = std::make_unique<VisualizerWindow>();
		visualizerWindow_->setMinimumSize(QSize(640, 480));

		LOG_DEBUG("Initializing lyrics sync for QML...");
		lyricsSync_ = std::make_unique<LyricsSync>(audioEngine_.get());

		LOG_DEBUG("Initializing Suno controller for QML...");
		sunoController_ = std::make_unique<suno::SunoController>(
			audioEngine_.get(), nullptr);

		qmlEngine_ = std::make_unique<QQmlApplicationEngine>();

		// Connect to QML warnings for debugging
		QObject::connect(qmlEngine_.get(), &QQmlEngine::warnings, [](const QList<QQmlError>& warnings) {
			for (const auto& warning : warnings) {
				LOG_ERROR("QML Warning: {} (line {})", warning.description().toStdString(), warning.line());
			}
		});

		// Setup static global items for QML custom types
		if (audioEngine_) {
			qml_bridge::VisualizerItem::setGlobalAudioEngine(audioEngine_.get());
			qml_bridge::VisualizerQFBO::setGlobalAudioEngine(audioEngine_.get());
		}
		if (presetManager_) {
			qml_bridge::VisualizerItem::setGlobalPresetManager(presetManager_.get());
			qml_bridge::VisualizerQFBO::setGlobalPresetManager(presetManager_.get());
		}

		// Load main QML file from Qt resource system
		const QUrl url(QStringLiteral("qrc:/qt/qml/ChadVis/src/qml/main.qml"));

		QObject::connect(qmlEngine_.get(), &QQmlApplicationEngine::objectCreated,
		this, [url](QObject* obj, const QUrl& objUrl) {
			if (!obj && objUrl == url) {
				LOG_ERROR("Failed to create QML window");
			} else if (obj) {
				LOG_INFO("QML window created successfully");
			}
		});

		qmlEngine_->load(url);
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

void Application::reloadTheme() {
	setupStyle();
}

void Application::setupStyle() {
	// QML handles its own styling via Theme.qml
	// This function is kept for API compatibility but does nothing
}

void Application::setupQmlStyle() {
	// QML handles its own styling via Theme.qml
}

void Application::printVersion() {
	using namespace CliColor;
	std::cout << "\n" << brightCyan() << bold()
		<< "╔═══════════════════════════════════════════╗\n"
		<< "║ ChadVis Audio Player ║\n"
		<< "╚═══════════════════════════════════════════╝" << reset() << "\n"
		<< " Version: " << brightGreen() << "1.0.0" << reset() << "\n"
		<< " Built with Qt: " << brightGreen() << qVersion() << reset() << "\n"
		<< " " << dim() << "\"I use Arch btw\"" << reset() << "\n\n";
}

void Application::printHelp() {
	using namespace CliColor;

	std::cout << "\n" << brightCyan() << bold()
		<< "╔════════════════════════════════════════════════════════════╗\n"
		<< "║ ChadVis - Chad-tier Audio Visualizer for Arch Linux ║\n"
		<< "╚════════════════════════════════════════════════════════════╝" << reset() << "\n\n";

	Cli::printSection("Usage");
	std::cout << " chadvis-projectm-qt [options] [files...]\n"
		<< " " << dim() << "chadvis-projectm-qt --help <topic> # Detailed topic help" << reset() << "\n"
		<< " " << dim() << "chadvis-projectm-qt --help-topics # List help topics" << reset() << "\n\n";

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
	std::cout << " " << brightCyan() << "chadvis-projectm-qt ~/Music/*.flac" << reset() << "\n"
		<< " " << dim() << "# Play all FLAC files" << reset() << "\n\n"
		<< " " << brightCyan() << "chadvis-projectm-qt -r -o video.mp4 song.mp3" << reset() << "\n"
		<< " " << dim() << "# Record to video.mp4" << reset() << "\n\n"
		<< " " << brightCyan() << "chadvis-projectm-qt --recording-codec h264_nvenc -r" << reset() << "\n"
		<< " " << dim() << "# Use NVIDIA hardware encoding" << reset() << "\n\n"
		<< " " << brightCyan() << "chadvis-projectm-qt --help recording" << reset() << "\n"
		<< " " << dim() << "# Detailed recording help" << reset() << "\n\n";

	Cli::printSection("Keyboard Shortcuts");
	std::cout << " " << brightYellow() << "Space" << reset() << " Play/Pause "
		<< " " << brightYellow() << "R" << reset() << " Toggle recording\n"
		<< " " << brightYellow() << "N/P" << reset() << " Next/Prev "
		<< " " << brightYellow() << "F" << reset() << " Fullscreen\n"
		<< " " << brightYellow() << "← →" << reset() << " Prev/Next preset\n\n";

	std::cout << "Config: " << brightYellow() << "~/.config/chadvis-projectm-qt/config.toml" << reset() << "\n"
		<< "Logs: " << brightYellow() << "~/.cache/chadvis-projectm-qt/logs/" << reset() << "\n\n"
		<< "Docs: " << brightBlue() << "https://github.com/yourusername/chadvis-projectm-qt" << reset() << "\n"
		<< dim() << "Or don't. We're not your mom." << reset() << "\n\n";
}

} // namespace vc
