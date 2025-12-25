/**
 * @file MainWindow.hpp
 * @brief Main application window - container for all UI elements.
 *
 * Single responsibility: Window lifecycle and top-level layout.
 * Does NOT contain visualization logic - that's in VisualizerWidget.
 */
#ifndef MAINWINDOW_HPP
#define MAINWINDOW_HPP
#include <QMainWindow>
#include <memory>
// Forward declarations - minimize header includes
class VisualizerWidget;
class QMenuBar;
class QStatusBar;
class MainWindow : public QMainWindow
{
Q_OBJECT
public:
explicit MainWindow(QWidget *parent = nullptr);
~MainWindow() override;
private slots:
void onOpenFile();
void onAbout();
private:
void setupUi();
void setupMenuBar();
void setupStatusBar();
void setupCentralWidget();
std::unique_ptr<VisualizerWidget> m_visualizer;
};
#endif // MAINWINDOW_HPP
