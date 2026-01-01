#pragma once
// MainWindow.hpp - The main application window
// Now refactored to use specialized controllers for logic.

#include "audio/AudioEngine.hpp"
#include "overlay/OverlayEngine.hpp"
#include "recorder/VideoRecorder.hpp"
#include "util/Types.hpp"

#include <QMainWindow>
#include <QTimer>
#include <functional>
#include <memory>

namespace vc {

class PlayerControls;
class PlaylistView;
class VisualizerPanel;
class PresetBrowser;
class RecordingControls;
class OverlayEditor;

class AudioController;
class VisualizerController;
class RecordingController;

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
        return audioEngine_.get();
    }
    VisualizerPanel* visualizerPanel() {
        return visualizerPanel_;
    }
    VideoRecorder* videoRecorder() {
        return videoRecorder_.get();
    }
    OverlayEngine* overlayEngine() {
        return overlayEngine_.get();
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
    void setupMenuBar();
    void setupConnections();
    void setupUpdateTimer();
    void updateWindowTitle();

    // Engines
    std::unique_ptr<AudioEngine> audioEngine_;
    std::unique_ptr<OverlayEngine> overlayEngine_;
    std::unique_ptr<VideoRecorder> videoRecorder_;

    // Controllers
    std::unique_ptr<AudioController> audioController_;
    std::unique_ptr<VisualizerController> visualizerController_;
    std::unique_ptr<RecordingController> recordingController_;

    // UI Widgets
    PlayerControls* playerControls_{nullptr};
    PlaylistView* playlistView_{nullptr};
    VisualizerPanel* visualizerPanel_{nullptr};
    PresetBrowser* presetBrowser_{nullptr};
    RecordingControls* recordingControls_{nullptr};
    OverlayEditor* overlayEditor_{nullptr};

    // Dock widgets
    QDockWidget* playlistDock_{nullptr};
    QDockWidget* toolsDock_{nullptr};

    QTimer updateTimer_;
    bool isFullscreen_{false};
};

} // namespace vc
