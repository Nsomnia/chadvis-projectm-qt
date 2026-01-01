#pragma once
#include <QObject>
#include "util/Types.hpp"

namespace vc {

class ProjectMBridge;
class VisualizerPanel;
class PresetBrowser;
class MainWindow;

class VisualizerController : public QObject {
    Q_OBJECT
public:
    explicit VisualizerController(ProjectMBridge* bridge, MainWindow* parent);

    void setupUI(VisualizerPanel* panel, PresetBrowser* browser);
    void connectSignals();

private:
    ProjectMBridge* bridge_;
    MainWindow* window_;
    VisualizerPanel* panel_{nullptr};
    PresetBrowser* browser_{nullptr};
};

} // namespace vc
