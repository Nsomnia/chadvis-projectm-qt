#pragma once
// Application.hpp - Main application controller
// The conductor of this symphony of audio chaos

#include "util/Types.hpp"
#include "util/Result.hpp"
#include <QApplication>
#include <memory>
#include <vector>

namespace vc {

class MainWindow;
class AudioEngine;
class OverlayEngine;
class VideoRecorder;

struct AppOptions {
    bool debug{false};
    bool headless{false};
    bool startRecording{false};
    std::optional<fs::path> outputFile;
    std::optional<fs::path> configFile;
    std::vector<fs::path> inputFiles;
    std::optional<std::string> presetName;
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
    
    // Component access
    AudioEngine* audioEngine() const { return audioEngine_.get(); }
    OverlayEngine* overlayEngine() const { return overlayEngine_.get(); }
    VideoRecorder* videoRecorder() const { return videoRecorder_.get(); }
    MainWindow* mainWindow() const { return mainWindow_.get(); }
    
    // Global instance
    static Application* instance() { return instance_; }
    
signals:
    void aboutToQuit();
    
public slots:
    void quit();
    
private:
    void setupStyle();
    void printVersion();
    void printHelp();
    
    static Application* instance_;
    
    std::unique_ptr<QApplication> qapp_;
    std::unique_ptr<MainWindow> mainWindow_;
    std::unique_ptr<AudioEngine> audioEngine_;
    std::unique_ptr<OverlayEngine> overlayEngine_;
    std::unique_ptr<VideoRecorder> videoRecorder_;
    
    int argc_;
    char** argv_;
};

// Global shortcut
#define APP vc::Application::instance()

} // namespace vc
