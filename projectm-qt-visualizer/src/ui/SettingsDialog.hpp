#pragma once
// SettingsDialog.hpp - Application settings
// All the knobs in one place

#include "util/Types.hpp"

#include <QDialog>
#include <QTabWidget>
#include <QLineEdit>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QCheckBox>

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
    QCheckBox* shufflePresetsCheck_{nullptr};
    
    // Recording
    QLineEdit* outputDirEdit_{nullptr};
    QComboBox* containerCombo_{nullptr};
    QComboBox* videoCodecCombo_{nullptr};
    QSpinBox* crfSpin_{nullptr};
    QComboBox* encoderPresetCombo_{nullptr};
};

} // namespace vc
