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
    onOpened: {
        root.working = root.value
        root.committed = root.value
    }

    implicitWidth: Theme.inputHeight
    implicitHeight: Theme.inputHeight

    // HSV decomposition of the working colour. Qt.hsva(color) returns
    // { h, s, v, a } with each component normalised to 0..1.
    readonly property var workingHsv: Qt.hsva(working)
    readonly property real workingHue: workingHsv ? workingHsv.h : 0.0
    readonly property real workingSat: workingHsv ? workingHsv.s : 0.0
    readonly property real workingVal: workingHsv ? workingHsv.v : 0.0
    readonly property real workingAlpha: working.a

    function emitWorking() {
        root.picked(root.working)
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

        MouseArea {
            id: swatchMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: picker.open()
        }
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
                    gradient: LinearGradient {
                        horizontal: true
                        GradientStop { position: 0.0; color: "#FFFFFF" }
                        GradientStop { position: 1.0; color: Qt.hsva(root.workingHue, 1.0, 1.0, 1.0) }
                    }
                }
                Rectangle {
                    anchors.fill: parent
                    gradient: LinearGradient {
                        vertical: true
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

                gradient: LinearGradient {
                    horizontal: true
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
                    gradient: LinearGradient {
                        horizontal: true
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