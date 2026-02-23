#include "MainWindow.hpp"

#include <QApplication>
#include <QCloseEvent>
#include <QDockWidget>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QKeyEvent>
#include <QMessageBox>
#include <QMimeData>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QStatusBar>

#include <chrono>
#include <ctime>

#include "MainWindowMenus.hpp"
#include "SettingsDialog.hpp"
#include "VisualizerPanel.hpp"
#include "controllers/SunoController.hpp"
#include "core/Application.hpp"
#include "core/Config.hpp"
#include "core/Logger.hpp"
#include "ui/qml/PlaybackViewModel.hpp"
#include "ui/qml/PlaylistTrackModel.hpp"
#include "ui/qml/QmlWorkspaceHost.hpp"
#include "ui/qml/RecordingViewModel.hpp"
#include "ui/qml/SunoRemoteLibraryViewModel.hpp"
#include "ui/qml/VisualizerViewModel.hpp"
#include "util/FileUtils.hpp"
#include "visualizer/VisualizerWindow.hpp"

namespace vc {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {
    setWindowTitle("ChadVis - Suno Remote Workspace");
    setMinimumSize(1120, 760);
    resize(1540, 920);
    setAcceptDrops(true);

    audioEngine_ = APP->audioEngine();
    overlayEngine_ = APP->overlayEngine();
    videoRecorder_ = APP->videoRecorder();

    if (!audioEngine_ || !overlayEngine_ || !videoRecorder_) {
        LOG_ERROR("MainWindow: One or more engines are null; application bootstrap failed");
    }

    sunoController_ = std::make_unique<suno::SunoController>(audioEngine_, overlayEngine_, this);

    setupUI();
    MainWindowMenus::setupAll(this, this, audioEngine_, visualizerPanel_, toolsDock_, useSidebarLayout_);

    if (visualizerPanel_ && visualizerPanel_->visualizer()) {
        visualizerPanel_->visualizer()->projectM().scanPresets(CONFIG.visualizer().presetPath);
    }

    setupConnections();
    setupUpdateTimer();

    statusBar()->showMessage("Workspace ready for Suno remote playback and capture");
}

MainWindow::~MainWindow() {
    updateTimer_.stop();

    if (videoRecorder_ && videoRecorder_->isRecording()) {
        videoRecorder_->stop();
    }

    visualizerViewModel_.reset();
    recordingViewModel_.reset();
    sunoLibraryViewModel_.reset();
    playlistTrackModel_.reset();
    playbackViewModel_.reset();
    sunoController_.reset();
}

void MainWindow::setupUI() {
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);

    visualizerPanel_ = new VisualizerPanel(this);
    visualizerPanel_->setOverlayEngine(overlayEngine_);
    setCentralWidget(visualizerPanel_);

    playbackViewModel_ = std::make_unique<ui::qml::PlaybackViewModel>(audioEngine_, this);
    playlistTrackModel_ = std::make_unique<ui::qml::PlaylistTrackModel>(audioEngine_, this);
    sunoLibraryViewModel_ =
            std::make_unique<ui::qml::SunoRemoteLibraryViewModel>(sunoController_.get(), this);
    recordingViewModel_ = std::make_unique<ui::qml::RecordingViewModel>(
            videoRecorder_,
            [this] { onStartRecording(QString{}); },
            [this] { onStopRecording(); },
            this);
    visualizerViewModel_ = std::make_unique<ui::qml::VisualizerViewModel>(
            visualizerPanel_->visualizer(),
            this);

    workspaceHost_ = new ui::qml::QmlWorkspaceHost(
            playbackViewModel_.get(),
            playlistTrackModel_.get(),
            sunoLibraryViewModel_.get(),
            recordingViewModel_.get(),
            visualizerViewModel_.get(),
            this);

    toolsDock_ = new QDockWidget("Workspace", this);
    toolsDock_->setObjectName("WorkspaceDock");
    toolsDock_->setMinimumWidth(420);
    toolsDock_->setWidget(workspaceHost_);
    toolsDock_->setFeatures(QDockWidget::DockWidgetMovable |
                            QDockWidget::DockWidgetFloatable |
                            QDockWidget::DockWidgetClosable);
    addDockWidget(Qt::RightDockWidgetArea, toolsDock_);

