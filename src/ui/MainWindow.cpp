#include "MainWindow.hpp"
#include <QApplication>
#include "OverlayEditor.hpp"
#include "PlayerControls.hpp"
#include "PlaylistView.hpp"
#include "PresetBrowser.hpp"
#include "RecordingControls.hpp"
#include "SettingsDialog.hpp"
#include "VisualizerPanel.hpp"
#include "core/Config.hpp"
#include "core/Logger.hpp"
#include "util/FileUtils.hpp"
#include "visualizer/VisualizerWindow.hpp"

#include <QCloseEvent>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QSplitter>
#include <QStatusBar>
#include <QVBoxLayout>

// RAII Guard to pause visualizer rendering during modal dialogs
namespace vc_render_guard {
class RenderGuard {
public:
    explicit RenderGuard(vc::VisualizerWindow* viz) : viz_(viz) {
        if (viz_) {
            wasVisible_ = viz_->isVisible();
            if (wasVisible_) {
                viz_->setVisible(false);
                QApplication::processEvents();
            }
        }
    }
    ~RenderGuard() {
        if (viz_ && wasVisible_) {
            viz_->setVisible(true);
        }
    }
    RenderGuard(const RenderGuard&) = delete;
    RenderGuard& operator=(const RenderGuard&) = delete;

private:
    vc::VisualizerWindow* viz_;
    bool wasVisible_{false};
};
} // namespace vc_render_guard
using vc_render_guard::RenderGuard;

namespace vc {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("ChadVis - I use Arch btw");
    setMinimumSize(1024, 768);
    resize(1400, 900);
    setAcceptDrops(true);

    // Initialize components
    audioEngine_ = std::make_unique<AudioEngine>();
    if (auto result = audioEngine_->init(); !result) {
        LOG_ERROR("Failed to init audio engine: {}", result.error().message);
    }

    overlayEngine_ = std::make_unique<OverlayEngine>();
    overlayEngine_->init();

    videoRecorder_ = std::make_unique<VideoRecorder>();

    setupUI();
    setupMenuBar();
    setupConnections();
    setupUpdateTimer();

    // Status bar
    statusBar()->showMessage(
            "Ready. Drag and drop some music files to get started.");

    LOG_INFO("MainWindow initialized");

    // Set projectM handle on audio engine after visualizer is initialized
    // VisualizerWindow initializes lazily on first expose, so we need to handle
    // this
    QTimer::singleShot(100, this, [this]() {
        if (visualizerPanel_ && visualizerPanel_->visualizer()) {
            auto& projectM = visualizerPanel_->visualizer()->projectM();
            if (projectM.isInitialized()) {
                audioEngine_->setProjectMHandle(projectM.getHandle());
                LOG_INFO("Set projectM handle on audio engine (delayed)");
            }
        }
    });
}

MainWindow::~MainWindow() {
    updateTimer_.stop();

    if (videoRecorder_->isRecording()) {
        videoRecorder_->stop();
    }

    audioEngine_->stop();
}

