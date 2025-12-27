#pragma once
// MainWindow.hpp - The main application window
// Where everything comes together like Voltron

#include "util/Types.hpp"
#include "audio/AudioEngine.hpp"
#include "overlay/OverlayEngine.hpp"
#include "recorder/VideoRecorder.hpp"

#include <QMainWindow>
#include <QTimer>
#include <functional>

namespace vc {

class PlayerControls;
class PlaylistView;
class VisualizerPanel;
class PresetBrowser;
class RecordingControls;
class OverlayEditor;

class MainWindow : public QMainWindow {
    Q_OBJECT
    
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;
    
    // Public interface for Application
    void addToPlaylist(const fs::path& path);
    void addToPlaylist(const std::vector<fs::path>& paths);
    void startRecording(const fs::path& outputPath = {});
    void stopRecording();
    void selectPreset(const std::string& name);
    
protected:
    void closeEvent(QCloseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    
private slots:
    void onPlayClicked();
    void onPauseClicked();
    void onStopClicked();
    void onNextClicked();
    void onPreviousClicked();
    void onSeekRequested(Duration position);
    void onVolumeChanged(f32 volume);
    void onShuffleToggled(bool enabled);
    void onRepeatToggled(RepeatMode mode);
    
    void onPlaylistTrackDoubleClicked(usize index);
    void onFilesDropped(const QStringList& paths);
    
    void onStartRecording(const QString& outputPath);
    void onStopRecording();
    
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
    void executeWithPausedRendering(std::function<void()> action);
    
    // Components
    std::unique_ptr<AudioEngine> audioEngine_;
    std::unique_ptr<OverlayEngine> overlayEngine_;
    std::unique_ptr<VideoRecorder> videoRecorder_;
    
    // UI Widgets
    PlayerControls* playerControls_{nullptr};
    PlaylistView* playlistView_{nullptr};
    VisualizerPanel* visualizerPanel_{nullptr};
    PresetBrowser* presetBrowser_{nullptr};
    RecordingControls* recordingControls_{nullptr};
    OverlayEditor* overlayEditor_{nullptr};
    
    // Dock widgets for flexible layout
    QDockWidget* playlistDock_{nullptr};
    QDockWidget* presetDock_{nullptr};
    QDockWidget* recordDock_{nullptr};
    QDockWidget* overlayDock_{nullptr};
    
    // Update timer
    QTimer updateTimer_;
    
    // State
    bool isFullscreen_{false};
};

} // namespace vc