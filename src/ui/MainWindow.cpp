#include "MainWindow.hpp"
#include <QApplication>
#include "OverlayEditor.hpp"
#include "PlayerControls.hpp"
#include "PlaylistView.hpp"
#include "PresetBrowser.hpp"
#include "RecordingControls.hpp"
#include "SettingsDialog.hpp"
#include "VisualizerPanel.hpp"
#include "controllers/AudioController.hpp"
#include "controllers/RecordingController.hpp"
#include "controllers/VisualizerController.hpp"
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
#include <QStatusBar>
#include <QVBoxLayout>

namespace vc {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("ChadVis - I use Arch btw");
    setMinimumSize(1024, 768);
    resize(1400, 900);
    setAcceptDrops(true);

    audioEngine_ = std::make_unique<AudioEngine>();
    if (auto result = audioEngine_->init(); !result) {
        LOG_ERROR("AudioEngine Init Failed: {}", result.error().message);
    }

    overlayEngine_ = std::make_unique<OverlayEngine>();
    overlayEngine_->init();
    videoRecorder_ = std::make_unique<VideoRecorder>();

    // Controllers
    audioController_ =
            std::make_unique<AudioController>(audioEngine_.get(), this);

    setupUI();
    setupMenuBar();

    visualizerController_ = std::make_unique<VisualizerController>(
            &visualizerPanel_->visualizer()->projectM(), this);
    recordingController_ =
            std::make_unique<RecordingController>(videoRecorder_.get(), this);

    setupConnections();
    setupUpdateTimer();

    statusBar()->showMessage("Ready. I use Arch btw.");

    QTimer::singleShot(100, this, [this]() {
        if (visualizerPanel_ && visualizerPanel_->visualizer()) {
            auto& pm = visualizerPanel_->visualizer()->projectM();
            if (pm.isInitialized())
                audioEngine_->setProjectMHandle(pm.getHandle());
        }
    });
}

MainWindow::~MainWindow() {
    updateTimer_.stop();
    if (videoRecorder_->isRecording())
        videoRecorder_->stop();
    audioEngine_->stop();
}

void MainWindow::setupUI() {
    visualizerPanel_ = new VisualizerPanel(this);
    visualizerPanel_->setOverlayEngine(overlayEngine_.get());
    setCentralWidget(visualizerPanel_);

    playerControls_ = new PlayerControls(this);
    auto* controlsDock = new QDockWidget("Player", this);
    controlsDock->setWidget(playerControls_);
    controlsDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    controlsDock->setTitleBarWidget(new QWidget());
    addDockWidget(Qt::BottomDockWidgetArea, controlsDock);

    playlistView_ = new PlaylistView(this);
    playlistDock_ = new QDockWidget("Playlist", this);
    playlistDock_->setObjectName("PlaylistDock");
    playlistDock_->setWidget(playlistView_);
    playlistDock_->setMinimumWidth(250);
    addDockWidget(Qt::LeftDockWidgetArea, playlistDock_);

    auto* rightTabs = new QTabWidget();
    presetBrowser_ = new PresetBrowser();
    rightTabs->addTab(presetBrowser_, "Presets");
    recordingControls_ = new RecordingControls();
    rightTabs->addTab(recordingControls_, "Recording");
    overlayEditor_ = new OverlayEditor();
    overlayEditor_->setOverlayEngine(overlayEngine_.get());
    rightTabs->addTab(overlayEditor_, "Overlay");

    toolsDock_ = new QDockWidget("Tools", this);
    toolsDock_->setObjectName("ToolsDock");
    toolsDock_->setWidget(rightTabs);
    toolsDock_->setMinimumWidth(300);
    addDockWidget(Qt::RightDockWidgetArea, toolsDock_);

    audioController_->setupUI(playerControls_, playlistView_);

    playlistDock_->setVisible(CONFIG.ui().showPlaylist);
    toolsDock_->setVisible(CONFIG.ui().showPresets);
}

