#include "SettingsDialog.hpp"
#include "core/Config.hpp"

#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace vc {

SettingsDialog::SettingsDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle("Settings");
    setMinimumSize(500, 400);
    setupUI();
    loadSettings();
}

void SettingsDialog::setupUI() {
    auto* layout = new QVBoxLayout(this);

    tabWidget_ = new QTabWidget();

    // === General Tab ===
    auto* generalTab = new QWidget();
    auto* generalLayout = new QFormLayout(generalTab);

    debugCheck_ = new QCheckBox("Enable debug logging");
    generalLayout->addRow("Debug:", debugCheck_);

    themeCombo_ = new QComboBox();
    themeCombo_->addItems({"dark", "gruvbox", "nord"});
    generalLayout->addRow("Theme:", themeCombo_);

    tabWidget_->addTab(generalTab, "General");

    // === Audio Tab ===
    auto* audioTab = new QWidget();
    auto* audioLayout = new QFormLayout(audioTab);

    audioDeviceCombo_ = new QComboBox();
    audioDeviceCombo_->addItem("default");
    audioLayout->addRow("Device:", audioDeviceCombo_);

    bufferSizeSpin_ = new QSpinBox();
    bufferSizeSpin_->setRange(256, 8192);
    bufferSizeSpin_->setSingleStep(256);
    audioLayout->addRow("Buffer Size:", bufferSizeSpin_);

    tabWidget_->addTab(audioTab, "Audio");

    // === Visualizer Tab ===
    auto* vizTab = new QWidget();
    auto* vizLayout = new QFormLayout(vizTab);

    auto* presetPathLayout = new QHBoxLayout();
    presetPathEdit_ = new QLineEdit();
    presetPathLayout->addWidget(presetPathEdit_);
    auto* browsePresetBtn = new QPushButton("...");
    browsePresetBtn->setFixedWidth(30);
    connect(browsePresetBtn, &QPushButton::clicked, this, [this] {
        QString dir = QFileDialog::getExistingDirectory(
                this, "Select Preset Directory", presetPathEdit_->text());
        if (!dir.isEmpty()) {
            presetPathEdit_->setText(dir);
        }
    });
    presetPathLayout->addWidget(browsePresetBtn);
    vizLayout->addRow("Preset Path:", presetPathLayout);

    auto* resLayout = new QHBoxLayout();
    vizWidthSpin_ = new QSpinBox();
    vizWidthSpin_->setRange(640, 7680);
    resLayout->addWidget(vizWidthSpin_);
    resLayout->addWidget(new QLabel("x"));
    vizHeightSpin_ = new QSpinBox();
    vizHeightSpin_->setRange(480, 4320);
    resLayout->addWidget(vizHeightSpin_);
    vizLayout->addRow("Resolution:", resLayout);

    vizFpsSpin_ = new QSpinBox();
    vizFpsSpin_->setRange(24, 144);
    vizLayout->addRow("FPS:", vizFpsSpin_);

    beatSensitivitySpin_ = new QDoubleSpinBox();
    beatSensitivitySpin_->setRange(0.1, 5.0);
    beatSensitivitySpin_->setSingleStep(0.1);
    vizLayout->addRow("Beat Sensitivity:", beatSensitivitySpin_);

    presetDurationSpin_ = new QSpinBox();
    presetDurationSpin_->setRange(5, 300);
    presetDurationSpin_->setSuffix(" sec");
    vizLayout->addRow("Preset Duration:", presetDurationSpin_);

    autoRotateCheck_ = new QCheckBox("Auto-rotate presets");
    vizLayout->addRow("", autoRotateCheck_);

    shufflePresetsCheck_ = new QCheckBox("Shuffle presets");
    vizLayout->addRow("", shufflePresetsCheck_);

    lowResourceCheck_ = new QCheckBox("Low Resource Mode (Half-res render)");
    vizLayout->addRow("", lowResourceCheck_);

    tabWidget_->addTab(vizTab, "Visualizer");

    // === Recording Tab ===
    auto* recTab = new QWidget();
    auto* recLayout = new QFormLayout(recTab);

    auto* outDirLayout = new QHBoxLayout();
    outputDirEdit_ = new QLineEdit();
    outDirLayout->addWidget(outputDirEdit_);
    auto* browseOutBtn = new QPushButton("...");
    browseOutBtn->setFixedWidth(30);
    connect(browseOutBtn, &QPushButton::clicked, this, [this] {
        QString dir = QFileDialog::getExistingDirectory(
                this, "Select Output Directory", outputDirEdit_->text());
        if (!dir.isEmpty()) {
            outputDirEdit_->setText(dir);
        }
    });
    outDirLayout->addWidget(browseOutBtn);
    recLayout->addRow("Output Directory:", outDirLayout);

    autoRecordCheck_ = new QCheckBox("Auto-record tracks (Automagical mode)");
    recLayout->addRow("", autoRecordCheck_);

    containerCombo_ = new QComboBox();
    containerCombo_->addItems({"mp4", "mkv", "webm", "mov"});
    recLayout->addRow("Container:", containerCombo_);

    videoCodecCombo_ = new QComboBox();
    videoCodecCombo_->addItems({"libx264", "libx265", "libvpx-vp9"});
    recLayout->addRow("Video Codec:", videoCodecCombo_);

    crfSpin_ = new QSpinBox();
    crfSpin_->setRange(0, 51);
    crfSpin_->setToolTip(
            "Quality: 0 = lossless, 18 = high quality, 23 = default, 51 = "
            "worst");
    recLayout->addRow("CRF (Quality):", crfSpin_);

    encoderPresetCombo_ = new QComboBox();
    encoderPresetCombo_->addItems({"ultrafast",
                                   "superfast",
                                   "veryfast",
                                   "faster",
                                   "fast",
                                   "medium",
                                   "slow",
                                   "slower",
                                   "veryslow"});
    recLayout->addRow("Encoder Preset:", encoderPresetCombo_);

    tabWidget_->addTab(recTab, "Recording");

    // === Suno Tab ===
    auto* sunoTab = new QWidget();
    auto* sunoLayout = new QFormLayout(sunoTab);

    sunoTokenEdit_ = new QLineEdit();
    sunoTokenEdit_->setEchoMode(QLineEdit::PasswordEchoOnEdit);
    sunoLayout->addRow("API Token:", sunoTokenEdit_);

    sunoCookieEdit_ = new QLineEdit();
    sunoCookieEdit_->setEchoMode(QLineEdit::PasswordEchoOnEdit);
    sunoLayout->addRow("Cookie:", sunoCookieEdit_);

    auto* sunoPathLayout = new QHBoxLayout();
    sunoDownloadPathEdit_ = new QLineEdit();
    sunoPathLayout->addWidget(sunoDownloadPathEdit_);
    auto* browseSunoBtn = new QPushButton("...");
    browseSunoBtn->setFixedWidth(30);
    connect(browseSunoBtn, &QPushButton::clicked, this, [this] {
        QString dir = QFileDialog::getExistingDirectory(
                this,
                "Select Suno Download Directory",
                sunoDownloadPathEdit_->text());
        if (!dir.isEmpty()) {
            sunoDownloadPathEdit_->setText(dir);
        }
    });
    sunoPathLayout->addWidget(browseSunoBtn);
    sunoLayout->addRow("Download Path:", sunoPathLayout);

    sunoAutoDownloadCheck_ = new QCheckBox("Auto-download synced tracks");
    sunoLayout->addRow("", sunoAutoDownloadCheck_);

    sunoSaveLyricsCheck_ = new QCheckBox("Save lyrics to database");
    sunoLayout->addRow("", sunoSaveLyricsCheck_);

    tabWidget_->addTab(sunoTab, "Suno");

    layout->addWidget(tabWidget_);

    // Dialog buttons
    auto* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();

    auto* okButton = new QPushButton("OK");
    okButton->setDefault(true);
    connect(okButton, &QPushButton::clicked, this, &SettingsDialog::accept);
    buttonLayout->addWidget(okButton);

    auto* cancelButton = new QPushButton("Cancel");
    connect(cancelButton, &QPushButton::clicked, this, &SettingsDialog::reject);
    buttonLayout->addWidget(cancelButton);

    auto* applyButton = new QPushButton("Apply");
    connect(applyButton,
            &QPushButton::clicked,
            this,
            &SettingsDialog::saveSettings);
    buttonLayout->addWidget(applyButton);

    layout->addLayout(buttonLayout);
}

