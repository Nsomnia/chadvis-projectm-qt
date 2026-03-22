/**
 * @file Application.hpp
 * @brief Main application controller.
 *
 * This file defines the Application class which acts as the central coordinator
 * for the application. It manages the lifecycle of the QML window, audio
 * engine, visualizer, and recorder. It also handles command-line argument
 * parsing.
 *
 * @section Dependencies
 * - QApplication
 * - AudioEngine
 * - OverlayEngine
 * - VideoRecorder
 *
 * @section Patterns
 * - Singleton: Global access to core components.
 * - Composition: Owns all major subsystems.
 */

#pragma once
#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <memory>
#include <vector>
#include "util/Result.hpp"
#include "util/Types.hpp"

namespace vc {

class AudioEngine;
class OverlayEngine;
class VideoRecorder;
class PresetManager;
class LyricsSync;

namespace suno {
class SunoController;
}

struct AppOptions {
  // General
  bool debug{false};
  bool headless{false};
  std::optional<fs::path> configFile;
  std::vector<fs::path> inputFiles;
    
    // Visualizer
    std::optional<std::string> presetName;
    bool useDefaultPreset{false};
    std::optional<int> visualizerFps;
    std::optional<int> visualizerWidth;
    std::optional<int> visualizerHeight;
    std::optional<bool> visualizerShuffle;
    
    // Recording
    bool startRecording{false};
    std::optional<fs::path> outputFile;
    std::optional<std::string> recordingCodec;
    std::optional<int> recordingCrf;
    std::optional<std::string> recordingPreset;
    
    // Audio
    std::optional<std::string> audioDevice;
    std::optional<int> audioBufferSize;
    std::optional<int> audioSampleRate;
    
    // Suno
    std::optional<std::string> sunoId;
    std::optional<fs::path> sunoDownloadPath;
    std::optional<bool> sunoAutoDownload;
    
    // Karaoke/Lyrics
    std::optional<fs::path> testLyricsFile;
    std::optional<bool> karaokeEnabled;
    std::optional<std::string> karaokeFont;
    std::optional<int> karaokeFontSize;
    std::optional<float> karaokeYPosition;
    
    // UI
    std::optional<std::string> theme;
};

class Application : public QObject {
    Q_OBJECT

public:
    explicit Application(int& argc, char** argv);
    ~Application();

    // Parse command line arguments
    Result<AppOptions> parseArgs();

    // Initialize and run
    Result<void> init(const AppOptions& opts);
    int exec();

    void reloadTheme();

	// Component access
	AudioEngine* audioEngine() const {
		return audioEngine_.get();
	}
	OverlayEngine* overlayEngine() const {
		return overlayEngine_.get();
	}
	VideoRecorder* videoRecorder() const {
		return videoRecorder_.get();
	}

	// Global instance
	static Application* instance() {
		return instance_;
	}

signals:
    void aboutToQuit();

public slots:
    void quit();

private:
    void setupStyle();
    void setupQmlStyle();
    void printVersion();
    void printHelp();

    static Application* instance_;

	std::unique_ptr<QApplication> qapp_;
	std::unique_ptr<QQmlApplicationEngine> qmlEngine_;
	// Components - Declaration order matters for destruction (reverse order)
	// We want engines to stay alive until the UI is gone
	std::unique_ptr<AudioEngine> audioEngine_;
	std::unique_ptr<OverlayEngine> overlayEngine_;
	std::unique_ptr<VideoRecorder> videoRecorder_;

	// QML-specific managers
	std::unique_ptr<PresetManager> presetManager_;
	std::unique_ptr<LyricsSync> lyricsSync_;
	std::unique_ptr<suno::SunoController> sunoController_;

	int argc_;
	char** argv_;
};

// Global shortcut
#define APP vc::Application::instance()

} // namespace vc
