#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>
#include <QWidget>

#include "RecordingControls.hpp"
#include "core/Config.hpp"
#include "core/Logger.hpp"
#include "ui/MainWindow.hpp"
#include "ui/VisualizerPanel.hpp"
#include "util/FileUtils.hpp"
#include "visualizer/VisualizerWindow.hpp"

namespace vc {

RecordingControls::RecordingControls(QWidget* parent) : QWidget(parent) {
    setupUI();
}

void RecordingControls::setVideoRecorder(VideoRecorder* recorder) {
    recorder_ = recorder;

    if (recorder) {
        recorder->stateChanged.connect([this](RecordingState s) {
            QMetaObject::invokeMethod(this, [this, s] { updateState(s); });
        });

        recorder->statsUpdated.connect([this](const RecordingStats& stats) {
            QMetaObject::invokeMethod(this,
                                      [this, stats] { updateStats(stats); });
        });
        
        // Also connect error signal
        recorder->error.connect([this](const std::string& error) {
            QMetaObject::invokeMethod(this, [this, error] {
                LOG_WARN("Recording error: {}", error);
                statusLabel_->setText(QString("Error: %1").arg(QString::fromStdString(error)));
                statusLabel_->setStyleSheet("color: #ff0000;");
            });
        });
    }
}

void RecordingControls::setupUI() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(8);

    // Recording controls group
    auto* recordGroup = new QGroupBox("Recording");
    auto* recordLayout = new QVBoxLayout(recordGroup);

    // Preset selection
    auto* presetLayout = new QHBoxLayout();
    presetLayout->addWidget(new QLabel("Quality:"));

    presetCombo_ = new QComboBox();
    for (const auto& preset : getQualityPresets()) {
        presetCombo_->addItem(QString::fromStdString(preset.name));
    }
    presetCombo_->setCurrentIndex(0);
    connect(presetCombo_,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            &RecordingControls::onPresetChanged);
    presetLayout->addWidget(presetCombo_, 1);
    recordLayout->addLayout(presetLayout);

    // Output path
    auto* outputLayout = new QHBoxLayout();
    outputLayout->addWidget(new QLabel("Output:"));

    outputEdit_ = new QLineEdit();
    outputEdit_->setPlaceholderText("Auto-generated filename");
    outputLayout->addWidget(outputEdit_, 1);

    browseButton_ = new chadvis::GlowButton("...");
    browseButton_->setFixedWidth(40);
    browseButton_->setGlowColor(QColor("#00bcd4"));
    connect(browseButton_,
            &chadvis::GlowButton::clicked,
            this,
            &RecordingControls::onBrowseOutputClicked);
    outputLayout->addWidget(browseButton_);
    recordLayout->addLayout(outputLayout);

    // Record button (modern glow style)
    recordButton_ = new chadvis::GlowButton("⏺ Start Recording");
    recordButton_->setObjectName("recordButton");
    recordButton_->setCheckable(true);
    recordButton_->setMinimumHeight(40);
    recordButton_->setGlowColor(QColor("#ff4444"));
    connect(recordButton_,
            &chadvis::GlowButton::clicked,
            this,
            &RecordingControls::onRecordButtonClicked);
    recordLayout->addWidget(recordButton_);

    auto* optionsLayout = new QVBoxLayout();
    restartTrackCheck_ = new QCheckBox("Restart track on start");
    stopAtEndCheck_ = new QCheckBox("Stop at track end");
    recordEntireSongCheck_ = new QCheckBox("Record entire song (Auto)");

    restartTrackCheck_->setChecked(CONFIG.recording().restartTrackOnRecord);
    stopAtEndCheck_->setChecked(CONFIG.recording().stopAtTrackEnd);
    recordEntireSongCheck_->setChecked(CONFIG.recording().recordEntireSong);

    connect(restartTrackCheck_, &QCheckBox::toggled, this, [](bool checked) {
        CONFIG.recording().restartTrackOnRecord = checked;
    });
    connect(stopAtEndCheck_, &QCheckBox::toggled, this, [](bool checked) {
        CONFIG.recording().stopAtTrackEnd = checked;
    });
    connect(recordEntireSongCheck_,
            &QCheckBox::toggled,
            this,
            [](bool checked) {
                CONFIG.recording().recordEntireSong = checked;
            });