void MainWindow::setupUI() {
    // Central widget: Visualizer panel
    visualizerPanel_ = new VisualizerPanel(this);
    visualizerPanel_->setOverlayEngine(overlayEngine_.get());
    setCentralWidget(visualizerPanel_);

    // Set preset manager for visualizer
    auto& presetMgr = visualizerPanel_->visualizer()->projectM().presets();

    // Bottom: Player controls
    playerControls_ = new PlayerControls(this);
    playerControls_->setAudioEngine(audioEngine_.get());

    auto* controlsDock = new QDockWidget("Player", this);
    controlsDock->setWidget(playerControls_);
    controlsDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    controlsDock->setTitleBarWidget(new QWidget()); // Hide title bar
    addDockWidget(Qt::BottomDockWidgetArea, controlsDock);

    // Left: Playlist
    playlistView_ = new PlaylistView(this);
    playlistView_->setPlaylist(&audioEngine_->playlist());

    playlistDock_ = new QDockWidget("Playlist", this);
    playlistDock_->setWidget(playlistView_);
    playlistDock_->setMinimumWidth(250);
    addDockWidget(Qt::LeftDockWidgetArea, playlistDock_);

    // Right: Tabbed panel for presets, recording, overlay
    auto* rightTabs = new QTabWidget();

    presetBrowser_ = new PresetBrowser();
    presetBrowser_->setPresetManager(&presetMgr);
    rightTabs->addTab(presetBrowser_, "Presets");

    recordingControls_ = new RecordingControls();
    recordingControls_->setVideoRecorder(videoRecorder_.get());
    rightTabs->addTab(recordingControls_, "Recording");

    overlayEditor_ = new OverlayEditor();
    overlayEditor_->setOverlayEngine(overlayEngine_.get());
    rightTabs->addTab(overlayEditor_, "Overlay");

    toolsDock_ = new QDockWidget("Tools", this);
    toolsDock_->setObjectName("ToolsDock");
    toolsDock_->setWidget(rightTabs);
    toolsDock_->setMinimumWidth(300);
    addDockWidget(Qt::RightDockWidgetArea, toolsDock_);

    // Show/hide based on config
    playlistDock_->setVisible(CONFIG.ui().showPlaylist);
    toolsDock_->setVisible(CONFIG.ui().showPresets);
}

