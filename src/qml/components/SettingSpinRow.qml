/**
 * @file SettingSpinRow.qml
 * @brief Label + SpinBox row for settings panels
 *
 * Extraction of the nine identical label/SpinBox rows in SettingsPanel:
 * caption label on the left (fillWidth) and a themed SpinBox on the right.
 *
 * Usage:
 *   SettingSpinRow {
 *       label: "Audio Buffer (ms)"
 *       from: 10; to: 1000; stepSize: 10
 *       value: SettingsBridge.audioBufferSize
 *       onValueMoved: (val) => SettingsBridge.audioBufferSize = val
 *   }
 *
 * @version 1.0.0
 */

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import ChadVis

RowLayout {
    id: root

    /** Left-side setting label */
    property string label: ""
    property int from: 0
    property int to: 99
    property int stepSize: 1
    /** Currently displayed value (bind to the backing setting) */
    property int value: 0

    /** Emitted when the user changes the value (not on programmatic rebinding) */
    signal valueMoved(int val)

    Layout.fillWidth: true

    Text {
        text: root.label
        color: Theme.textSecondary
        font: Theme.fontCaption
        Layout.fillWidth: true
    }

    SpinBox {
        from: root.from
        to: root.to
        stepSize: root.stepSize
        value: root.value
        onValueModified: root.valueMoved(value)
        background: Rectangle {
            color: Theme.surfaceRaised
            radius: Theme.radiusSmall
            border.color: Theme.border
        }
    }
}
