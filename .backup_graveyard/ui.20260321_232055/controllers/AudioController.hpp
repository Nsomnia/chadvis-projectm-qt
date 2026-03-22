#pragma once
#include <QObject>
#include "util/Types.hpp"

namespace vc {

class AudioEngine;
class PlayerControls;
class PlaylistView;
class MainWindow;

class AudioController : public QObject {
    Q_OBJECT
public:
    explicit AudioController(AudioEngine* engine, MainWindow* parent);

    void setupUI(PlayerControls* controls, PlaylistView* view);
    void connectSignals();

private:
    AudioEngine* engine_;
    MainWindow* window_;
    PlayerControls* controls_{nullptr};
    PlaylistView* view_{nullptr};
};

} // namespace vc
