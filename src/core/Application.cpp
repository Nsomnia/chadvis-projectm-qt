/**
 * @file Application.cpp
 * @brief Main application lifecycle controller implementation.
 */

#include "Application.hpp"
#include "ArgParser.hpp"
#include "HelpPrinter.hpp"
#include "ThemeLoader.hpp"
#include "Config.hpp"
#include "Logger.hpp"
#include "audio/AudioEngine.hpp"
#include "overlay/OverlayEngine.hpp"
#include "recorder/VideoRecorder.hpp"
#include "ui/MainWindow.hpp"
#include "util/FileUtils.hpp"
#include "visualizer/RatingManager.hpp"
#include "ui/controllers/SunoController.hpp"
#include "suno/SunoModels.hpp"

#include <cstdlib>

namespace vc {

Application* Application::instance_ = nullptr;

Application::Application(int& argc, char** argv) : argc_(argc), argv_(argv) {
    instance_ = this;
}

Application::~Application() {
    mainWindow_.reset();
    videoRecorder_.reset();
    overlayEngine_.reset();
    audioEngine_.reset();
    themeLoader_.reset();
    qapp_.reset();

    Logger::shutdown();
    instance_ = nullptr;
}

Result<void> Application::init(const AppOptions& opts) {
    bool debug = opts.debug || CONFIG.debug();
    Logger::init("chadvis-projectm-qt", debug);
    LOG_INFO("chadvis-projectm-qt starting up. I use Arch btw.");

    if (opts.configFile) {
        if (auto result = CONFIG.load(*opts.configFile); !result) {
            LOG_ERROR("Failed to load config: {}", result.error().message);
            return result;
        }
    } else {
        if (auto result = CONFIG.loadDefault(); !result) {
            LOG_WARN("Failed to load default config: {}", result.error().message);
        }
    }

    if (!opts.debug && CONFIG.debug()) {
        Logger::init("chadvis-projectm-qt", true);
    }

    if (opts.useDefaultPreset) {
        CONFIG.visualizer().useDefaultPreset = true;
    } else {
        CONFIG.visualizer().useDefaultPreset = false;
    }

    if (opts.testLyricsFile) {
        CONFIG.suno().debugLyrics = true;
        CONFIG.suno().debugLyricsFile = *opts.testLyricsFile;
        LOG_INFO("Debug lyrics enabled: {}", opts.testLyricsFile->string());
    }

    applyConfigOverrides(opts);

    qapp_ = std::make_unique<QApplication>(argc_, argv_);
    qapp_->setApplicationName("ChadVis");
    qapp_->setApplicationVersion("1.1.0-alpha");
    qapp_->setOrganizationName("ChadVis");
    qapp_->setOrganizationDomain("github.com/chadvis-projectm-qt");

    themeLoader_ = std::make_unique<ThemeLoader>(qapp_.get());
    themeLoader_->load(QString::fromStdString(CONFIG.ui().theme));

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

        if (!opts.inputFiles.empty()) {
            LOG_INFO("Auto-playing first track");
            mainWindow_->audioEngine()->play();
        }

        if (opts.startRecording) {
            if (opts.outputFile) {
                mainWindow_->startRecording(*opts.outputFile);
            } else {
                mainWindow_->startRecording();
            }
        }

        if (opts.presetName) {
            LOG_INFO("Application: Requesting preset '{}'", *opts.presetName);
            mainWindow_->selectPreset(*opts.presetName);
        } else if (opts.useDefaultPreset) {
            LOG_INFO("Using default projectM visualizer (no preset selected)");
        }
    }

    connect(qapp_.get(), &QApplication::aboutToQuit, this, &Application::aboutToQuit);

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

void Application::applyConfigOverrides(const AppOptions& opts) {
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

    if (opts.sunoDownloadPath) {
        CONFIG.suno().downloadPath = *opts.sunoDownloadPath;
        LOG_INFO("CLI override: suno.downloadPath = {}", opts.sunoDownloadPath->string());
    }
    if (opts.sunoAutoDownload) {
        CONFIG.suno().autoDownload = *opts.sunoAutoDownload;
        LOG_INFO("CLI override: suno.autoDownload = {}", *opts.sunoAutoDownload);
    }

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

    if (opts.theme) {
        CONFIG.ui().theme = *opts.theme;
        LOG_INFO("CLI override: ui.theme = {}", *opts.theme);
    }
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

    if (videoRecorder_ && videoRecorder_->isRecording()) {
        videoRecorder_->stop();
    }

    if (audioEngine_) {
        auto lastSession = file::configDir() / "last_session.m3u";
        audioEngine_->playlist().saveM3U(lastSession);
        LOG_DEBUG("Saved session playlist to {}", lastSession.string());
        audioEngine_->stop();
    }

    if (CONFIG.isDirty()) {
        CONFIG.save(CONFIG.configPath());
    }

    if (qapp_) {
        qapp_->quit();
    }

    std::exit(0);
}

void Application::reloadTheme() {
    if (themeLoader_) {
        themeLoader_->reload();
    }
}

} // namespace vc
