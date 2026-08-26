/**
 * @file PulseIndicator.qml
 * @brief Pulsing status dot
 *
 * Unified extraction of the three previously-inlined pulsing dots:
 * - main.qml play/pause indicator (iconSmall, success/textDisabled, 800ms)
 * - main.qml REC indicator dot (8px, recording red, 500ms)
 * - RecordingPanel pulse dot (12px, recording red, 500ms)
 *
 * Defaults match the recording dots; play/pause usage overrides
 * size/dimOpacity/periodMs to stay pixel-identical.
 *
 * @version 1.0.0
 */

import QtQuick
import ChadVis

Rectangle {
    id: root

    /** Whether the dot shows its active color and pulses */
    property bool active: false
    /** Color while active (dimmed to Theme.textDisabled when inactive) */
    property color baseColor: Theme.recording
    /** Dot diameter (width = height = size) */
    property int size: 8
    /** Lowest opacity reached by the pulse animation */
    property real dimOpacity: 0.3
    /** Duration of each fade half-cycle in ms */
    property int periodMs: 500

    width: root.size
    height: root.size
    radius: width / 2
    color: root.active ? root.baseColor : Theme.textDisabled

    SequentialAnimation on opacity {
        running: root.active
        loops: Animation.Infinite
        NumberAnimation { to: root.dimOpacity; duration: root.periodMs }
        NumberAnimation { to: 1.0; duration: root.periodMs }
    }
}