void MainWindow::setupMenuBar() {
    auto* fileMenu = menuBar()->addMenu("&File");
    fileMenu->addAction("&Open Files...",
                        this,
                        &MainWindow::onOpenFiles,
                        QKeySequence::Open);
    fileMenu->addAction("Open &Folder...",
                        this,
                        &MainWindow::onOpenFolder,
                        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O));
    fileMenu->addSeparator();
    fileMenu->addAction("&Save Playlist...", this, &MainWindow::onSavePlaylist);
    fileMenu->addAction("&Load Playlist...", this, &MainWindow::onLoadPlaylist);
    fileMenu->addSeparator();
    fileMenu->addAction("E&xit", this, &QMainWindow::close, QKeySequence::Quit);

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
            "&Next",
            this,
            [this] { audioEngine_->playlist().next(); },
            QKeySequence(Qt::Key_N));
    playbackMenu->addAction(
            "&Previous",
            this,
            [this] { audioEngine_->playlist().previous(); },
            QKeySequence(Qt::Key_P));

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

    auto* lockAction = vizMenu->addAction("&Lock Preset");
    lockAction->setCheckable(true);
    connect(lockAction, &QAction::toggled, this, [this](bool locked) {
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

    auto* recordMenu = menuBar()->addMenu("&Recording");
    recordMenu->addAction(
            "&Start Recording",
            this,
            [this] { onStartRecording(""); },
            QKeySequence(Qt::CTRL | Qt::Key_R));
    recordMenu->addAction(
            "S&top Recording", this, &MainWindow::onStopRecording);

    auto* toolsMenu = menuBar()->addMenu("&Tools");
    toolsMenu->addAction("&Settings...",
                         this,
                         &MainWindow::onShowSettings,
                         QKeySequence::Preferences);

    auto* helpMenu = menuBar()->addMenu("&Help");
    helpMenu->addAction("&About ChadVis", this, &MainWindow::onShowAbout);
}

void MainWindow::setupConnections() {
    audioController_->connectSignals();
    visualizerController_->setupUI(visualizerPanel_, presetBrowser_);
    visualizerController_->connectSignals();
    recordingController_->setupUI(recordingControls_);
    recordingController_->connectSignals();

    audioEngine_->trackChanged.connect([this] {
        QMetaObject::invokeMethod(this, [this] {
            if (const auto* item = audioEngine_->playlist().currentItem()) {
                overlayEngine_->updateMetadata(item->metadata);
                updateWindowTitle();
            }
        });
    });

    connect(overlayEditor_, &OverlayEditor::overlayChanged, this, [this] {
        overlayEngine_->config().saveToAppConfig();
    });
}

void MainWindow::setupUpdateTimer() {
    connect(&updateTimer_, &QTimer::timeout, this, &MainWindow::onUpdateLoop);
    updateTimer_.start(16);
}

void MainWindow::onUpdateLoop() {
    overlayEngine_->update(0.016f);
    const auto& spectrum = audioEngine_->currentSpectrum();
    if (spectrum.beatDetected)
        overlayEngine_->onBeat(spectrum.beatIntensity);
}

void MainWindow::updateWindowTitle() {
    QString title = "ChadVis";
    if (const auto* item = audioEngine_->playlist().currentItem()) {
        title = QString::fromStdString(item->metadata.displayArtist()) + " - " +
                QString::fromStdString(item->metadata.displayTitle()) + " | " +
                title;
    }
    if (videoRecorder_->isRecording())
        title = "⏺ " + title;
    setWindowTitle(title);
}

void MainWindow::addToPlaylist(const fs::path& path) {
    if (fs::is_directory(path)) {
        for (const auto& f : file::listFiles(path, file::audioExtensions, true))
            audioEngine_->playlist().addFile(f);
    } else
        audioEngine_->playlist().addFile(path);
}

void MainWindow::addToPlaylist(const std::vector<fs::path>& paths) {
    for (const auto& p : paths)
        addToPlaylist(p);
}

void MainWindow::startRecording(const fs::path& outputPath) {
    fs::path path = outputPath;
    if (path.empty()) {
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
    visualizerPanel_->visualizer()->setRecordingSize(settings.video.width,
                                                     settings.video.height);
    visualizerPanel_->visualizer()->startRecording();

    if (auto result = videoRecorder_->start(settings); !result) {
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
    visualizerPanel_->visualizer()->projectM().presets().selectByName(name);
}
void MainWindow::onStartRecording(const QString& path) {
    startRecording(path.toStdString());
}
void MainWindow::onStopRecording() {
    stopRecording();
}

void MainWindow::onOpenFiles() {
    QFileDialog dialog(this, "Open Audio Files", QDir::homePath());
    dialog.setNameFilters(
            {"Audio Files (*.mp3 *.flac *.ogg *.opus *.wav *.m4a *.aac)",
             "All Files (*)"});
    dialog.setFileMode(QFileDialog::ExistingFiles);
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);
    if (dialog.exec()) {
        for (const auto& f : dialog.selectedFiles())
            addToPlaylist(fs::path(f.toStdString()));
    }
}

void MainWindow::onOpenFolder() {
    QFileDialog dialog(this, "Open Folder", QDir::homePath());
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);
    dialog.setFileMode(QFileDialog::Directory);
    if (dialog.exec())
        addToPlaylist(fs::path(dialog.selectedFiles().first().toStdString()));
}

void MainWindow::onSavePlaylist() {
    QString path =
            QFileDialog::getSaveFileName(this,
                                         "Save Playlist",
                                         QDir::homePath(),
                                         "M3U Playlist (*.m3u)",
                                         nullptr,
                                         QFileDialog::DontUseNativeDialog);
    if (!path.isEmpty())
        audioEngine_->playlist().saveM3U(path.toStdString());
}

void MainWindow::onLoadPlaylist() {
    QString path =
            QFileDialog::getOpenFileName(this,
                                         "Load Playlist",
                                         QDir::homePath(),
                                         "M3U Playlist (*.m3u *.m3u8)",
                                         nullptr,
                                         QFileDialog::DontUseNativeDialog);
    if (!path.isEmpty())
        audioEngine_->playlist().loadM3U(path.toStdString());
}

void MainWindow::onShowSettings() {
    SettingsDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted) {
        if (visualizerPanel_ && visualizerPanel_->visualizer())
            visualizerPanel_->visualizer()->updateSettings();
    }
}