    optionsLayout->addWidget(restartTrackCheck_);
    optionsLayout->addWidget(stopAtEndCheck_);
    optionsLayout->addWidget(recordEntireSongCheck_);
    recordLayout->addLayout(optionsLayout);

    layout->addWidget(recordGroup);

    // Stats group
    auto* statsGroup = new QGroupBox("Statistics");
    auto* statsLayout = new QVBoxLayout(statsGroup);

    statusLabel_ = new QLabel("Ready");
    statusLabel_->setObjectName("statusLabel");
    statsLayout->addWidget(statusLabel_);

    auto* statsGrid = new QHBoxLayout();

    auto* timeBox = new QVBoxLayout();
    timeBox->addWidget(new QLabel("Time:"));
    timeLabel_ = new QLabel("00:00:00");
    timeLabel_->setStyleSheet("font-weight: bold; font-size: 14px;");
    timeBox->addWidget(timeLabel_);
    statsGrid->addLayout(timeBox);

    auto* framesBox = new QVBoxLayout();
    framesBox->addWidget(new QLabel("Frames:"));
    framesLabel_ = new QLabel("0");
    framesBox->addWidget(framesLabel_);
    statsGrid->addLayout(framesBox);

    auto* sizeBox = new QVBoxLayout();
    sizeBox->addWidget(new QLabel("Size:"));
    sizeLabel_ = new QLabel("0 B");
    sizeBox->addWidget(sizeLabel_);
    statsGrid->addLayout(sizeBox);

    statsLayout->addLayout(statsGrid);

    // Buffer indicator
    bufferBar_ = new QProgressBar();
    bufferBar_->setRange(0, 100);
    bufferBar_->setValue(0);
    bufferBar_->setTextVisible(false);
    bufferBar_->setMaximumHeight(8);
    statsLayout->addWidget(bufferBar_);

    layout->addWidget(statsGroup);
    layout->addStretch();
}

void RecordingControls::updateState(RecordingState state) {
    currentState_ = state;

    switch (state) {
    case RecordingState::Stopped:
        statusLabel_->setText("Ready");
        statusLabel_->setStyleSheet("color: #888888;");
        recordButton_->setText("⏺ Start Recording");
        recordButton_->setChecked(false);
        recordButton_->setEnabled(true);
        presetCombo_->setEnabled(true);
        outputEdit_->setEnabled(true);
        browseButton_->setEnabled(true);
        break;

    case RecordingState::Starting:
        statusLabel_->setText("Starting...");
        statusLabel_->setStyleSheet("color: #ffaa00;");
        recordButton_->setEnabled(false);
        break;

    case RecordingState::Recording:
        statusLabel_->setText("Recording");
        statusLabel_->setStyleSheet("color: #ff4444;");
        recordButton_->setText("⏹ Stop Recording");
        recordButton_->setChecked(true);
        recordButton_->setEnabled(true);
        presetCombo_->setEnabled(false);
        outputEdit_->setEnabled(false);
        browseButton_->setEnabled(false);

        // Reset labels immediately
        timeLabel_->setText("00:00:00");
        framesLabel_->setText("0 (0 dropped)");
        sizeLabel_->setText("0 B");
        bufferBar_->setValue(0);
        break;

    case RecordingState::Stopping:
        statusLabel_->setText("Finalizing...");
        statusLabel_->setStyleSheet("color: #ffaa00;");
        recordButton_->setEnabled(false);
        break;

    case RecordingState::Error:
        statusLabel_->setText("Error!");
        statusLabel_->setStyleSheet("color: #ff0000;");
        recordButton_->setText("⏺ Start Recording");
        recordButton_->setChecked(false);
        recordButton_->setEnabled(true);
        presetCombo_->setEnabled(true);
        outputEdit_->setEnabled(true);
        browseButton_->setEnabled(true);
        break;
    }
}