void SettingsDialog::loadSettings() {
    debugCheck_->setChecked(CONFIG.debug());
    themeCombo_->setCurrentText(QString::fromStdString(CONFIG.ui().theme));

    audioDeviceCombo_->setCurrentText(
            QString::fromStdString(CONFIG.audio().device));
    bufferSizeSpin_->setValue(CONFIG.audio().bufferSize);

    presetPathEdit_->setText(
            QString::fromStdString(CONFIG.visualizer().presetPath.string()));
    vizWidthSpin_->setValue(CONFIG.visualizer().width);
    vizHeightSpin_->setValue(CONFIG.visualizer().height);
    vizFpsSpin_->setValue(CONFIG.visualizer().fps);
    beatSensitivitySpin_->setValue(CONFIG.visualizer().beatSensitivity);
    presetDurationSpin_->setValue(CONFIG.visualizer().presetDuration > 0
                                          ? CONFIG.visualizer().presetDuration
                                          : 30);
    autoRotateCheck_->setChecked(CONFIG.visualizer().presetDuration > 0);
    shufflePresetsCheck_->setChecked(CONFIG.visualizer().shufflePresets);
    lowResourceCheck_->setChecked(CONFIG.visualizer().lowResourceMode);

    outputDirEdit_->setText(QString::fromStdString(
            CONFIG.recording().outputDirectory.string()));
    autoRecordCheck_->setChecked(CONFIG.recording().autoRecord);
    containerCombo_->setCurrentText(
            QString::fromStdString(CONFIG.recording().container));
    videoCodecCombo_->setCurrentText(
            QString::fromStdString(CONFIG.recording().video.codec));
    crfSpin_->setValue(CONFIG.recording().video.crf);
    encoderPresetCombo_->setCurrentText(
            QString::fromStdString(CONFIG.recording().video.preset));

    sunoTokenEdit_->setText(QString::fromStdString(CONFIG.suno().token));
    sunoCookieEdit_->setText(QString::fromStdString(CONFIG.suno().cookie));
    sunoDownloadPathEdit_->setText(
            QString::fromStdString(CONFIG.suno().downloadPath.string()));
    sunoAutoDownloadCheck_->setChecked(CONFIG.suno().autoDownload);
    sunoSaveLyricsCheck_->setChecked(CONFIG.suno().saveLyrics);
}