void MainWindow::setupMenuBar() {
    auto* fileMenu = menuBar()->addMenu("&File");

    auto* openFilesAction = fileMenu->addAction("&Open Files...",
                                                this,
                                                &MainWindow::onOpenFiles,
                                                QKeySequence::Open);
    Q_UNUSED(openFilesAction);

    auto* openFolderAction =
            fileMenu->addAction("Open &Folder...",
                                this,
                                &MainWindow::onOpenFolder,
                                QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O));
    Q_UNUSED(openFolderAction);

    fileMenu->addSeparator();

    fileMenu->addAction("&Save Playlist...", this, &MainWindow::onSavePlaylist);
    fileMenu->addAction("&Load Playlist...", this, &MainWindow::onLoadPlaylist);

    fileMenu->addSeparator();

    fileMenu->addAction("E&xit", this, &QMainWindow::close, QKeySequence::Quit);

    // Playback menu
    auto* playbackMenu = menuBar()->addMenu("&Playback");

    playbackMenu->addAction(
            "&Play/Pause",
            this,
            [this] { audioEngine_->togglePlayPause(); },
            QKeySequence(Qt::Key_Space));

    playbackMenu->addAction(
            "&Stop",
            this,
            [this] { audioEngine_->stop(); },
            QKeySequence(Qt::Key_S));

    playbackMenu->addAction(
            "&Next", this, &MainWindow::onNextClicked, QKeySequence(Qt::Key_N));

    playbackMenu->addAction("&Previous",
                            this,
                            &MainWindow::onPreviousClicked,
                            QKeySequence(Qt::Key_P));

    // View menu
    auto* viewMenu = menuBar()->addMenu("&View");

    viewMenu->addAction(
            "&Fullscreen",
            this,
            [this] { visualizerPanel_->visualizer()->toggleFullscreen(); },
            QKeySequence::FullScreen);

    viewMenu->addSeparator();

    auto* showPlaylistAction = viewMenu->addAction("Show &Playlist");
    showPlaylistAction->setCheckable(true);
    showPlaylistAction->setChecked(playlistDock_->isVisible());
    connect(showPlaylistAction,
            &QAction::toggled,
            playlistDock_,
            &QDockWidget::setVisible);
    connect(playlistDock_,
            &QDockWidget::visibilityChanged,
            showPlaylistAction,
            &QAction::setChecked);

    auto* showToolsAction = viewMenu->addAction("Show &Tools");
    showToolsAction->setCheckable(true);
    showToolsAction->setChecked(toolsDock_->isVisible());
    connect(showToolsAction,
            &QAction::toggled,
            toolsDock_,
            &QDockWidget::setVisible);
    connect(toolsDock_,
            &QDockWidget::visibilityChanged,
            showToolsAction,
            &QAction::setChecked);

    // Visualizer menu
    auto* vizMenu = menuBar()->addMenu("&Visualizer");

    vizMenu->addAction(
            "&Next Preset",
            this,
            [this] { visualizerPanel_->visualizer()->projectM().nextPreset(); },
            QKeySequence(Qt::Key_Right));

    vizMenu->addAction(
            "&Previous Preset",
            this,
            [this] {
                visualizerPanel_->visualizer()->projectM().previousPreset();
            },
            QKeySequence(Qt::Key_Left));

    vizMenu->addAction(
            "&Random Preset",
            this,
            [this] {
                visualizerPanel_->visualizer()->projectM().randomPreset();
            },
            QKeySequence(Qt::Key_R));

    auto* lockPresetAction = vizMenu->addAction("&Lock Preset");
    lockPresetAction->setCheckable(true);
    connect(lockPresetAction, &QAction::toggled, this, [this](bool locked) {
        visualizerPanel_->visualizer()->projectM().lockPreset(locked);
    });

    auto* shuffleAction = vizMenu->addAction("&Shuffle Presets");
    shuffleAction->setCheckable(true);
    shuffleAction->setChecked(CONFIG.visualizer().shufflePresets);
    connect(shuffleAction, &QAction::toggled, this, [this](bool enabled) {
        CONFIG.visualizer().shufflePresets = enabled;
        visualizerPanel_->visualizer()->updateSettings();
    });

    auto* autoRotateAction = vizMenu->addAction("&Auto-Rotate Presets");
    autoRotateAction->setCheckable(true);
    autoRotateAction->setChecked(CONFIG.visualizer().presetDuration > 0);
    connect(autoRotateAction, &QAction::toggled, this, [this](bool enabled) {
        CONFIG.visualizer().presetDuration = enabled ? 30 : 0;
        visualizerPanel_->visualizer()->updateSettings();
    });

    // Recording menu
    auto* recordMenu = menuBar()->addMenu("&Recording");

    auto* startRecAction = recordMenu->addAction(
            "&Start Recording",
            this,
            [this] { onStartRecording(""); },
            QKeySequence(Qt::CTRL | Qt::Key_R));
    Q_UNUSED(startRecAction);

    recordMenu->addAction(
            "S&top Recording", this, &MainWindow::onStopRecording);

    // Tools menu
    auto* toolsMenu = menuBar()->addMenu("&Tools");

    toolsMenu->addAction("&Settings...",
                         this,
                         &MainWindow::onShowSettings,
                         QKeySequence::Preferences);

    // Help menu
    auto* helpMenu = menuBar()->addMenu("&Help");

    helpMenu->addAction("&About ChadVis", this, &MainWindow::onShowAbout);
    helpMenu->addAction("About &Qt", qApp, &QApplication::aboutQt);
}

