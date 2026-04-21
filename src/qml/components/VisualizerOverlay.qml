import QtQuick
import ChadVis

Item {
    id: root
    anchors.fill: parent
    
    // Modern Visualizer Overlay with reactive text and glowing aesthetics
    Repeater {
        model: OverlayBridge.overlays
        
        delegate: Item {
            id: overlayItem
            x: modelData.x * root.width - overlayText.width / 2
            y: modelData.y * root.height - overlayText.height / 2
            
            width: overlayText.width
            height: overlayText.height
            opacity: modelData.opacity
            
            // Glow backdrop for modern aesthetic
            Rectangle {
                anchors.centerIn: parent
                width: overlayText.width + 20
                height: overlayText.height + 10
                radius: 10
                color: modelData.color
                opacity: 0.15
                visible: modelData.glow || false
                
                // Reactive scale based on audio intensity (pseudo-reactive for now)
                scale: 1.0 + (AudioBridge.isPlaying ? 0.05 * Math.sin(Date.now() / 100) : 0)
            }
            
            Text {
                id: overlayText
                text: modelData.text
                color: modelData.color
                font.pixelSize: modelData.fontSize
                font.bold: modelData.bold
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
            
            // Animation logic based on modelData.animation
            SequentialAnimation {
                running: modelData.animation === 1 // Fade Pulse
                loops: Animation.Infinite
                NumberAnimation { target: overlayItem; property: "opacity"; from: modelData.opacity; to: modelData.opacity * 0.3; duration: 1000; easing.type: Easing.InOutQuad }
                NumberAnimation { target: overlayItem; property: "opacity"; from: modelData.opacity * 0.3; to: modelData.opacity; duration: 1000; easing.type: Easing.InOutQuad }
            }
            
            NumberAnimation {
                running: modelData.animation === 2 // Scroll Left
                target: overlayItem
                property: "x"
                from: root.width
                to: -overlayText.width
                duration: 5000
                loops: Animation.Infinite
            }
            
            NumberAnimation {
                running: modelData.animation === 3 // Scroll Right
                target: overlayItem
                property: "x"
                from: -overlayText.width
                to: root.width
                duration: 5000
                loops: Animation.Infinite
            }
            
            SequentialAnimation {
                running: modelData.animation === 4 // Bounce
                loops: Animation.Infinite
                NumberAnimation { target: overlayItem; property: "y"; from: modelData.y * root.height - overlayText.height / 2; to: (modelData.y - 0.05) * root.height - overlayText.height / 2; duration: 500; easing.type: Easing.OutQuad }
                NumberAnimation { target: overlayItem; property: "y"; from: (modelData.y - 0.05) * root.height - overlayText.height / 2; to: modelData.y * root.height - overlayText.height / 2; duration: 500; easing.type: Easing.InQuad }
            }
        }
    }
}
