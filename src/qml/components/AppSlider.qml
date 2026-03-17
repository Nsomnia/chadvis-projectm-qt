/**
 * @file AppSlider.qml
 * @brief Modern slider with cyan accent styling
 *
 * Features:
 * - Custom track and handle styling
 * - Cyan accent for filled portion
 * - Hover and press effects
 * - Optional tick marks
 *
 * @version 1.0.0
 */

import QtQuick 2.15
import "../styles"

Item {
    id: root

    property real from: 0
    property real to: 100
    property real value: 0
    property real stepSize: 1

    property bool pressed: sliderMouseArea.pressed
    property bool hovered: sliderMouseArea.containsMouse

    signal moved(real value)

    implicitWidth: 200
    implicitHeight: 24

    // ═══════════════════════════════════════════════════════════
    // CALCULATED VALUES
    // ═══════════════════════════════════════════════════════════

    readonly property real normalizedValue: (value - from) / (to - from)
    readonly property real handleWidth: 16
    readonly property real trackHeight: 6

    // ═══════════════════════════════════════════════════════════
    // TRACK
    // ═══════════════════════════════════════════════════════════

    Rectangle {
        id: track
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.right: parent.right

        height: root.trackHeight
        radius: root.trackHeight / 2

        color: Theme.surfaceOverlay

        // Filled portion
        Rectangle {
            id: filledTrack

            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom

            width: Math.max(0, Math.min(handle.x + root.handleWidth / 2, track.width))
            radius: root.trackHeight / 2

            color: Theme.accent

            // Glow effect
            Rectangle {
                anchors.fill: parent
                anchors.margins: -2
                radius: parent.radius + 2
                color: "transparent"
                border.color: Theme.accent
                border.width: 1
                opacity: 0.5
                visible: root.hovered || root.pressed
            }
        }
    }

    // ═══════════════════════════════════════════════════════════
    // HANDLE
    // ═══════════════════════════════════════════════════════════

    Rectangle {
        id: handle

        anchors.verticalCenter: parent.verticalCenter

        x: Math.max(0, Math.min(root.normalizedValue * (track.width - root.handleWidth), track.width - root.handleWidth))
        width: root.handleWidth
        height: root.handleWidth

        radius: root.handleWidth / 2

        color: root.pressed ? Theme.accentPressed :
               root.hovered ? Theme.accentHover : Theme.accent

        // Border glow
        border.color: Theme.accentLight
        border.width: root.hovered || root.pressed ? 2 : 1

        // Scale on hover/press
        scale: root.pressed ? 1.1 : (root.hovered ? 1.05 : 1.0)

        Behavior on scale {
            NumberAnimation { duration: Theme.durationInstant }
        }

        // Inner shadow
        Rectangle {
            anchors.centerIn: parent
            width: parent.width - 4
            height: parent.height - 4
            radius: parent.radius - 2
            color: Qt.lighter(parent.color, 1.2)
            opacity: 0.5
        }
    }

    // ═══════════════════════════════════════════════════════════
    // MOUSE AREA
    // ═══════════════════════════════════════════════════════════

    MouseArea {
        id: sliderMouseArea
        anchors.fill: parent
        hoverEnabled: true

        function updateValue(mouseX) {
            var newX = mouseX - root.handleWidth / 2
            var maxX = track.width - root.handleWidth
            var normalized = Math.max(0, Math.min(1, newX / maxX))
            var newValue = root.from + normalized * (root.to - root.from)

            // Apply step size
            if (root.stepSize > 0) {
                newValue = Math.round(newValue / root.stepSize) * root.stepSize
            }

            root.value = Math.max(root.from, Math.min(root.to, newValue))
            root.moved(root.value)
        }

        onPressed: updateValue(mouseX)
        onPositionChanged: {
            if (pressed) updateValue(mouseX)
        }
    }
}