    setDockOptions(DockOption::AnimatedDocks |
                   DockOption::AllowTabbedDocks |
                   DockOption::AllowNestedDocks);
    resizeDocks({toolsDock_}, {470}, Qt::Horizontal);

    toolsDock_->setVisible(true);
}

void MainWindow::setupConnections() {
    audioEngine_->trackChanged.connect([this] {
        QMetaObject::invokeMethod(this, [this] {
            if (const auto* item = audioEngine_->playlist().currentItem()) {
                overlayEngine_->updateMetadata(item->metadata);
            }
            updateWindowTitle();
        });
    });

    audioEngine_->positionChanged.connect([this](Duration pos) {
        QMetaObject::invokeMethod(this, [this, pos] {
            overlayEngine_->updatePlaybackTime(static_cast<f32>(pos.count()) / 1000.0f);
        });
    });

    audioEngine_->pcmReceived.connect([this](const std::vector<f32>& pcm,
                                             u32 frames,
                                             u32 channels,
                                             u32 sampleRate) {
        if (!pcm.empty() && visualizerPanel_ && visualizerPanel_->visualizer()) {
            visualizerPanel_->visualizer()->feedAudio(
                    pcm.data(), frames, channels, sampleRate);
        }

        if (videoRecorder_ && videoRecorder_->isRecording()) {
            videoRecorder_->submitAudioSamples(
                    pcm.data(), frames, channels, sampleRate);
        }
    });

    connect(visualizerPanel_->visualizer(),
            &VisualizerWindow::frameCaptured,
            this,
            [this](std::vector<u8> data, u32 width, u32 height, i64 timestamp) {
                if (videoRecorder_ && videoRecorder_->isRecording()) {
                    videoRecorder_->submitVideoFrame(
                            std::move(data), width, height, timestamp);
                }
            },
            Qt::DirectConnection);

    videoRecorder_->stateChanged.connect([this](RecordingState) {
        QMetaObject::invokeMethod(this, [this] {
            updateWindowTitle();
        });
    });

    sunoController_->statusMessage.connect([this](const std::string& message) {
        QMetaObject::invokeMethod(this, [this, message] {
            statusBar()->showMessage(QString::fromStdString(message), 5000);
        });
    });
}

void MainWindow::setupUpdateTimer() {
    connect(&updateTimer_, &QTimer::timeout, this, &MainWindow::onUpdateLoop);
    updateTimer_.start(16);
}

void MainWindow::onUpdateLoop() {
    overlayEngine_->update(0.016f);
    const auto& spectrum = audioEngine_->currentSpectrum();
    if (spectrum.beatDetected) {
        overlayEngine_->onBeat(spectrum.beatIntensity);
    }
}

void MainWindow::updateWindowTitle() {
    QString title = QStringLiteral("ChadVis");

    if (const auto* item = audioEngine_->playlist().currentItem()) {
        const auto artist = QString::fromStdString(item->metadata.displayArtist());
        const auto track = QString::fromStdString(item->metadata.displayTitle());
        if (!artist.isEmpty() || !track.isEmpty()) {
            title = artist + " - " + track + " | " + title;
        }
    }

    if (videoRecorder_->isRecording()) {
        title = "[REC] " + title;
    }

    setWindowTitle(title);
}