void MainWindow::setupConnections() {
    // Player controls signals
    connect(playerControls_,
            &PlayerControls::playClicked,
            this,
            &MainWindow::onPlayClicked);
    connect(playerControls_,
            &PlayerControls::pauseClicked,
            this,
            &MainWindow::onPauseClicked);
    connect(playerControls_,
            &PlayerControls::stopClicked,
            this,
            &MainWindow::onStopClicked);
    connect(playerControls_,
            &PlayerControls::nextClicked,
            this,
            &MainWindow::onNextClicked);
    connect(playerControls_,
            &PlayerControls::previousClicked,
            this,
            &MainWindow::onPreviousClicked);
    connect(playerControls_,
            &PlayerControls::seekRequested,
            this,
            &MainWindow::onSeekRequested);
    connect(playerControls_,
            &PlayerControls::volumeChanged,
            this,
            &MainWindow::onVolumeChanged);
    connect(playerControls_,
            &PlayerControls::shuffleToggled,
            this,
            &MainWindow::onShuffleToggled);
    connect(playerControls_,
            &PlayerControls::repeatToggled,
            this,
            &MainWindow::onRepeatToggled);

    // Playlist signals
    connect(playlistView_,
            &PlaylistView::trackDoubleClicked,
            this,
            &MainWindow::onPlaylistTrackDoubleClicked);
    connect(playlistView_,
            &PlaylistView::filesDropped,
            this,
            &MainWindow::onFilesDropped);

    // Recording signals
    connect(recordingControls_,
            &RecordingControls::startRecordingRequested,
            this,
            &MainWindow::onStartRecording);
    connect(recordingControls_,
            &RecordingControls::stopRecordingRequested,
            this,
            &MainWindow::onStopRecording);

    // Audio engine track change -> update overlay metadata
    audioEngine_->trackChanged.connect([this] {
        QMetaObject::invokeMethod(this, [this] {
            if (const auto* item = audioEngine_->playlist().currentItem()) {
                overlayEngine_->updateMetadata(item->metadata);
                updateWindowTitle();
            }
        });
    });

    // Audio engine PCM data -> feed to visualizer
    audioEngine_->pcmReceived.connect([this](const std::vector<f32>& pcm,
                                             u32 frames,
                                             u32 channels,
                                             u32 sampleRate) {
        LOG_DEBUG("MainWindow: Received {} frames from audio engine", frames);
        if (!pcm.empty() && frames > 0) {
            visualizerPanel_->visualizer()->feedAudio(
                    pcm.data(), frames, channels, sampleRate);

            if (videoRecorder_->isRecording()) {
                videoRecorder_->submitAudioSamples(
                        pcm.data(), frames, channels, sampleRate);
            }
        }
    });

    // Overlay editor changes
    connect(overlayEditor_, &OverlayEditor::overlayChanged, this, [this] {
        overlayEngine_->config().saveToAppConfig();
    });

    // Visualizer frame ready -> feed to recorder
    connect(visualizerPanel_->visualizer(),
            &VisualizerWindow::frameCaptured,
            this,
            [this](const u8* data, u32 width, u32 height, i64 timestamp) {
                if (videoRecorder_->isRecording()) {
                    videoRecorder_->submitVideoFrame(
                            data, width, height, timestamp);
                }
            });

    // Preset browser selection -> load preset
    // NOTE: PresetBrowser already calls presetManager_->selectByPath() which
    // triggers the signal chain (PresetManager -> ProjectMBridge ->
    // projectM_load_preset_file). The presetSelected signal is emitted but we
    // don't need to connect to it here because the signal chain handles
    // everything. connect(presetBrowser_, &PresetBrowser::presetSelected, ...);
}

void MainWindow::setupUpdateTimer() {
    connect(&updateTimer_, &QTimer::timeout, this, &MainWindow::onUpdateLoop);
    updateTimer_.start(16); // ~60 fps
}

void MainWindow::onUpdateLoop() {
    // Update overlay animations
    overlayEngine_->update(0.016f);

    // Check for beat and notify overlay
    const auto& spectrum = audioEngine_->currentSpectrum();
    if (spectrum.beatDetected) {
        overlayEngine_->onBeat(spectrum.beatIntensity);
    }
}

void MainWindow::updateWindowTitle() {
    QString title = "ChadVis";

    if (const auto* item = audioEngine_->playlist().currentItem()) {
        title = QString::fromStdString(item->metadata.displayArtist()) + " - " +
                QString::fromStdString(item->metadata.displayTitle()) + " | " +
                title;
    }

    if (videoRecorder_->isRecording()) {
        title = "⏺ " + title;
    }

    setWindowTitle(title);
}

void MainWindow::addToPlaylist(const fs::path& path) {
    if (fs::is_directory(path)) {
        auto files = file::listFiles(path, file::audioExtensions, true);
        for (const auto& f : files) {
            audioEngine_->playlist().addFile(f);
        }
    } else {
        audioEngine_->playlist().addFile(path);
    }
}

void MainWindow::addToPlaylist(const std::vector<fs::path>& paths) {
    for (const auto& p : paths) {
        addToPlaylist(p);
    }
}

