/**
* projectm-qt-visualizer
* Main entry point
*
* "I use Arch, BTW" - This is a Chad developer's audio visualizer.
*
* @file main.cpp
* @brief Application entry point - initializes Qt and launches main window.
*
* AGENT NOTE: This is THE entry point. Keep it minimal.
* All logic should be in Application class.
*/
#include <QApplication>
#include <QSurfaceFormat>
#include "core/utils/Logger.hpp"
#include "gui/MainWindow.hpp"

int main(int argc, char *argv[])
{
    // Configure OpenGL BEFORE QApplication
    // projectM v4 requires OpenGL 3.3 Core Profile or ES 2.0+
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    format.setSwapInterval(0); // Disable VSync for max FPS (can be enabled later)
    
    // Request sufficient color buffer depth
    format.setRedBufferSize(8);
    format.setGreenBufferSize(8);
    format.setBlueBufferSize(8);
    format.setAlphaBufferSize(8);
    format.setDepthBufferSize(24);
    
    QSurfaceFormat::setDefaultFormat(format);
    
    QApplication app(argc, argv);
    app.setApplicationName("projectm-qt-visualizer");
    app.setApplicationVersion("0.1.0");
    app.setOrganizationName("ChadsVisualizerBTW");
    
    // Initialize logging
    // TODO: Logger::init("logs/visualizer.log");
    qDebug() << "=== Starting projectm-qt-visualizer ===";
    qDebug() << "I use Arch, BTW.";
    qDebug() << "Qt version:" << QT_VERSION_STR;
    
    MainWindow window;
    window.show();
    
    int result = app.exec();
    
    qDebug() << "Application exited with code:" << result;
    return result;
}
