#include "MainWindowMenus.hpp"
#include "MainWindow.hpp"
#include "VisualizerPanel.hpp"
#include "audio/AudioEngine.hpp"
#include "core/Config.hpp"
#include "visualizer/VisualizerWindow.hpp"

#include <QAction>
#include <QDockWidget>
#include <QKeySequence>
#include <QMenuBar>

namespace vc {

void MainWindowMenus::setupAll(QMainWindow* window, MainWindow* mainWindow,
                               AudioEngine* audioEngine, VisualizerPanel* visualizerPanel,
                               QDockWidget* toolsDock, bool& useSidebarLayout) {
    setupFileMenu(window, mainWindow);
    setupPlaybackMenu(window, audioEngine);
    setupViewMenu(window, mainWindow, toolsDock, useSidebarLayout);
    setupVisualizerMenu(window, visualizerPanel);
    setupRecordMenu(window, mainWindow);
    setupToolsMenu(window, mainWindow);
    setupHelpMenu(window, mainWindow);
}

void MainWindowMenus::setupFileMenu(QMainWindow* window, MainWindow* mainWindow) {
    auto* fileMenu = window->menuBar()->addMenu("&File");
    fileMenu->addAction("&Open Files...", QKeySequence::Open, mainWindow, &MainWindow::onOpenFiles);
    fileMenu->addAction("Open &Folder...", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O), mainWindow, &MainWindow::onOpenFolder);
    fileMenu->addSeparator();
    fileMenu->addAction("&Save Playlist...", mainWindow, &MainWindow::onSavePlaylist);
    fileMenu->addAction("&Load Playlist...", mainWindow, &MainWindow::onLoadPlaylist);
    fileMenu->addSeparator();
    fileMenu->addAction("E&xit", QKeySequence::Quit, window, &QMainWindow::close);
}

void MainWindowMenus::setupPlaybackMenu(QMainWindow* window, AudioEngine* audioEngine) {
    auto* playbackMenu = window->menuBar()->addMenu("&Playback");
    playbackMenu->addAction("&Play/Pause", QKeySequence(Qt::Key_Space), window, [audioEngine] {
        audioEngine->togglePlayPause();
    });
    playbackMenu->addAction("&Stop", QKeySequence(Qt::Key_S), window, [audioEngine] {
        audioEngine->stop();
    });
    playbackMenu->addAction("&Next", QKeySequence(Qt::Key_N), window, [audioEngine] {
        audioEngine->playlist().next();
    });
    playbackMenu->addAction("&Previous", QKeySequence(Qt::Key_P), window, [audioEngine] {
        audioEngine->playlist().previous();
    });
}

void MainWindowMenus::setupViewMenu(QMainWindow* window, MainWindow* mainWindow, 
                                    QDockWidget* toolsDock, bool& useSidebarLayout) {
    auto* viewMenu = window->menuBar()->addMenu("&View");
    viewMenu->addAction("&Fullscreen", QKeySequence::FullScreen, window, [mainWindow] {
        mainWindow->visualizerPanel()->visualizer()->toggleFullscreen();
    });
    viewMenu->addSeparator();

    auto* showToolsAction = viewMenu->addAction("Show &Tools");
    showToolsAction->setCheckable(true);
    showToolsAction->setChecked(toolsDock->isVisible());
    window->connect(showToolsAction, &QAction::toggled, toolsDock, &QDockWidget::setVisible);
    window->connect(toolsDock, &QDockWidget::visibilityChanged, showToolsAction, &QAction::setChecked);
    
    viewMenu->addSeparator();
    auto* useSidebarAction = viewMenu->addAction("Use &Sidebar Layout");
    useSidebarAction->setCheckable(true);
    useSidebarAction->setChecked(useSidebarLayout);
    window->connect(useSidebarAction, &QAction::toggled, window, [toolsDock, &useSidebarLayout](bool useSidebar) {
        useSidebarLayout = useSidebar;
        if (useSidebar) {
            toolsDock->show();
        } else {
            toolsDock->hide();
        }
    });
}

void MainWindowMenus::setupVisualizerMenu(QMainWindow* window, VisualizerPanel* visualizerPanel) {
    auto* vizMenu = window->menuBar()->addMenu("&Visualizer");
    vizMenu->addAction("&Next Preset", QKeySequence(Qt::Key_Right), window, [visualizerPanel] {
        visualizerPanel->visualizer()->nextPreset();
    });
    vizMenu->addAction("&Previous Preset", QKeySequence(Qt::Key_Left), window, [visualizerPanel] {
        visualizerPanel->visualizer()->previousPreset();
    });
    vizMenu->addAction("&Random Preset", QKeySequence(Qt::Key_R), window, [visualizerPanel] {
        visualizerPanel->visualizer()->randomPreset();
    });

    auto* lockAction = vizMenu->addAction("&Lock Preset");
    lockAction->setCheckable(true);
    window->connect(lockAction, &QAction::toggled, window, [visualizerPanel](bool locked) {
        visualizerPanel->visualizer()->lockPreset(locked);
    });

    auto* shuffleAction = vizMenu->addAction("&Shuffle Presets");
    shuffleAction->setCheckable(true);
    shuffleAction->setChecked(CONFIG.visualizer().shufflePresets);
    window->connect(shuffleAction, &QAction::toggled, window, [visualizerPanel](bool enabled) {
        CONFIG.visualizer().shufflePresets = enabled;
        visualizerPanel->visualizer()->updateSettings();
    });

    auto* autoRotateAction = vizMenu->addAction("&Auto-Rotate Presets");
    autoRotateAction->setCheckable(true);
    autoRotateAction->setChecked(CONFIG.visualizer().presetDuration > 0);
    window->connect(autoRotateAction, &QAction::toggled, window, [visualizerPanel](bool enabled) {
        CONFIG.visualizer().presetDuration = enabled ? 30 : 0;
        visualizerPanel->visualizer()->updateSettings();
    });
}

void MainWindowMenus::setupRecordMenu(QMainWindow* window, MainWindow* mainWindow) {
    auto* recordMenu = window->menuBar()->addMenu("&Recording");
    recordMenu->addAction("&Start Recording", QKeySequence(Qt::CTRL | Qt::Key_R), window, [mainWindow] {
        mainWindow->onStartRecording("");
    });
    recordMenu->addAction("S&top Recording", mainWindow, &MainWindow::onStopRecording);
}

void MainWindowMenus::setupToolsMenu(QMainWindow* window, MainWindow* mainWindow) {
    auto* toolsMenu = window->menuBar()->addMenu("&Tools");
    toolsMenu->addAction("&Settings...", QKeySequence::Preferences, mainWindow, &MainWindow::onShowSettings);
}

void MainWindowMenus::setupHelpMenu(QMainWindow* window, MainWindow* mainWindow) {
    auto* helpMenu = window->menuBar()->addMenu("&Help");
    helpMenu->addAction("&About ChadVis", mainWindow, &MainWindow::onShowAbout);
}

} // namespace vc