void MainWindow::startRecording(const fs::path& outputPath) {
    fs::path path = outputPath;

    if (path.empty()) {
        // Generate default path
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::tm tm = *std::localtime(&time);

        char buf[64];
        std::strftime(
                buf, sizeof(buf), "chadvis-projectm-qt_%Y%m%d_%H%M%S", &tm);

        path = CONFIG.recording().outputDirectory /
               (std::string(buf) +
                EncoderSettings::fromConfig().containerExtension());
    }

    auto settings = EncoderSettings::fromConfig();
    settings.outputPath = path;

    // Set recording size on visualizer
    visualizerPanel_->visualizer()->setRecordingSize(settings.video.width,
                                                     settings.video.height);
    visualizerPanel_->visualizer()->startRecording();

    if (auto result = videoRecorder_->start(settings); !result) {
        LOG_ERROR("Failed to start recording: {}", result.error().message);
        QMessageBox::critical(this,
                              "Recording Error",
                              QString::fromStdString(result.error().message));
        visualizerPanel_->visualizer()->stopRecording();
    } else {
        updateWindowTitle();
        statusBar()->showMessage("Recording started: " +
                                 QString::fromStdString(path.string()));
    }
}

void MainWindow::stopRecording() {
    if (videoRecorder_->isRecording()) {
        videoRecorder_->stop();
        visualizerPanel_->visualizer()->stopRecording();
        updateWindowTitle();
        statusBar()->showMessage("Recording stopped");
    }
}

void MainWindow::selectPreset(const std::string& name) {
    LOG_INFO("MainWindow::selectPreset called with '{}'", name);
    visualizerPanel_->visualizer()->projectM().presets().selectByName(name);
}

// Slots

void MainWindow::onPlayClicked() {
    audioEngine_->play();
}

void MainWindow::onPauseClicked() {
    audioEngine_->pause();
}

void MainWindow::onStopClicked() {
    audioEngine_->stop();
}

void MainWindow::onNextClicked() {
    audioEngine_->playlist().next();
}

void MainWindow::onPreviousClicked() {
    audioEngine_->playlist().previous();
}

void MainWindow::onSeekRequested(Duration position) {
    audioEngine_->seek(position);
}

void MainWindow::onVolumeChanged(f32 volume) {
    audioEngine_->setVolume(volume);
}

void MainWindow::onShuffleToggled(bool enabled) {
    audioEngine_->playlist().setShuffle(enabled);
}

void MainWindow::onRepeatToggled(RepeatMode mode) {
    audioEngine_->playlist().setRepeatMode(mode);
}

void MainWindow::onPlaylistTrackDoubleClicked(usize index) {
    audioEngine_->playlist().jumpTo(index);
}

void MainWindow::onFilesDropped(const QStringList& paths) {
    for (const auto& p : paths) {
        addToPlaylist(fs::path(p.toStdString()));
    }
    statusBar()->showMessage(
            QString("Added %1 files to playlist").arg(paths.size()));
}

void MainWindow::onStartRecording(const QString& outputPath) {
    fs::path path;
    if (!outputPath.isEmpty()) {
        path = outputPath.toStdString();
    }
    startRecording(path);
}

void MainWindow::onStopRecording() {
    stopRecording();
}

void MainWindow::onOpenFiles() {
    // Guard to pause visualizer during dialog (prevents Wayland/Hyprland hang)
    RenderGuard guard(visualizerPanel_->visualizer());

    QFileDialog dialog(this, "Open Audio Files", QDir::homePath());
    dialog.setNameFilters(
            {"Audio Files (*.mp3 *.flac *.ogg *.opus *.wav *.m4a *.aac)",
             "All Files (*)"});
    dialog.setFileMode(QFileDialog::ExistingFiles);

    // CRITICAL: Don't use native portal on Wayland (causes hangs)
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);

    if (dialog.exec()) {
        QStringList files = dialog.selectedFiles();
        for (const auto& f : files) {
            addToPlaylist(fs::path(f.toStdString()));
        }
    }
}

