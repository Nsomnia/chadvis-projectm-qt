import QtQuick
import QtQuick.Effects
import ChadVis

Item {
    id: root
    anchors.fill: parent

    // Overlays are persisted as JSON and read back as a plain map (OverlayBridge).
    // Every field below is therefore `modelData.<key>` over data whose schema can
    // drift -- a renamed or missing key yields `undefined`, and `undefined * 2`
    // is NaN, which Qt Quick turns into an invisible item at a nonsense position
    // rather than an error. One defaulted binding per field is the cheap guard
    // (T0077); the Repeater itself still renders whatever the bridge publishes.
    Repeater {
        model: OverlayBridge.overlays

        delegate: Item {
            id: overlayItem
            readonly property var overlay: modelData || ({})
            readonly property real posX: overlay.x ?? 0.5
            readonly property real posY: overlay.y ?? 0.5
            readonly property real baseOpacity: overlay.opacity ?? 1.0
            readonly property color textColor: overlay.color ?? "#ffffff"
            readonly property bool glowing: overlay.glow === true
            readonly property int animation: overlay.animation ?? 0

            x: posX * root.width - overlayText.width / 2
            y: posY * root.height - overlayText.height / 2

            width: overlayText.width
            height: overlayText.height
            opacity: baseOpacity

            // Glow backdrop for modern aesthetic
            Rectangle {
                anchors.centerIn: parent
                width: overlayText.width + 20
                height: overlayText.height + 10
                radius: 10
                color: overlayItem.textColor
                opacity: 0.15
                visible: overlayItem.glowing

                // A real animation, not `Math.sin(Date.now() ...)`: Date.now() is
                // not a bindable property, so that expression was evaluated once
                // and the "reactive" glow was frozen at whatever the clock said
                // when the delegate was created (T0078).
                scale: 1.0
                SequentialAnimation on scale {
                    running: overlayItem.glowing && AudioBridge.isPlaying
                    loops: Animation.Infinite
                    NumberAnimation { to: 1.05; duration: 500; easing.type: Easing.InOutSine }
                    NumberAnimation { to: 1.0; duration: 500; easing.type: Easing.InOutSine }
                }
            }

            Text {
                id: overlayText
                text: overlayItem.overlay.text ?? ""
                color: overlayItem.textColor
                font.pixelSize: overlayItem.overlay.fontSize ?? 32
                font.bold: overlayItem.overlay.bold === true
                font.family: "Inter, Roboto, sans-serif"

                style: Text.Outline
                styleColor: Qt.rgba(0, 0, 0, 0.5)

                // Optional shadow for readability
                layer.enabled: true
                layer.effect: MultiEffect {
                    autoPaddingEnabled: true
                    shadowEnabled: true
                    shadowColor: "black"
                    shadowBlur: 0.5
                    shadowHorizontalOffset: 2
                    shadowVerticalOffset: 2
                }
            }

            // Animation logic based on the overlay's animation index. Each
            // animation drives `overlayItem` directly, so the base bindings above
            // are the resting state and the animation is the only writer while it
            // runs.
            SequentialAnimation {
                running: overlayItem.animation === 1 // Fade Pulse
                loops: Animation.Infinite
                NumberAnimation { target: overlayItem; property: "opacity"; from: overlayItem.baseOpacity; to: overlayItem.baseOpacity * 0.3; duration: 1000; easing.type: Easing.InOutQuad }
                NumberAnimation { target: overlayItem; property: "opacity"; from: overlayItem.baseOpacity * 0.3; to: overlayItem.baseOpacity; duration: 1000; easing.type: Easing.InOutQuad }
            }

            NumberAnimation {
                running: overlayItem.animation === 2 // Scroll Left
                target: overlayItem
                property: "x"
                from: root.width
                to: -overlayText.width
                duration: 5000
                loops: Animation.Infinite
            }

            NumberAnimation {
                running: overlayItem.animation === 3 // Scroll Right
                target: overlayItem
                property: "x"
                from: -overlayText.width
                to: root.width
                duration: 5000
                loops: Animation.Infinite
            }

            SequentialAnimation {
                running: overlayItem.animation === 4 // Bounce
                loops: Animation.Infinite
                NumberAnimation { target: overlayItem; property: "y"; from: overlayItem.posY * root.height - overlayText.height / 2; to: (overlayItem.posY - 0.05) * root.height - overlayText.height / 2; duration: 500; easing.type: Easing.OutQuad }
                NumberAnimation { target: overlayItem; property: "y"; from: (overlayItem.posY - 0.05) * root.height - overlayText.height / 2; to: overlayItem.posY * root.height - overlayText.height / 2; duration: 500; easing.type: Easing.InQuad }
            }
        }
    }
}
