#pragma once
// MainWindow.hpp - The main application window
// Now refactored to use specialized controllers for logic.

// clang-format off
#include "util/GLIncludes.hpp" // Must be first
// clang-format on

#include "audio/AudioEngine.hpp"
#include "overlay/OverlayEngine.hpp"
#include "recorder/VideoRecorder.hpp"
#include "util/Types.hpp"

#include <QMainWindow>
#include <QTimer>
#include <functional>
#include <memory>

class QDockWidget;

namespace vc {

class VisualizerPanel;

namespace suno {
class SunoController;
}

namespace ui::qml {
class QmlWorkspaceHost;
class PlaybackViewModel;
class PlaylistTrackModel;
class SunoRemoteLibraryViewModel;
class RecordingViewModel;
class VisualizerViewModel;
} // namespace ui::qml

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    // Public interface for Application
    void addToPlaylist(const fs::path& path);
    void addToPlaylist(const std::vector<fs::path>& paths);

    // Component Accessors
    AudioEngine* audioEngine() {
        return audioEngine_;
    }
    VisualizerPanel* visualizerPanel() {
        return visualizerPanel_;
    }
    VideoRecorder* videoRecorder() {
        return videoRecorder_;
    }
    OverlayEngine* overlayEngine() {
        return overlayEngine_;
    }

    suno::SunoController* sunoController() {
        return sunoController_.get();
    }

    void startRecording(const fs::path& outputPath = {});
    void stopRecording();
    void selectPreset(const std::string& name);

public slots:
    void onStartRecording(const QString& outputPath);
    void onStopRecording();

protected:
    void closeEvent(QCloseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void changeEvent(QEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private slots:
    void onOpenFiles();
    void onOpenFolder();
    void onSavePlaylist();
    void onLoadPlaylist();
    void onShowSettings();
    void onShowAbout();
    void onUpdateLoop();

private:
    void setupUI();
    void setupConnections();
    void setupUpdateTimer();
    void updateWindowTitle();

    // Engines (Managed by Application)
    AudioEngine* audioEngine_{nullptr};
    OverlayEngine* overlayEngine_{nullptr};
    VideoRecorder* videoRecorder_{nullptr};

    // Domain controllers
    std::unique_ptr<suno::SunoController> sunoController_;

    // Core UI widgets
    VisualizerPanel* visualizerPanel_{nullptr};
    ui::qml::QmlWorkspaceHost* workspaceHost_{nullptr};

    // Dock widgets and layout mode
    QDockWidget* toolsDock_{nullptr};
    bool useSidebarLayout_{true};

    // QML view-model layer
    std::unique_ptr<ui::qml::PlaybackViewModel> playbackViewModel_;
    std::unique_ptr<ui::qml::PlaylistTrackModel> playlistTrackModel_;
    std::unique_ptr<ui::qml::SunoRemoteLibraryViewModel> sunoLibraryViewModel_;
    std::unique_ptr<ui::qml::RecordingViewModel> recordingViewModel_;
    std::unique_ptr<ui::qml::VisualizerViewModel> visualizerViewModel_;

    QTimer updateTimer_;
};

} // namespace vc