void MainWindow::onShowAbout() {
    QMessageBox::about(
            this,
            "About ChadVis",
            "<h2>ChadVis Audio Player</h2><p>Version 1.1.0</p>"
            "<p>Built with Qt6, projectM v4, and Arch Linux pride.</p><hr>"
            "<p><b>\"I use Arch btw\"</b></p>");
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (videoRecorder_->isRecording()) {
        auto reply =
                QMessageBox::question(this,
                                      "Recording Active",
                                      "Recording in progress. Stop and exit?",
                                      QMessageBox::Yes | QMessageBox::No);
        if (reply == QMessageBox::No) {
            event->ignore();
            return;
        }
        stopRecording();
    }
    CONFIG.save(CONFIG.configPath());
    event->accept();
}

void MainWindow::keyPressEvent(QKeyEvent* event) {
    switch (event->key()) {
    case Qt::Key_Space:
        audioEngine_->togglePlayPause();
        break;
    default:
        QMainWindow::keyPressEvent(event);
    }
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}
void MainWindow::dropEvent(QDropEvent* event) {
    QStringList paths;
    for (const auto& url : event->mimeData()->urls())
        if (url.isLocalFile())
            paths.append(url.toLocalFile());
    if (!paths.isEmpty()) {
        for (const auto& p : paths)
            addToPlaylist(fs::path(p.toStdString()));
        statusBar()->showMessage(
                QString("Added %1 files to playlist").arg(paths.size()));
    }
}

} // namespace vc
