#include "RecordingControls.hpp"
#include "core/Config.hpp"
#include "util/FileUtils.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QFileDialog>
#include <QDateTime>
#include <QLabel>

namespace vc {

RecordingControls::RecordingControls(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
}

void RecordingControls::setVideoRecorder(VideoRecorder* recorder) {
    recorder_ = recorder;
    
    if (recorder) {
        recorder->stateChanged.connect([this](RecordingState s) {
            QMetaObject::invokeMethod(this, [this, s] { updateState(s); });
        });
        
        recorder->statsUpdated.connect([this](const RecordingStats& stats) {
            QMetaObject::invokeMethod(this, [this, stats] { updateStats(stats); });
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
    connect(presetCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &RecordingControls::onPresetChanged);
    presetLayout->addWidget(presetCombo_, 1);
    recordLayout->addLayout(presetLayout);
    
    // Output path
    auto* outputLayout = new QHBoxLayout();
    outputLayout->addWidget(new QLabel("Output:"));
    
    outputEdit_ = new QLineEdit();
    outputEdit_->setPlaceholderText("Auto-generated filename");
    outputLayout->addWidget(outputEdit_, 1);
    
    browseButton_ = new QPushButton("...");
    browseButton_->setFixedWidth(30);
    connect(browseButton_, &QPushButton::clicked, this, &RecordingControls::onBrowseOutputClicked);
    outputLayout->addWidget(browseButton_);
    recordLayout->addLayout(outputLayout);
    
    // Record button
    recordButton_ = new QPushButton("⏺ Start Recording");
    recordButton_->setObjectName("recordButton");
    recordButton_->setCheckable(true);
    recordButton_->setMinimumHeight(40);
    connect(recordButton_, &QPushButton::clicked, this, &RecordingControls::onRecordButtonClicked);
    recordLayout->addWidget(recordButton_);
    
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
    
    framesLabel_->setText(QString("%1 (%2 dropped)")
        .arg(stats.framesWritten)
        .arg(stats.framesDropped));
    
    sizeLabel_->setText(QString::fromStdString(file::humanSize(stats.bytesWritten)));
    
    int bufferLevel = std::min(100, static_cast<int>(stats.avgFps * 1.5));
    bufferBar_->setValue(bufferLevel);
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
    QString path = QFileDialog::getSaveFileName(this, "Save Recording", 
                                                  generateOutputPath(), filter);
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
    
    filename += QString::fromStdString(EncoderSettings::fromConfig().containerExtension());
    
    fs::path outDir = recCfg.outputDirectory;
    file::ensureDir(outDir);
    
    return QString::fromStdString((outDir / filename.toStdString()).string());
}

} // namespace vc
