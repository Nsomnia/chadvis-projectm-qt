/**
 * @file AppButton.qml
 * @brief Modern button with glassmorphism and glow effects
 */

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Effects
import QtQuick.Layouts
import ChadVis

Rectangle {
    id: root

    property string text: ""
    property string icon: ""
    property bool highlighted: false
    property bool flat: false
    property int buttonRadius: Theme.radiusMedium

    property alias pressed: mouseArea.pressed
    property alias hovered: mouseArea.containsMouse

    signal clicked()
    signal pressAndHold()
    signal released()

    implicitWidth: Math.max(buttonLayout.implicitWidth + Theme.spacingMedium * 2, Theme.buttonHeight)
    implicitHeight: Theme.buttonHeight
    radius: buttonRadius
    activeFocusOnTab: true

    Accessible.role: Accessible.Button
    Accessible.name: text
    Accessible.onPressAction: {
        if (root.enabled)
            root.clicked()
    }

    color: root.flat ? "transparent" : (root.highlighted ? Theme.accent : Theme.surfaceRaised)
    border.width: root.flat ? 0 : 1
    border.color: root.highlighted ? Theme.accentLight : (mouseArea.containsMouse ? Theme.accent : Theme.border)

    Rectangle {
        anchors.fill: parent
        radius: root.buttonRadius
        color: Theme.glassBackground
        opacity: root.flat ? 0 : (root.highlighted ? 0.3 : 0.85)
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: -2
        radius: root.buttonRadius + 2
        color: "transparent"
        border.color: Theme.accent
        border.width: 2
        opacity: glowAnimation.running ? 0.5 : (mouseArea.containsMouse ? 0.3 : 0)
        visible: !root.flat
        Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }
        PropertyAnimation on opacity {
            id: glowAnimation
            running: root.highlighted && !root.flat
            loops: Animation.Infinite
            from: 0.3; to: 0.6; duration: 1500; easing.type: Easing.InOutSine
        }
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: -3
        radius: root.buttonRadius + 3
        color: Theme.withAlpha(Theme.background, 0)
        border.width: 2
        border.color: Theme.borderFocus
        visible: root.activeFocus
    }

    // T0188. The app's primary control used to react to PRESS only -- scale 0.97 --
    // and not at all to hover. That is not a mistake of taste so much as a
    // desktop convention: a mouse user is assumed to be looking. The reference
    // design this app is heading towards treats it like a touch surface that
    // happens to have a cursor, and gives hover its own state.
    //
    // 1.02 on hover is small on purpose. It has to read as "this responds to
    // you" without making a row of buttons shimmer, which is what a larger
    // factor does the moment three of them are adjacent. The press state is
    // deeper than the hover state because press is a decision, not a question.
    //
    // `durationFast` is 150 ms, which is the top of the 100-150 ms band the
    // design docs call "fast" for hover and press. Instant (50 ms) was what
    // this used, and at 50 ms the grow reads as a flicker rather than a
    // response.
    readonly property real hoverScale: 1.02
    readonly property real pressScale: 0.96

    scale: !root.enabled ? 1.0
                             : (mouseArea.pressed ? root.pressScale
                                : (mouseArea.containsMouse ? root.hoverScale : 1.0))
    Behavior on scale {
        NumberAnimation {
            duration: Theme.durationFast
            // OutCubic on the way up, InCubic on the way down. The asymmetry is
            // what makes it feel like an object rather than a slider.
            easing.type: root.pressed ? Easing.InCubic : Easing.OutCubic
        }
    }

    RowLayout {
        id: buttonLayout
        anchors.centerIn: parent
        spacing: root.text !== "" && root.icon !== "" ? Theme.spacingSmall : 0

        Image {
            visible: root.icon !== ""
            source: root.icon
            sourceSize.width: Theme.iconMedium
            sourceSize.height: Theme.iconMedium
            Layout.preferredWidth: Theme.iconMedium
            Layout.preferredHeight: Theme.iconMedium
            fillMode: Image.PreserveAspectFit

        layer.enabled: true
        layer.effect: MultiEffect {
            autoPaddingEnabled: true
            colorization: 1.0
            colorizationColor: root.enabled ? (root.highlighted ? Theme.textOnAccent : Theme.textPrimary) : Theme.textDisabled
        }
        }

        Text {
            visible: root.text !== ""
            text: root.text
            color: root.enabled ? (root.highlighted ? Theme.textOnAccent : Theme.textPrimary) : Theme.textDisabled
            font: Theme.fontBody
        }
    }

    // The MouseArea is left enabled when the button is not, so that its
    // cursorShape can still say "not now". That means `pressed` and
    // `containsMouse` are both live on a DISABLED button, and both have to be
    // gated on root.enabled above or a disabled button still reacts.

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: root.enabled ? Qt.PointingHandCursor : Qt.ForbiddenCursor
        onClicked: {
            root.forceActiveFocus()
            if (root.enabled) root.clicked()
        }
        onPressAndHold: { if (root.enabled) root.pressAndHold() }
        onReleased: { if (root.enabled) root.released() }
    }

    Keys.onReturnPressed: {
        if (root.enabled)
            root.clicked()
    }

    Keys.onSpacePressed: {
        if (root.enabled)
            root.clicked()
    }
}