void RecordingControls::updateStats(const RecordingStats& stats) {
    auto totalSecs = stats.elapsed.count() / 1000;
    auto hours = totalSecs / 3600;
    auto mins = (totalSecs % 3600) / 60;
    auto secs = totalSecs % 60;
    timeLabel_->setText(QString("%1:%2:%3")
                                .arg(hours, 2, 10, QChar('0'))
                                .arg(mins, 2, 10, QChar('0'))
                                .arg(secs, 2, 10, QChar('0')));

    // Show frames with dropped count and encoding FPS
    QString framesText = QString("%1 (%2 dropped)").arg(stats.framesWritten).arg(stats.framesDropped);
    if (stats.encodingFps > 0) {
        framesText += QString(" @ %1 FPS").arg(stats.encodingFps, 0, 'f', 1);
    }
    framesLabel_->setText(framesText);

    sizeLabel_->setText(
            QString::fromStdString(file::humanSize(stats.bytesWritten)));

    // Buffer bar shows encoding health (target FPS vs actual)
    // Green = good (>90%), Yellow = warning (50-90%), Red = bad (<50%)
    int targetFps = CONFIG.recording().video.fps;
    int healthPercent = 0;
    if (targetFps > 0) {
        healthPercent = std::min(100, static_cast<int>((stats.avgFps / targetFps) * 100));
    }
    bufferBar_->setValue(healthPercent);
    
    // Color-code the buffer bar based on health
    if (healthPercent >= 90) {
        bufferBar_->setStyleSheet("QProgressBar { background-color: #2a2a2a; border: none; } QProgressBar::chunk { background-color: #00ff88; }");
    } else if (healthPercent >= 50) {
        bufferBar_->setStyleSheet("QProgressBar { background-color: #2a2a2a; border: none; } QProgressBar::chunk { background-color: #ffaa00; }");
    } else {
        bufferBar_->setStyleSheet("QProgressBar { background-color: #2a2a2a; border: none; } QProgressBar::chunk { background-color: #ff4444; }");
    }
}

void RecordingControls::onRecordButtonClicked() {
    if (currentState_ == RecordingState::Recording) {
        emit stopRecordingRequested();
    } else {
        QString path = outputEdit_->text();
        if (path.isEmpty()) {
            path = generateOutputPath();
            outputEdit_->setText(path);
        }
        emit startRecordingRequested(path);
    }
}

void RecordingControls::onBrowseOutputClicked() {
    QString filter = "Video Files (*.mp4 *.mkv *.webm *.mov);;All Files (*)";
    QString path = QFileDialog::getSaveFileName(
            this, "Save Recording", generateOutputPath(), filter);
    if (!path.isEmpty()) {
        outputEdit_->setText(path);
    }
}

void RecordingControls::onPresetChanged(int index) {
    Q_UNUSED(index);
}

QString RecordingControls::generateOutputPath() {
    const auto& recCfg = CONFIG.recording();

    QString filename = QString::fromStdString(recCfg.defaultFilename);

    QDateTime now = QDateTime::currentDateTime();
    filename.replace("{date}", now.toString("yyyy-MM-dd"));
    filename.replace("{time}", now.toString("HH-mm-ss"));

    // Add preset name support
    QString presetName = "unknown";
    if (auto* mw = qobject_cast<MainWindow*>(window())) {
        if (mw->visualizerPanel() && mw->visualizerPanel()->visualizer()) {
            presetName = QString::fromStdString(mw->visualizerPanel()
                                                        ->visualizer()
                                                        ->projectM()
                                                        .currentPresetName());
            // Sanitize preset name for filesystem
            presetName.replace(QRegularExpression("[^a-zA-Z0-9_-]"), "_");
        }
    }
    filename.replace("{preset}", presetName);

    filename += QString::fromStdString(
            EncoderSettings::fromConfig().containerExtension());

    fs::path outDir = recCfg.outputDirectory;
    file::ensureDir(outDir);

    return QString::fromStdString((outDir / filename.toStdString()).string());
}

} // namespace vc
