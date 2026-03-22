#pragma once
#include <QObject>
#include "util/Types.hpp"

namespace vc {

class VideoRecorder;
class RecordingControls;
class MainWindow;

class RecordingController : public QObject {
    Q_OBJECT
public:
    explicit RecordingController(VideoRecorder* recorder, MainWindow* parent);

    void setupUI(RecordingControls* controls);
    void connectSignals();

private:
    VideoRecorder* recorder_;
    MainWindow* window_;
    RecordingControls* controls_{nullptr};
};

} // namespace vc
