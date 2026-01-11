#pragma once
// RecordingControls.hpp - Recording control panel
// Making those sweet YouTube videos

#include "recorder/VideoRecorder.hpp"
#include "util/Types.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QWidget>

namespace vc {

class RecordingControls : public QWidget {
    Q_OBJECT

public:
    explicit RecordingControls(QWidget* parent = nullptr);

    void setVideoRecorder(VideoRecorder* recorder);

signals:
    void startRecordingRequested(const QString& outputPath);
    void stopRecordingRequested();

public slots:
    void updateState(RecordingState state);
    void updateStats(const RecordingStats& stats);

private slots:
    void onRecordButtonClicked();
    void onBrowseOutputClicked();
    void onPresetChanged(int index);

private:
    void setupUI();
    void updateUI();
    QString generateOutputPath();

    VideoRecorder* recorder_{nullptr};

    QPushButton* recordButton_{nullptr};
    QLabel* statusLabel_{nullptr};
    QLabel* timeLabel_{nullptr};
    QLabel* framesLabel_{nullptr};
    QLabel* sizeLabel_{nullptr};

    QComboBox* presetCombo_{nullptr};
    QLineEdit* outputEdit_{nullptr};
    QPushButton* browseButton_{nullptr};

    QCheckBox* restartTrackCheck_{nullptr};
    QCheckBox* stopAtEndCheck_{nullptr};
    QCheckBox* recordEntireSongCheck_{nullptr};

    QProgressBar* bufferBar_{nullptr};

    RecordingState currentState_{RecordingState::Stopped};
};

} // namespace vc
