#include <QApplication>
#include <QSurfaceFormat>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include "gui/MainWindow.hpp"

// Simple file logging
void fileLog(const QString& msg) {
    QFile file("/tmp/projectm-qt-visualizer.log");
    if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        QTextStream out(&file);
        out << QDateTime::currentDateTime().toString("HH:mm:ss.zzz") << " " << msg << "\n";
    }
    qDebug() << msg;
}

int main(int argc, char *argv[])
{
    // Clear old log
    QFile::remove("/tmp/projectm-qt-visualizer.log");
    fileLog("=== Starting application ===");
    
    // Configure OpenGL
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    format.setSwapInterval(0);
    format.setRedBufferSize(8);
    format.setGreenBufferSize(8);
    format.setBlueBufferSize(8);
    format.setAlphaBufferSize(8);
    format.setDepthBufferSize(24);
    QSurfaceFormat::setDefaultFormat(format);
    fileLog("OpenGL format set: 3.3 Core Profile");
    
    QApplication app(argc, argv);
    fileLog("QApplication created");
    
    MainWindow window;
    fileLog("MainWindow created");
    
    window.show();
    fileLog("Window shown");
    
    fileLog("Entering event loop");
    int result = app.exec();
    fileLog("Event loop exited");
    
    return result;
}
