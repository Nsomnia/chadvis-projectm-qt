import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import ChadVis
import "../components"

ColumnLayout {
    id: root
    spacing: Theme.spacingMedium

    // ═══════════════════════════════════════════════════════════
    // GENERATION UI (CHAD CREATION SUITE)
    // ═══════════════════════════════════════════════════════════
    
    Text {
        text: "Create Magic"
        color: Theme.accent
        font: Theme.fontSubtitle
    }

    TextField {
        id: promptInput
        Layout.fillWidth: true
        placeholderText: "Describe your vibe..."
        background: Rectangle { 
            color: Theme.surfaceRaised
            radius: Theme.radiusSmall
            border.color: parent.activeFocus ? Theme.accent : Theme.border
        }
    }

    RowLayout {
        Layout.fillWidth: true
        TextField {
            id: styleInput
            Layout.fillWidth: true
            placeholderText: "Style/Tags..."
            background: Rectangle { 
                color: Theme.surfaceRaised
                radius: Theme.radiusSmall
                border.color: parent.activeFocus ? Theme.accent : Theme.border
            }
        }
        
        ComboBox {
            id: modelSelector
            model: ["v4 (Advanced)", "v3.5 (Classic)", "v3.0 (Legacy)"]
            background: Rectangle { color: Theme.surfaceRaised; radius: Theme.radiusSmall; border.color: Theme.border }
        }
    }

    AppButton {
        text: "Generate Song"
        Layout.fillWidth: true
        onClicked: {
            console.log("SunoBridge: Generating with model: " + modelSelector.currentText)
        }
    }

    // ═══════════════════════════════════════════════════════════
    // LIBRARY VIEW
    // ═══════════════════════════════════════════════════════════

    Text {
        text: "Your Library"
        color: Theme.accent
        font: Theme.fontSubtitle
        Layout.topMargin: Theme.spacingMedium
    }

    ListView {
        id: libraryList
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        model: SunoBridge.clips
        
        delegate: ItemDelegate {
            width: libraryList.width
            height: 60
            
            contentItem: RowLayout {
                spacing: Theme.spacingMedium
                
                Rectangle {
                    width: 48; height: 48
                    radius: Theme.radiusSmall
                    color: Theme.surfaceRaised
                    Image {
                        anchors.fill: parent
                        source: modelData.image_url || ""
                        sourceSize: Qt.size(48, 48)
                        fillMode: Image.PreserveAspectCrop
                    }
                }
                
                Column {
                    Layout.fillWidth: true
                    Text { text: modelData.title || "Untitled"; color: Theme.textPrimary; font: Theme.fontBody }
                    Text { text: modelData.metadata.tags || ""; color: Theme.textSecondary; font: Theme.fontCaption }
                }

                Text {
                    text: modelData.status === "complete" ? "Ready" : "Creating..."
                    color: modelData.status === "complete" ? Theme.accent : Theme.textSecondary
                    font: Theme.fontCaption
                }
            }
        }

        // Infinite Scrolling Implementation
        onAtYEndChanged: {
            if (atYEnd && SunoBridge.clips.length < SunoBridge.totalClips) {
                console.log("SunoBridge: Fetching next page...")
                SunoBridge.refreshLibrary(Math.floor(SunoBridge.clips.length / 20) + 2)
            }
        }
    }
}
