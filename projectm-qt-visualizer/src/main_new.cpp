#include <QApplication>
#include <QCommandLineParser>
#include <QDebug>
#include "ProjectMWindow.hpp"

int main(int argc, char *argv[])
{
    // Use desktop OpenGL
    QCoreApplication::setAttribute(Qt::AA_UseDesktopOpenGL);
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts, true);
    
    QApplication app(argc, argv);
    
    QCoreApplication::setApplicationName("projectm-qt-visualizer");
    QCoreApplication::setApplicationVersion("0.1.0");
    
    // Parse command line
    QCommandLineParser parser;
    parser.setApplicationDescription("projectM Visualizer - Chad Edition");
    parser.addHelpOption();
    parser.addVersionOption();
    
    QCommandLineOption audioFileOption("f", "Load audio file", "file");
    parser.addOption(audioFileOption);
    
    QCommandLineOption captureOption("c", "Start with audio capture");
    parser.addOption(captureOption);
    
    parser.process(app);
    
    // Create window
    ProjectMWindow window;
    window.resize(1280, 720);
    window.show();
    
    // Handle command line options
    if (parser.isSet(audioFileOption)) {
        QString file = parser.value(audioFileOption);
        qDebug() << "Loading audio file from command line:" << file;
        window.loadAudioFile(file);
    }
    
    if (parser.isSet(captureOption)) {
        qDebug() << "Starting with audio capture";
        // Will be started after initialization
        QTimer::singleShot(1000, [&window]() {
            window.toggleAudioCapture();
        });
    }
    
    qDebug() << "=== projectM Visualizer Started ===";
    qDebug() << "Controls: Ctrl+A (audio capture), N/P (next/prev preset), F11 (fullscreen), ESC (quit)";
    
    return app.exec();
}
