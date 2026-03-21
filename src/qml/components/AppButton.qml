/**
 * @file AppButton.qml
 * @brief Modern button with glassmorphism and glow effects
 *
 * Features:
 * - Glassmorphism background with configurable opacity
 * - Cyan accent glow on hover/focus
 * - Icon or text content
 * - Press feedback animation
 * - Disabled state styling
 *
 * @version 1.0.0
 */

import QtQuick
import Qt5Compat.GraphicalEffects
import QtQuick.Layouts
import "../styles"

Rectangle {
    id: root

    // ═══════════════════════════════════════════════════════════
    // PROPERTIES
    // ═══════════════════════════════════════════════════════════

    property string text: ""
    property string icon: ""
    property bool highlighted: false
    property bool flat: false
    property int buttonRadius: Theme.radiusMedium

    property alias pressed: mouseArea.pressed
    property alias hovered: mouseArea.containsMouse

    signal clicked()
    signal pressed()
    signal released()

    implicitWidth: Math.max(buttonLayout.implicitWidth + Theme.spacingMedium * 2, Theme.buttonHeight)
    implicitHeight: Theme.buttonHeight

    radius: buttonRadius

    // ═══════════════════════════════════════════════════════════
    // STYLING
    // ═══════════════════════════════════════════════════════════

    color: root.flat ? "transparent" :
           root.highlighted ? Theme.accent :
           Theme.surfaceRaised

    border.width: root.flat ? 0 : 1
    border.color: root.highlighted ? Theme.accentLight :
                  mouseArea.containsMouse ? Theme.accent :
                  Theme.border

    // Glassmorphism effect
    Rectangle {
        anchors.fill: parent
        radius: buttonRadius
        color: Theme.glassBackground
        opacity: root.flat ? 0 : (root.highlighted ? 0.3 : 0.85)
    }

    // Glow effect on hover/highlight
    Rectangle {
        anchors.fill: parent
        anchors.margins: -2
        radius: buttonRadius + 2
        color: "transparent"
        border.color: Theme.accent
        border.width: 2
        opacity: glowAnimation.running ? 0.5 : (mouseArea.containsMouse ? 0.3 : 0)
        visible: !root.flat

        Behavior on opacity {
            NumberAnimation { duration: Theme.durationFast }
        }

        PropertyAnimation on opacity {
            id: glowAnimation
            running: root.highlighted && !root.flat
            loops: Animation.Infinite
            from: 0.3
            to: 0.6
            duration: 1500
            easing.type: Easing.SinusoidalInOut
        }
    }

    // Press scale effect
    scale: mouseArea.pressed ? 0.97 : 1.0

    Behavior on scale {
        NumberAnimation { duration: Theme.durationInstant }
    }

    // ═══════════════════════════════════════════════════════════
    // CONTENT
    // ═══════════════════════════════════════════════════════════

    RowLayout {
        id: buttonLayout
        anchors.centerIn: parent
        spacing: root.text !== "" && root.icon !== "" ? Theme.spacingSmall : 0

        Image {
            visible: root.icon !== ""
            source: root.icon
            Layout.preferredWidth: Theme.iconMedium
            Layout.preferredHeight: Theme.iconMedium
            fillMode: Image.PreserveAspectFit

            ColorOverlay {
                anchors.fill: parent
                source: parent
                color: root.enabled ?
                       (root.highlighted ? Theme.textOnAccent : Theme.textPrimary) :
                       Theme.textDisabled
            }
        }

        Text {
            visible: root.text !== ""
            text: root.text
            color: root.enabled ?
                   (root.highlighted ? Theme.textOnAccent : Theme.textPrimary) :
                   Theme.textDisabled
            font: Theme.fontBody
        }
    }

    // ═══════════════════════════════════════════════════════════
    // MOUSE AREA
    // ═══════════════════════════════════════════════════════════

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: root.enabled ? Qt.PointingHandCursor : Qt.ForbiddenCursor

        onClicked: {
            if (root.enabled) root.clicked()
        }

        onPressed: {
            if (root.enabled) root.pressed()
        }

        onReleased: {
            if (root.enabled) root.released()
        }
    }
}