void MainWindow::addToPlaylist(const fs::path& path) {
    if (fs::is_directory(path)) {
        for (const auto& f : file::listFiles(path, file::audioExtensions, true)) {
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
        const auto now = std::chrono::system_clock::now();
        const auto time = std::chrono::system_clock::to_time_t(now);
        std::tm tm = *std::localtime(&time);

        char buf[64];
        std::strftime(buf, sizeof(buf), "chadvis-projectm-qt_%Y%m%d_%H%M%S", &tm);
        path = CONFIG.recording().outputDirectory /
               (std::string(buf) + EncoderSettings::fromConfig().containerExtension());
    }

    auto settings = EncoderSettings::fromConfig();
    settings.outputPath = path;

    visualizerPanel_->visualizer()->setRecordingSize(settings.video.width, settings.video.height);
    visualizerPanel_->visualizer()->startRecording();

    if (auto result = videoRecorder_->start(settings); !result) {
        QMessageBox::critical(
                this,
                "Recording Error",
                QString::fromStdString(result.error().message));
        visualizerPanel_->visualizer()->stopRecording();
        return;
    }

    updateWindowTitle();
    statusBar()->showMessage("Recording started: " + QString::fromStdString(path.string()));
}

void MainWindow::stopRecording() {
    if (!videoRecorder_->isRecording()) {
        return;
    }

    videoRecorder_->stop();
    visualizerPanel_->visualizer()->stopRecording();

    updateWindowTitle();
    statusBar()->showMessage("Recording stopped");
}

void MainWindow::selectPreset(const std::string& name) {
    if (visualizerPanel_ && visualizerPanel_->visualizer()) {
        visualizerPanel_->visualizer()->projectM().presets().selectByName(name);
        visualizerPanel_->visualizer()->loadPresetFromManager();
    }
}

void MainWindow::onStartRecording(const QString& path) {
    startRecording(path.toStdString());
}

void MainWindow::onStopRecording() {
    stopRecording();
}

void MainWindow::onOpenFiles() {
    QFileDialog dialog(this, "Open Audio Files", QDir::homePath());
    dialog.setNameFilters({"Audio Files (*.mp3 *.flac *.ogg *.opus *.wav *.m4a *.aac)", "All Files (*)"});
    dialog.setFileMode(QFileDialog::ExistingFiles);
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);

    if (!dialog.exec()) {
        return;
    }

    for (const auto& filePath : dialog.selectedFiles()) {
        addToPlaylist(fs::path(filePath.toStdString()));
    }
}

void MainWindow::onOpenFolder() {
    QFileDialog dialog(this, "Open Folder", QDir::homePath());
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);
    dialog.setFileMode(QFileDialog::Directory);

    if (dialog.exec()) {
        addToPlaylist(fs::path(dialog.selectedFiles().first().toStdString()));
    }
}

void MainWindow::onSavePlaylist() {
    const auto path = QFileDialog::getSaveFileName(this,
                                                   "Save Playlist",
                                                   QDir::homePath(),
                                                   "M3U Playlist (*.m3u)",
                                                   nullptr,
                                                   QFileDialog::DontUseNativeDialog);
    if (!path.isEmpty()) {
        audioEngine_->playlist().saveM3U(path.toStdString());
    }
}

void MainWindow::onLoadPlaylist() {
    const auto path = QFileDialog::getOpenFileName(this,
                                                   "Load Playlist",
                                                   QDir::homePath(),
                                                   "M3U Playlist (*.m3u *.m3u8)",
                                                   nullptr,
                                                   QFileDialog::DontUseNativeDialog);
    if (!path.isEmpty()) {
        audioEngine_->playlist().loadM3U(path.toStdString());
    }
}

void MainWindow::onShowSettings() {
    SettingsDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted &&
        visualizerPanel_ &&
        visualizerPanel_->visualizer()) {
        visualizerPanel_->visualizer()->updateSettings();
    }
}

void MainWindow::onShowAbout() {
    QMessageBox::about(this,
                       "About ChadVis",
                       "<h2>ChadVis Audio Player</h2><p>Version 1.1.0</p>"
                       "<p>Built with Qt6, projectM v4, and Suno-focused remote workflows.</p>");
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (videoRecorder_->isRecording()) {
        const auto reply = QMessageBox::question(this,
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

void MainWindow::changeEvent(QEvent* event) {
    if (event->type() == QEvent::ActivationChange &&
        visualizerPanel_ &&
        visualizerPanel_->visualizer()) {
        const bool active = isActiveWindow();
        const int fps = active ? CONFIG.visualizer().fps : 10;
        visualizerPanel_->visualizer()->setRenderRate(fps);
        LOG_DEBUG("MainWindow: Focus changed, render rate now {} FPS", fps);
    }

    QMainWindow::changeEvent(event);
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

    if (paths.isEmpty()) {
        return;
    }

    for (const auto& path : paths) {
        addToPlaylist(fs::path(path.toStdString()));
    }

    statusBar()->showMessage(QString("Added %1 files to playlist").arg(paths.size()));
}

} // namespace vc
