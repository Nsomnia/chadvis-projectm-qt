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
 * Default divergence from Qt (deliberate): the raw QtQuick.Controls Slider
 * defaults stepSize to 0 (continuous), but this wrapper defaults stepSize to
 * 1, so mouse, keyboard and screen-reader steps quantize to whole units
 * unless the caller opts out. Any fractional or continuous control MUST set
 * stepSize explicitly (0 for continuous, or the desired fraction); an integer
 * control may rely on the default 1. See stepSize below.
 *
 * Accessibility: accessibleName defaults to "" (unnamed). A slider that is
 * not given a real accessibleName is announced only by its role and value,
 * exactly as a native Qt control with no accessibleName is. Every slider
 * that has a visible label should set accessibleName to that label.
 *
 * @version 1.0.0
 */

import QtQuick
import ChadVis

Item {
    id: root

    property real from: 0
    property real to: 100
    property real value: 0
    /// Step size for mouse, keyboard and screen-reader movement.
    /// WARNING: this defaults to 1, not to Qt Slider's 0 (continuous).
    /// Fractional/continuous controls must set this explicitly (e.g. 0 or
    /// 0.01); otherwise values silently quantize to whole units.
    property real stepSize: 1

    property bool pressed: sliderMouseArea.pressed
    property bool hovered: sliderMouseArea.containsMouse
    /// Screen-reader name. Defaults to "" (unnamed, as a native Qt control
    /// with no accessibleName is); callers with a visible label must set a
    /// real name here.
    property string accessibleName: ""

    signal moved(real value)

    implicitWidth: 200
    implicitHeight: 24

    activeFocusOnTab: true
    Accessible.role: Accessible.Slider
    Accessible.name: root.accessibleName
    Accessible.description: root.accessibleName.length > 0
        ? root.accessibleName + ", " + Math.round(root.value)
        : "" + Math.round(root.value)
    // Screen readers drive a Slider role through these, not through a value
    // property (Accessible has no `value`). Keep them in lockstep with keys.
    Accessible.onIncreaseAction: root.stepBy(1)
    Accessible.onDecreaseAction: root.stepBy(-1)

    // One handler for every key: relying on the specific-vs-generic Keys
    // dispatch order is fragile, and this keeps every key's behaviour in one
    // place. event.accepted stops the key reaching a parent control.
    Keys.onPressed: function (event) {
        switch (event.key) {
        case Qt.Key_Left:
        case Qt.Key_Down:
            root.stepBy(-1); event.accepted = true; break
        case Qt.Key_Right:
        case Qt.Key_Up:
            root.stepBy(1); event.accepted = true; break
        case Qt.Key_PageUp:
            root.stepBy(10); event.accepted = true; break
        case Qt.Key_PageDown:
            root.stepBy(-10); event.accepted = true; break
        case Qt.Key_Home:
            root.setValue(root.from); event.accepted = true; break
        case Qt.Key_End:
            root.setValue(root.to); event.accepted = true; break
        }
    }

    /// Keyboard step. The mouse path clamps to [from, to] and emits moved();
    /// keyboard must do exactly the same so callers see one interaction model.
    function stepBy(steps) {
        var step = root.stepSize > 0 ? root.stepSize : 1
        setValue(root.value + steps * step)
    }

    function setValue(newValue) {
        var step = root.stepSize > 0 ? root.stepSize : 0
        var v = step > 0 ? Math.round(newValue / step) * step : newValue
        v = Math.max(root.from, Math.min(root.to, v))
        if (v === root.value)
            return
        root.value = v
        root.moved(root.value)
    }

    // ═══════════════════════════════════════════════════════════
    // CALCULATED VALUES
    // ═══════════════════════════════════════════════════════════

    readonly property real normalizedValue: (to - from) === 0 ? 0 : (value - from) / (to - from)
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