void SettingsDialog::saveSettings() {
    CONFIG.setDebug(debugCheck_->isChecked());
    CONFIG.ui().theme = themeCombo_->currentText().toStdString();

    CONFIG.audio().device = audioDeviceCombo_->currentText().toStdString();
    CONFIG.audio().bufferSize = bufferSizeSpin_->value();

    CONFIG.visualizer().presetPath = presetPathEdit_->text().toStdString();
    CONFIG.visualizer().width = vizWidthSpin_->value();
    CONFIG.visualizer().height = vizHeightSpin_->value();
    CONFIG.visualizer().fps = vizFpsSpin_->value();
    CONFIG.visualizer().beatSensitivity = beatSensitivitySpin_->value();
    CONFIG.visualizer().presetDuration =
            autoRotateCheck_->isChecked() ? presetDurationSpin_->value() : 0;
    CONFIG.visualizer().shufflePresets = shufflePresetsCheck_->isChecked();
    CONFIG.visualizer().lowResourceMode = lowResourceCheck_->isChecked();

    CONFIG.recording().outputDirectory = outputDirEdit_->text().toStdString();
    CONFIG.recording().autoRecord = autoRecordCheck_->isChecked();
    CONFIG.recording().container = containerCombo_->currentText().toStdString();
    CONFIG.recording().video.codec =
            videoCodecCombo_->currentText().toStdString();
    CONFIG.recording().video.crf = crfSpin_->value();
    CONFIG.recording().video.preset =
            encoderPresetCombo_->currentText().toStdString();

    CONFIG.suno().token = sunoTokenEdit_->text().toStdString();
    CONFIG.suno().cookie = sunoCookieEdit_->text().toStdString();
    CONFIG.suno().downloadPath = sunoDownloadPathEdit_->text().toStdString();
    CONFIG.suno().autoDownload = sunoAutoDownloadCheck_->isChecked();
    CONFIG.suno().saveLyrics = sunoSaveLyricsCheck_->isChecked();

    CONFIG.save(CONFIG.configPath());
}

void SettingsDialog::accept() {
    saveSettings();
    QDialog::accept();
}

void SettingsDialog::reject() {
    QDialog::reject();
}

} // namespace vc
