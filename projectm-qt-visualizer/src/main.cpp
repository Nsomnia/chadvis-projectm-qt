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
// projectM v4 needs OpenGL 3.3+
QSurfaceFormat format;
format.setVersion(3, 3);
format.setProfile(QSurfaceFormat::CoreProfile);
format.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
format.setSwapInterval(1); // VSync
QSurfaceFormat::setDefaultFormat(format);
QApplication app(argc, argv);
app.setApplicationName("projectm-qt-visualizer");
app.setApplicationVersion("0.1.0");
app.setOrganizationName("ChadsVisualizerBTW");
// Initialize logging
// TODO: Logger::init("logs/visualizer.log");
qDebug() << "Starting projectm-qt-visualizer";
qDebug() << "I use Arch, BTW.";
MainWindow window;
window.show();
return app.exec();
}
