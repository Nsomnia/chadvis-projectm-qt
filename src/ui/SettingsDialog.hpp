#pragma once
// SettingsDialog.hpp - Application settings
// All the knobs in one place

#include "util/Types.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QFontComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QTabWidget>
#include <QPushButton>
#include <QColor>

namespace vc {

class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget* parent = nullptr);

public slots:
    void accept() override;
    void reject() override;

private:
    void setupUI();
    void loadSettings();
    void saveSettings();

    QTabWidget* tabWidget_{nullptr};

    // General
    QCheckBox* debugCheck_{nullptr};
    QComboBox* themeCombo_{nullptr};
    QCheckBox* showPlaylistCheck_{nullptr};
    QCheckBox* showPresetsCheck_{nullptr};
    QCheckBox* showDebugPanelCheck_{nullptr};

    // Audio
    QComboBox* audioDeviceCombo_{nullptr};
    QSpinBox* bufferSizeSpin_{nullptr};

    // Visualizer
    QLineEdit* presetPathEdit_{nullptr};
    QSpinBox* vizWidthSpin_{nullptr};
    QSpinBox* vizHeightSpin_{nullptr};
    QSpinBox* vizFpsSpin_{nullptr};
    QDoubleSpinBox* beatSensitivitySpin_{nullptr};
    QSpinBox* presetDurationSpin_{nullptr};
    QSpinBox* smoothPresetDurationSpin_{nullptr};
    QCheckBox* autoRotateCheck_{nullptr};
    QCheckBox* shufflePresetsCheck_{nullptr};
    QCheckBox* lowResourceCheck_{nullptr};

    // Recording
    QLineEdit* outputDirEdit_{nullptr};
    QLineEdit* defaultFilenameEdit_{nullptr};
    QCheckBox* autoRecordCheck_{nullptr};
    QCheckBox* recordEntireSongCheck_{nullptr};
    QCheckBox* restartTrackOnRecordCheck_{nullptr};
    QCheckBox* stopAtTrackEndCheck_{nullptr};
    QComboBox* containerCombo_{nullptr};
    QComboBox* videoCodecCombo_{nullptr};
    QSpinBox* crfSpin_{nullptr};
    QComboBox* encoderPresetCombo_{nullptr};

    // Suno
    QLineEdit* sunoTokenEdit_{nullptr};
    QLineEdit* sunoCookieEdit_{nullptr};
    QLineEdit* sunoDownloadPathEdit_{nullptr};
    QCheckBox* sunoAutoDownloadCheck_{nullptr};
    QCheckBox* sunoSaveLyricsCheck_{nullptr};
    QCheckBox* sunoEmbedMetadataCheck_{nullptr};

    // Karaoke
    QCheckBox* kEnabledCheck_{nullptr};
    QFontComboBox* kFontCombo_{nullptr};
    QSpinBox* kFontSizeSpin_{nullptr};
    QCheckBox* kBoldCheck_{nullptr};
    QDoubleSpinBox* kYPosSpin_{nullptr};
    // We'll use buttons that launch QColorDialog for colors
    QPushButton* kActiveColorBtn_{nullptr};
    QPushButton* kInactiveColorBtn_{nullptr};
    QPushButton* kShadowColorBtn_{nullptr};
    
    QColor kActiveColor_;
    QColor kInactiveColor_;
    QColor kShadowColor_;
};

} // namespace vc
