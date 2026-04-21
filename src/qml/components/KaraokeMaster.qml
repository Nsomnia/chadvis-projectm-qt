import QtQuick
import QtQuick.Layouts
import QtQuick.Effects
import ChadVis

Item {
    id: root

    property alias title: titleLabel.text
    property alias artist: artistLabel.text
    property color accentColor: Theme.accent
    property color textColor: Theme.onSurface
    property bool showGlow: true
    property real verticalPosition: 0.8 // 0.0 to 1.0 (bottom heavy by default)

    implicitHeight: 200
    implicitWidth: 600

    // Backdrop for better legibility on busy visualizers
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: "transparent" }
            GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.6) }
        }
        visible: LyricsBridge.hasLyrics
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingLarge
        spacing: Theme.spacingMedium
        
        // Metadata (Top left/right)
        RowLayout {
            Layout.fillWidth: true
            opacity: LyricsBridge.hasLyrics ? 0.7 : 0.0
            Behavior on opacity { NumberAnimation { duration: 500 } }

            ColumnLayout {
                spacing: 0
                Label {
                    id: titleLabel
                    text: LyricsBridge.title
                    color: root.textColor
                    font.pixelSize: Theme.fontSizeMedium
                    font.bold: true
                }
                Label {
                    id: artistLabel
                    text: LyricsBridge.artist
                    color: root.textColor
                    font.pixelSize: Theme.fontSizeSmall
                    opacity: 0.8
                }
            }
            Item { Layout.fillWidth: true }
            
            // Visual feedback of progress
            Rectangle {
                width: 100
                height: 4
                color: Qt.rgba(1, 1, 1, 0.2)
                radius: 2
                Rectangle {
                    width: LyricsBridge.lineProgress * parent.width
                    height: parent.height
                    color: root.accentColor
                    radius: 2
                }
            }
        }

        Item { Layout.fillHeight: true }

        // The Main Event: Karaoke Text
        Item {
            id: lyricsContainer
            Layout.fillWidth: true
            Layout.preferredHeight: 120
            
            ColumnLayout {
                anchors.centerIn: parent
                width: parent.width
                spacing: Theme.spacingSmall

                // Previous line (dimmed)
                Label {
                    id: prevLine
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: LyricsBridge.currentLineIndex > 0 ? LyricsBridge.getLine(LyricsBridge.currentLineIndex - 1).text : ""
                    color: root.textColor
                    opacity: 0.3
                    font.pixelSize: Theme.fontSizeMedium
                    font.italic: true
                    visible: text !== ""
                }

                // Current line (Master)
                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: mainText.implicitHeight + 20
                    
                    Text {
                        id: mainText
                        anchors.centerIn: parent
                        text: LyricsBridge.currentLineIndex >= 0 ? LyricsBridge.getLine(LyricsBridge.currentLineIndex).text : "♫ CHADVIS VISUALIZER ♫"
                        color: root.textColor
                        font.pixelSize: Theme.fontSizeXXL
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                        
                        // Reflection/Glow Effect
                        layer.enabled: root.showGlow
                        layer.effect: MultiEffect {
                            autoPaddingEnabled: true
                            shadowEnabled: true
                            shadowColor: root.accentColor
                            shadowBlur: 0.8
                            shadowOpacity: LyricsBridge.lineProgress * 0.5
                            blurEnabled: true
                            blur: LyricsBridge.lineProgress * 0.2
                            brightness: LyricsBridge.lineProgress * 0.1
                        }

                        // Progressive Color Overlay (Word levelish if possible via shader, or simple for now)
                        Rectangle {
                            anchors.left: parent.left
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            width: parent.width * LyricsBridge.lineProgress
                            color: root.accentColor
                            opacity: 0.4
                            visible: LyricsBridge.hasLyrics
                            
                            layer.enabled: true
                            layer.effect: MultiEffect {
                                maskEnabled: true
                                maskSource: mainText
                            }
                        }
                    }
                }

                // Next line (upcoming)
                Label {
                    id: nextLine
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: LyricsBridge.currentLineIndex < LyricsBridge.lines.length - 1 ? LyricsBridge.getLine(LyricsBridge.currentLineIndex + 1).text : ""
                    color: root.textColor
                    opacity: 0.6
                    font.pixelSize: Theme.fontSizeLarge
                    visible: text !== ""
                }
            }
        }
        
        // Bottom Spacer controlled by verticalPosition
        Item { 
            Layout.preferredHeight: parent.height * (1.0 - root.verticalPosition)
        }
    }

    // Instrumental Break Overlay
    Rectangle {
        anchors.centerIn: parent
        width: 300
        height: 60
        color: Qt.rgba(0, 0, 0, 0.4)
        radius: 30
        visible: LyricsBridge.isInstrumental
        border.color: root.accentColor
        border.width: 1

        RowLayout {
            anchors.centerIn: parent
            spacing: Theme.spacingMedium
            Label {
                text: "♪ INSTRUMENTAL ♪"
                color: root.accentColor
                font.bold: true
                font.letterSpacing: 2
            }
            // Simple animated dots
            Row {
                spacing: 4
                Repeater {
                    model: 3
                    Rectangle {
                        width: 6; height: 6; radius: 3
                        color: root.accentColor
                        opacity: 0.3 + (0.7 * Math.abs(Math.sin(Date.now() / 500 + index)))
                    }
                }
            }
        }
    }
}
