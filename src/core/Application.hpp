/**
 * @file Application.hpp
 * @brief Main application lifecycle controller.
 *
 * Single responsibility: Manage application lifecycle and component ownership.
 * Delegates argument parsing to ArgParser, theming to ThemeLoader.
 *
 * @section Patterns
 * - Singleton: Global access to core components.
 * - Composition: Owns all major subsystems.
 */

#pragma once
#include <QApplication>
#include <memory>
#include "util/Result.hpp"
#include "util/Types.hpp"

namespace vc {

class MainWindow;
class AudioEngine;
class OverlayEngine;
class VideoRecorder;
class ThemeLoader;
struct AppOptions;

class Application : public QObject {
    Q_OBJECT

public:
    explicit Application(int& argc, char** argv);
    ~Application();

    Result<void> init(const AppOptions& opts);
    int exec();
    void reloadTheme();

    AudioEngine* audioEngine() const { return audioEngine_.get(); }
    OverlayEngine* overlayEngine() const { return overlayEngine_.get(); }
    VideoRecorder* videoRecorder() const { return videoRecorder_.get(); }
    MainWindow* mainWindow() const { return mainWindow_.get(); }

    static Application* instance() { return instance_; }

signals:
    void aboutToQuit();

public slots:
    void quit();

private:
    void applyConfigOverrides(const AppOptions& opts);

    static Application* instance_;

    std::unique_ptr<QApplication> qapp_;
    std::unique_ptr<ThemeLoader> themeLoader_;
    std::unique_ptr<AudioEngine> audioEngine_;
    std::unique_ptr<OverlayEngine> overlayEngine_;
    std::unique_ptr<VideoRecorder> videoRecorder_;
    std::unique_ptr<MainWindow> mainWindow_;

    int argc_;
    char** argv_;
};

#define APP vc::Application::instance()

} // namespace vc
