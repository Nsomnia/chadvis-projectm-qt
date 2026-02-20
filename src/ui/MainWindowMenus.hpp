#pragma once
#include <QMainWindow>

namespace vc {

class MainWindow;
class VisualizerPanel;
class QDockWidget;

class MainWindowMenus {
public:
    static void setupFileMenu(QMainWindow* window, MainWindow* mainWindow);
    static void setupPlaybackMenu(QMainWindow* window, class AudioEngine* audioEngine);
    static void setupViewMenu(QMainWindow* window, MainWindow* mainWindow, QDockWidget* toolsDock, bool& useSidebarLayout);
    static void setupVisualizerMenu(QMainWindow* window, VisualizerPanel* visualizerPanel);
    static void setupRecordMenu(QMainWindow* window, MainWindow* mainWindow);
    static void setupToolsMenu(QMainWindow* window, MainWindow* mainWindow);
    static void setupHelpMenu(QMainWindow* window, MainWindow* mainWindow);
    
    static void setupAll(QMainWindow* window, MainWindow* mainWindow, 
                         AudioEngine* audioEngine, VisualizerPanel* visualizerPanel,
                         QDockWidget* toolsDock, bool& useSidebarLayout);
};

} // namespace vc
