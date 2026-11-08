/**
 * @file AppColorPicker.qml
 * @brief Interactive HSV colour picker with alpha
 *
 * Features:
 * - Clickable swatch that opens a popup picker
 * - Saturation/value area, hue strip and alpha strip, all drag-driven
 * - Live `picked()` emission while dragging, so the caller can preview
 * - Alpha is first class, because alpha is the channel T0047 lost: the
 *   persisted form is `#RRGGBBAA` and Qt's own `toString()` would write
 *   `#AARRGGBB`. Emission is a `color`, never a string, so a caller persists
 *   through Theme.toRgbaHex() -- see AppearanceSettings.qml.
 *
 * This exists because of T0183: the appearance panel shipped a read-only swatch
 * and a hex label with no input handling at all, so clicking a colour did
 * nothing. There was no picker to be broken.
 *
 * @version 1.0.0
 */

import QtQuick
import QtQuick.Controls
import ChadVis

Item {
    id: root

    /// The colour this picker currently shows.
    property color value: "#00bcd4"

    /// Screen-reader name for the swatch that opens the picker ("Accent
    /// colour", "Background colour"). The swatch is icon-only by nature -- it
    /// shows a colour, not a word -- so a caller with a visible label must set
    /// this, exactly as AppSlider requires.
    property string accessibleName: ""

    /// Emitted continuously while the user interacts, in `color` form.
    signal picked(color c)

    readonly property bool hovered: swatchMouse.containsMouse

    /// Working copy. Reopened popups start from `value`, which is only
    /// overwritten on intent, so Cancel really cancels.
    property color working: value
    onValueChanged: working = value

    /// The value as it stood when this popup was opened. Live preview means
    /// `value` is overwritten the moment the user drags, so restoring `value`
    /// on Cancel is not enough -- `value` no longer holds the original. This is
    /// the original, and Cancel re-emits it.
    property color committed: value

    implicitWidth: Theme.inputHeight
    implicitHeight: Theme.inputHeight

    // HSV decomposition of the working colour, components normalised to 0..1.
    //
    // Written out rather than delegated because `Qt.hsva()` has no single-colour
    // overload — only Qt.hsva(h, s, v, a). Qt can go colour → hsva and hsva →
    // colour, but not colour → hsva, so the middle step has to happen in JS.
    // (This was "Unable to determine callable overload", which QML reports as a
    // *warning* while the property silently evaluates to undefined — and then
    // every colour the picker offers is black.)
    function hsvOf(c) {
        const r = c.r, g = c.g, b = c.b
        const max = Math.max(r, g, b)
        const min = Math.min(r, g, b)
        const d = max - min
        let h = 0
        if (d !== 0) {
            if (max === r)
                h = ((g - b) / d) % 6
            else if (max === g)
                h = (b - r) / d + 2
            else
                h = (r - g) / d + 4
            h /= 6
            if (h < 0)
                h += 1
        }
        return { h: h, s: max === 0 ? 0 : d / max, v: max, a: c.a }
    }

    readonly property var workingHsv: hsvOf(working)
    readonly property real workingHue: workingHsv ? workingHsv.h : 0.0
    readonly property real workingSat: workingHsv ? workingHsv.s : 0.0
    readonly property real workingVal: workingHsv ? workingHsv.v : 0.0
    readonly property real workingAlpha: working.a

    function emitWorking() {
        root.picked(root.working)
    }

    // Keyboard adjustment for the three strips below. Each mirrors its drag
    // handler exactly -- same clamping, same `picked()` emission -- so a key and
    // a drag are one interaction model, the way AppSlider's keyboard path is.
    function nudgeHue(delta) {
        root.working = Qt.hsva(Math.max(0, Math.min(1, root.workingHue + delta)),
                               root.workingSat, root.workingVal, root.workingAlpha)
        root.emitWorking()
    }

    function nudgeAlpha(delta) {
        root.working = Qt.hsva(root.workingHue, root.workingSat, root.workingVal,
                               Math.max(0, Math.min(1, root.workingAlpha + delta)))
        root.emitWorking()
    }

    function nudgeSatVal(dSat, dVal) {
        root.working = Qt.hsva(root.workingHue,
                               Math.max(0, Math.min(1, root.workingSat + dSat)),
                               Math.max(0, Math.min(1, root.workingVal + dVal)),
                               root.workingAlpha)
        root.emitWorking()
    }

    // ═══════════════════════════════════════════════════════════
    // SWATCH
    // ═══════════════════════════════════════════════════════════

    Rectangle {
        id: swatch

        anchors.fill: parent
        radius: Theme.radiusSmall

        color: root.value
        border.color: Theme.border
        border.width: 1

        Behavior on color {
            ColorAnimation { duration: Theme.durationFast }
        }

        Rectangle {
            anchors.fill: parent
            anchors.margins: -1
            radius: parent.radius + 1
            color: "transparent"
            border.color: Theme.accent
            border.width: 2
            opacity: root.hovered ? 1.0 : 0.0

            Behavior on opacity {
                NumberAnimation { duration: Theme.durationFast }
            }
        }

        // Focus ring for keyboard focus only; an overlay, so no geometry shifts.
        Rectangle {
            anchors.fill: parent
            anchors.margins: -Theme.spacingTiny
            radius: Theme.radiusSmall + Theme.spacingTiny
            color: Theme.withAlpha(Theme.background, 0)
            border.width: 2
            border.color: Theme.borderFocus
            visible: swatch.activeFocus && swatch.keyboardFocused
        }

        MouseArea {
            id: swatchMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                swatch.keyboardFocused = false
                swatch.forceActiveFocus()
                picker.open()
            }
        }

        // The swatch is the only way into the picker, and it was mouse-only:
        // a coloured Rectangle with a MouseArea and nothing a reader or the
        // keyboard could reach.
        activeFocusOnTab: true
        /// True for keyboard focus; Item has no focusReason, so click clears it.
        property bool keyboardFocused: true
        onActiveFocusChanged: if (!activeFocus) keyboardFocused = true
        Accessible.role: Accessible.Button
        Accessible.name: root.accessibleName.length > 0
            ? root.accessibleName + ", " + Theme.toRgbaHex(root.value)
            : "Colour " + Theme.toRgbaHex(root.value)
        Accessible.onPressAction: picker.open()

        Keys.onReturnPressed: picker.open()
        Keys.onSpacePressed: picker.open()
    }

    // ═══════════════════════════════════════════════════════════
    // POPUP
    // ═══════════════════════════════════════════════════════════

    Popup {
        id: picker

        anchors.centerIn: Overlay.overlay
        modal: true
        focus: true
        padding: Theme.spacingMedium
        width: svArea.width + padding * 2

        // Reset both working and committed each time the popup opens. On the
        // root Item this is not merely misplaced: `opened` is Popup's signal,
        // so an `onOpened` on the Item is QML assigning to a non-existent
        // property, which fails the *whole file* -- and with it the settings
        // window and the main window. Only the launching app finds that.
        onOpened: {
            root.working = root.value
            root.committed = root.value
        }

        background: Rectangle {
            color: Theme.surfaceOverlay
            radius: Theme.radiusMedium
            border.color: Theme.borderLight
        }

        enter: Transition {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationFast }
        }
        exit: Transition {
            NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.durationFast }
        }

        contentItem: Column {
            spacing: Theme.spacingMedium

            // ── Saturation / value area ───────────────────────────
            // Left to right is the hue at full saturation and value; the
            // top-to-bottom black gradient multiplies value over it. Two
            // stacked gradients, so no Canvas and no per-pixel work.
            Rectangle {
                id: svArea

                width: 260
                height: 150
                radius: Theme.radiusSmall
                clip: true
                border.color: Theme.border

                Rectangle {
                    anchors.fill: parent
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0.0; color: "#FFFFFF" }
                        GradientStop { position: 1.0; color: Qt.hsva(root.workingHue, 1.0, 1.0, 1.0) }
                    }
                }
                Rectangle {
                    anchors.fill: parent
                    gradient: Gradient {
                        orientation: Gradient.Vertical
                        GradientStop { position: 0.0; color: "#00000000" }
                        GradientStop { position: 1.0; color: "#FF000000" }
                    }
                }

                // Selection ring. The ring colour flips with value so it stays
                // visible against both ends of the gradient.
                Rectangle {
                    readonly property real d: 14
                    x: svArea.width * root.workingSat - width / 2
                    y: svArea.height * (1.0 - root.workingVal) - height / 2
                    width: d
                    height: d
                    radius: d / 2
                    color: "transparent"
                    border.color: root.workingVal > 0.55 ? "#CC000000" : "#CCFFFFFF"
                    border.width: 2
                }

                // Focus ring for keyboard focus only; inset, because svArea is
                // clipped, so an outer ring would be cut off.
                Rectangle {
                    anchors.fill: parent
                    radius: Theme.radiusSmall
                    color: Theme.withAlpha(Theme.background, 0)
                    border.width: 2
                    border.color: Theme.borderFocus
                    visible: svArea.activeFocus && svArea.keyboardFocused
                }

                activeFocusOnTab: true
                /// Keyboard gate: nothing takes item focus by mouse, so activeFocus means Tab.
                property bool keyboardFocused: true
                Accessible.role: Accessible.Slider
                Accessible.name: "Saturation and brightness"
                Accessible.onIncreaseAction: root.nudgeSatVal(0.01, 0)
                Accessible.onDecreaseAction: root.nudgeSatVal(-0.01, 0)

                Keys.onPressed: function (event) {
                    switch (event.key) {
                    case Qt.Key_Left: root.nudgeSatVal(-0.01, 0); event.accepted = true; break
                    case Qt.Key_Right: root.nudgeSatVal(0.01, 0); event.accepted = true; break
                    case Qt.Key_Down: root.nudgeSatVal(0, -0.01); event.accepted = true; break
                    case Qt.Key_Up: root.nudgeSatVal(0, 0.01); event.accepted = true; break
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.CrossCursor

                    function pickAt(mx, my) {
                        const sat = Math.max(0, Math.min(1, mx / svArea.width))
                        const val = 1.0 - Math.max(0, Math.min(1, my / svArea.height))
                        root.working = Qt.hsva(root.workingHue, sat, val, root.workingAlpha)
                        root.emitWorking()
                    }

                    onPressed: function(mouse) { pickAt(mouse.x, mouse.y) }
                    onPositionChanged: function(mouse) {
                        if (pressed) pickAt(mouse.x, mouse.y)
                    }
                }
            }

            // ── Hue strip ─────────────────────────────────────────
            Rectangle {
                id: hueStrip

                width: svArea.width
                height: 20
                radius: Theme.radiusSmall
                clip: true

                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.00; color: "#FFFF0000" }
                    GradientStop { position: 0.17; color: "#FFFFFF00" }
                    GradientStop { position: 0.33; color: "#FF00FF00" }
                    GradientStop { position: 0.50; color: "#FF00FFFF" }
                    GradientStop { position: 0.67; color: "#FF0000FF" }
                    GradientStop { position: 0.83; color: "#FFFF00FF" }
                    GradientStop { position: 1.00; color: "#FFFF0000" }
                }

                Rectangle {
                    readonly property real d: 16
                    x: hueStrip.width * root.workingHue - width / 2
                    anchors.verticalCenter: parent.verticalCenter
                    width: d
                    height: d
                    radius: d / 2
                    color: "transparent"
                    border.color: "#FFFFFFFF"
                    border.width: 3
                }

                // Focus ring for keyboard focus only; inset, because the strip
                // is clipped, so an outer ring would be cut off.
                Rectangle {
                    anchors.fill: parent
                    radius: Theme.radiusSmall
                    color: Theme.withAlpha(Theme.background, 0)
                    border.width: 2
                    border.color: Theme.borderFocus
                    visible: hueStrip.activeFocus && hueStrip.keyboardFocused
                }

                activeFocusOnTab: true
                /// Keyboard gate: nothing takes item focus by mouse, so activeFocus means Tab.
                property bool keyboardFocused: true
                Accessible.role: Accessible.Slider
                Accessible.name: "Hue"
                Accessible.onIncreaseAction: root.nudgeHue(0.01)
                Accessible.onDecreaseAction: root.nudgeHue(-0.01)

                Keys.onPressed: function (event) {
                    switch (event.key) {
                    case Qt.Key_Left:
                    case Qt.Key_Down: root.nudgeHue(-0.01); event.accepted = true; break
                    case Qt.Key_Right:
                    case Qt.Key_Up: root.nudgeHue(0.01); event.accepted = true; break
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor

                    function pickAt(mx) {
                        const hue = Math.max(0, Math.min(1, mx / hueStrip.width))
                        root.working = Qt.hsva(hue, root.workingSat, root.workingVal, root.workingAlpha)
                        root.emitWorking()
                    }

                    onPressed: function(mouse) { pickAt(mouse.x) }
                    onPositionChanged: function(mouse) {
                        if (pressed) pickAt(mouse.x)
                    }
                }
            }

            // ── Alpha strip ───────────────────────────────────────
            // Left opaque, right transparent, over a checkerboard, so an alpha
            // of 0 reads as see-through rather than as a white band. The
            // checkerboard is drawn: the repo ships no image assets and adding
            // one for eight squares is not a trade worth making.
            Rectangle {
                id: alphaStrip

                width: svArea.width
                height: 20
                radius: Theme.radiusSmall
                clip: true

                Item {
                    id: checker
                    anchors.fill: parent
                    readonly property real cell: 10
                    readonly property int cols: Math.ceil(alphaStrip.width / cell)
                    readonly property int rows: Math.ceil(alphaStrip.height / cell)

                    Repeater {
                        model: checker.cols * checker.rows
                        delegate: Rectangle {
                            required property int index
                            readonly property int col: index % checker.cols
                            readonly property int row: Math.floor(index / checker.cols)
                            x: col * checker.cell
                            y: row * checker.cell
                            width: checker.cell
                            height: checker.cell
                            color: ((col + row) % 2 === 0) ? "#FF5A5A5A" : "#FF383838"
                        }
                    }
                }

                Rectangle {
                    anchors.fill: parent
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop {
                            position: 0.0
                            color: Qt.hsva(root.workingHue, root.workingSat, root.workingVal, 0.0)
                        }
                        GradientStop {
                            position: 1.0
                            color: Qt.hsva(root.workingHue, root.workingSat, root.workingVal, 1.0)
                        }
                    }
                }

                Rectangle {
                    readonly property real d: 16
                    x: alphaStrip.width * (1.0 - root.workingAlpha) - width / 2
                    anchors.verticalCenter: parent.verticalCenter
                    width: d
                    height: d
                    radius: d / 2
                    color: "transparent"
                    border.color: "#FFFFFFFF"
                    border.width: 3
                }

                // Focus ring for keyboard focus only; inset, because the strip
                // is clipped, so an outer ring would be cut off.
                Rectangle {
                    anchors.fill: parent
                    radius: Theme.radiusSmall
                    color: Theme.withAlpha(Theme.background, 0)
                    border.width: 2
                    border.color: Theme.borderFocus
                    visible: alphaStrip.activeFocus && alphaStrip.keyboardFocused
                }

                activeFocusOnTab: true
                /// Keyboard gate: nothing takes item focus by mouse, so activeFocus means Tab.
                property bool keyboardFocused: true
                Accessible.role: Accessible.Slider
                Accessible.name: "Opacity"
                Accessible.onIncreaseAction: root.nudgeAlpha(0.01)
                Accessible.onDecreaseAction: root.nudgeAlpha(-0.01)

                Keys.onPressed: function (event) {
                    switch (event.key) {
                    case Qt.Key_Left:
                    case Qt.Key_Down: root.nudgeAlpha(-0.01); event.accepted = true; break
                    case Qt.Key_Right:
                    case Qt.Key_Up: root.nudgeAlpha(0.01); event.accepted = true; break
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor

                    function pickAt(mx) {
                        const a = 1.0 - Math.max(0, Math.min(1, mx / alphaStrip.width))
                        root.working = Qt.hsva(root.workingHue, root.workingSat, root.workingVal, a)
                        root.emitWorking()
                    }

                    onPressed: function(mouse) { pickAt(mouse.x) }
                    onPositionChanged: function(mouse) {
                        if (pressed) pickAt(mouse.x)
                    }
                }
            }

            Text {
                width: svArea.width
                text: Theme.toRgbaHex(root.working) + "   " + Math.round(root.workingAlpha * 100) + "%"
                color: Theme.textSecondary
                font: Theme.fontCaption
                elide: Text.ElideRight
            }

            Row {
                anchors.right: parent.right
                spacing: Theme.spacingSmall

                // Live preview: every drag re-emits, so the caller shows the change
                // immediately. That is why Cancel cannot simply restore `value`
                // -- `value` already moved -- and re-emits `committed` instead.
                // T0189 owns the preview/commit question proper (live preview,
                // reset-to-default, and a contrast readout); this is the
                // minimum needed for a Cancel button to mean what it says.
                AppButton {
                    text: "Cancel"
                    flat: true
                    onClicked: {
                        root.working = root.committed
                        root.picked(root.committed)
                        picker.close()
                    }
                }
                AppButton {
                    text: "Done"
                    highlighted: true
                    onClicked: picker.close()
                }
            }
        }
    }
}