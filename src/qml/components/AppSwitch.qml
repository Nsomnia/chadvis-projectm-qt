/**
 * @file AppSwitch.qml
 * @brief Themed Switch with animated thumb indicator
 *
 * Byte-compatible extraction of the previously-duplicated Switch indicator
 * styling in SettingsPanel (Shuffle Presets / Aspect Correction rows).
 *
 * @version 1.0.0
 */

import QtQuick
import QtQuick.Controls
import ChadVis

Switch {
    id: root

    indicator: Rectangle {
        implicitWidth: 40; implicitHeight: 20
        x: root.leftPadding; y: root.topPadding + (root.availableHeight - height) / 2
        radius: 10; color: root.checked ? Theme.accent : Theme.surfaceOverlay
        border.color: Theme.border
        Rectangle {
            width: 16; height: 16; radius: 8
            x: root.checked ? parent.width - width - 2 : 2
            y: (parent.height - height) / 2
            color: root.checked ? Theme.textOnAccent : Theme.textSecondary
            Behavior on x { NumberAnimation { duration: Theme.durationFast } }
        }
    }
}
