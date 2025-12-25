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
MainWindow::MainWindow(QWidget *parent)
: QMainWindow(parent)
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
statusBar()->showMessage(tr("Ready - I use Arch, BTW"));
}
void MainWindow::setupCentralWidget()
{
m_visualizer = std::make_unique<VisualizerWidget>(this);
setCentralWidget(m_visualizer.get());
}
void MainWindow::onOpenFile()
{
QString filePath = QFileDialog::getOpenFileName(
this,
tr("Open Audio File"),
QString(),
tr("Audio Files (*.mp3 *.wav *.flac *.ogg *.m4a);;All Files (*)")
);
if (!filePath.isEmpty()) {
statusBar()->showMessage(tr("Opened: %1").arg(filePath));
// TODO: Pass to audio engine
// m_audioEngine->loadFile(filePath);
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
"<p>Built by a Senior Developer/Project Lead,<br>"
"not a Junior Developer.</p>")
);
}
