/**
 * @file PerformanceSettings.qml
 * @brief Settings section: performance presets
 */

import QtQuick
import QtQuick.Layouts
import ChadVis
import "../../components"

ColumnLayout {
    Layout.fillWidth: true
    spacing: Theme.spacingMedium

    SectionHeader {
        text: "Performance Presets"
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingSmall
        Repeater {
            model: ["Performance", "Balanced", "High Fidelity", "Ultra (Chad)"]
            delegate: AppButton {
                text: modelData
                Layout.fillWidth: true
                onClicked: SettingsBridge.setPerformancePreset(modelData)
            }
        }
    }
}
