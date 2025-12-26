/**
 * @file MainWindow.cpp
 * @brief Implementation of main application window.
 */
#include "MainWindow.hpp"
#include "VisualizerWidget.hpp"
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QStatusBar>
#include <QFileDialog>
#include <QMessageBox>
#include <QStandardPaths>
#include <QDir>
#include <QDebug>
#include <QFileInfo>

MainWindow::MainWindow(QWidget *parent)
: QMainWindow(parent), m_audioActive(false)
{
    setupUi();
}

MainWindow::~MainWindow() = default;

void MainWindow::setupUi()
{
    setWindowTitle("projectM Visualizer - Chad Edition");
    resize(1280, 720);
    setupMenuBar();
    setupStatusBar();
    setupCentralWidget();
}

void MainWindow::setupMenuBar()
{
    // File menu
    auto* fileMenu = menuBar()->addMenu(tr("&File"));
    
    auto* openAction = fileMenu->addAction(tr("&Open Audio File..."));
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, &MainWindow::onOpenFile);
    
    // Audio toggle for PulseAudio/PipeWire capture
    auto* audioAction = fileMenu->addAction(tr("&Toggle Audio Capture"));
    audioAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_A));
    connect(audioAction, &QAction::triggered, this, &MainWindow::onAudioToggle);
    
    fileMenu->addSeparator();
    
    auto* exitAction = fileMenu->addAction(tr("E&xit"));
    exitAction->setShortcut(QKeySequence::Quit);
    connect(exitAction, &QAction::triggered, this, &QMainWindow::close);
    
    // Help menu
    auto* helpMenu = menuBar()->addMenu(tr("&Help"));
    auto* aboutAction = helpMenu->addAction(tr("&About"));
    connect(aboutAction, &QAction::triggered, this, &MainWindow::onAbout);
}

void MainWindow::setupStatusBar()
{
    statusBar()->showMessage(tr("Ready - I use Arch, BTW | Audio: OFF"));
}

void MainWindow::setupCentralWidget()
{
    m_visualizer = std::make_unique<VisualizerWidget>(this);
    setCentralWidget(m_visualizer.get());
}

void MainWindow::onOpenFile()
{
    // Use native file dialog
    QFileDialog dialog(this);
    dialog.setWindowTitle(tr("Open Audio File"));
    dialog.setDirectory(QStandardPaths::standardLocations(QStandardPaths::MusicLocation).value(0, QDir::homePath()));
    dialog.setFileMode(QFileDialog::ExistingFile);
    dialog.setOption(QFileDialog::DontUseNativeDialog, false);
    dialog.setOption(QFileDialog::DontResolveSymlinks, true);
    
    // Only show audio files
    QStringList filters;
    filters << "Audio Files (*.mp3 *.wav *.flac *.ogg *.m4a *.aac *.opus *.mid)"
            << "All Files (*)";
    dialog.setNameFilters(filters);
    
    if (dialog.exec() == QDialog::Accepted) {
        QString filePath = dialog.selectedFiles().first();
        if (!filePath.isEmpty()) {
            statusBar()->showMessage(tr("Opened: %1").arg(QFileInfo(filePath).fileName()));
            qDebug() << "Selected audio file:" << filePath;
            // TODO: Pass to audio engine for playback
        }
    }
}

void MainWindow::onAudioToggle()
{
    if (!m_visualizer) return;
    
    if (m_audioActive) {
        // Stop audio capture
        m_visualizer->stopAudioCapture();
        m_audioActive = false;
        statusBar()->showMessage(tr("Audio Capture: OFF"));
        qDebug() << "Audio capture disabled";
    } else {
        // Start audio capture
        if (m_visualizer->startAudioCapture()) {
            m_audioActive = true;
            statusBar()->showMessage(tr("Audio Capture: ON - PulseAudio/PipeWire"));
            qDebug() << "Audio capture enabled";
        } else {
            statusBar()->showMessage(tr("Audio Capture: FAILED - check console"));
            qDebug() << "Audio capture failed";
        }
    }
}

void MainWindow::onAbout()
{
    QMessageBox::about(this,
        tr("About projectM Visualizer"),
        tr("<h2>projectM Visualizer</h2>"
           "<p>Version 0.1.0 - Chad Edition</p>"
           "<p>A Qt6-based frontend for projectM v4</p>"
           "<p><i>\"I use Arch, BTW\"</i></p>"
           "<p><b>Features:</b></p>"
           "<ul>"
           "<li>OpenGL visualization via projectM v4</li>"
           "<li>PulseAudio/PipeWire audio capture</li>"
           "<li>Audio file playback support</li>"
           "<li>Single-core ninja builds (potato-safe)</li>"
           "</ul>"
           "<p>Built by a Senior Developer/Project Lead,<br>"
           "not a Junior Developer.</p>")
    );
}
