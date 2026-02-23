#pragma once

#include <QWidget>

class QQuickWidget;

namespace vc::ui::qml {

class PlaybackViewModel;
class PlaylistTrackModel;
class SunoRemoteLibraryViewModel;
class RecordingViewModel;
class VisualizerViewModel;

class QmlWorkspaceHost : public QWidget {
    Q_OBJECT

public:
    QmlWorkspaceHost(PlaybackViewModel* playbackViewModel,
                     PlaylistTrackModel* playlistTrackModel,
                     SunoRemoteLibraryViewModel* sunoViewModel,
                     RecordingViewModel* recordingViewModel,
                     VisualizerViewModel* visualizerViewModel,
                     QWidget* parent = nullptr);

private:
    void setupUi();

    PlaybackViewModel* playbackViewModel_{nullptr};
    PlaylistTrackModel* playlistTrackModel_{nullptr};
    SunoRemoteLibraryViewModel* sunoViewModel_{nullptr};
    RecordingViewModel* recordingViewModel_{nullptr};
    VisualizerViewModel* visualizerViewModel_{nullptr};

    QQuickWidget* quickWidget_{nullptr};
};

} // namespace vc::ui::qml