void MainWindow::onOpenFolder() {
    // Guard to pause visualizer during dialog (prevents Wayland/Hyprland hang)
    RenderGuard guard(visualizerPanel_->visualizer());

    QFileDialog dialog(this, "Open Folder", QDir::homePath());
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);

    if (dialog.exec()) {
        QString dir = dialog.selectedFiles().first();
        if (!dir.isEmpty()) {
            addToPlaylist(fs::path(dir.toStdString()));
        }
    }
}

void MainWindow::onSavePlaylist() {
    // Guard to pause visualizer during dialog (prevents Wayland/Hyprland hang)
    RenderGuard guard(visualizerPanel_->visualizer());

    QFileDialog dialog(this, "Save Playlist", QDir::homePath());
    dialog.setNameFilter("M3U Playlist (*.m3u)");
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);

    if (dialog.exec()) {
        QString path = dialog.selectedFiles().first();
        if (auto result = audioEngine_->playlist().saveM3U(path.toStdString());
            !result) {
            QMessageBox::warning(
                    this,
                    "Error",
                    QString::fromStdString(result.error().message));
        }
    }
}

void MainWindow::onLoadPlaylist() {
    // Guard to pause visualizer during dialog (prevents Wayland/Hyprland hang)
    RenderGuard guard(visualizerPanel_->visualizer());

    QFileDialog dialog(this, "Load Playlist", QDir::homePath());
    dialog.setNameFilter("M3U Playlist (*.m3u *.m3u8)");
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);

    if (dialog.exec()) {
        QString path = dialog.selectedFiles().first();
        if (auto result = audioEngine_->playlist().loadM3U(path.toStdString());
            !result) {
            QMessageBox::warning(
                    this,
                    "Error",
                    QString::fromStdString(result.error().message));
        }
    }
}

void MainWindow::onShowSettings() {
    SettingsDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted) {
        if (visualizerPanel_ && visualizerPanel_->visualizer()) {
            visualizerPanel_->visualizer()->updateSettings();
        }
        LOG_INFO("Settings applied and visualizer updated");
    }
}

void MainWindow::onShowAbout() {
    QMessageBox::about(
            this,
            "About ChadVis",
            "<h2>ChadVis Audio Player</h2>"
            "<p>Version 1.0.0</p>"
            "<p>A brutally customizable audio visualizer for Arch Linux.</p>"
            "<p>Built with Qt6, ProjectM, and the tears of junior "
            "developers.</p>"
            "<hr>"
            "<p><b>\"I use Arch btw\"</b></p>"
            "<p>© 2024 The ChadVis Collective</p>"
            "<p>Licensed under the MIT License</p>");
}

void MainWindow::closeEvent(QCloseEvent* event) {
    // Stop recording if active
    if (videoRecorder_->isRecording()) {
        auto reply = QMessageBox::question(
                this,
                "Recording Active",
                "Recording is in progress. Stop and exit?",
                QMessageBox::Yes | QMessageBox::No);

        if (reply == QMessageBox::No) {
            event->ignore();
            return;
        }

        stopRecording();
    }

    // Save config
    CONFIG.save(CONFIG.configPath());

    event->accept();
}

void MainWindow::keyPressEvent(QKeyEvent* event) {
    // Global keyboard shortcuts
    switch (event->key()) {
    case Qt::Key_Space:
        audioEngine_->togglePlayPause();
        break;
    case Qt::Key_Escape:
        if (isFullscreen_) {
            visualizerPanel_->visualizer()->toggleFullscreen();
            isFullscreen_ = false;
        }
        break;
    default:
        QMainWindow::keyPressEvent(event);
    }
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent* event) {
    QStringList paths;
    for (const auto& url : event->mimeData()->urls()) {
        if (url.isLocalFile()) {
            paths.append(url.toLocalFile());
        }
    }

    if (!paths.isEmpty()) {
        onFilesDropped(paths);
    }
}

} // namespace vc

#include "moc_MainWindow.cpp"
