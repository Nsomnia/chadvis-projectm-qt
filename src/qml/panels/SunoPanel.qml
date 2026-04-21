import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import ChadVis
import "../components"

ColumnLayout {
    id: root
    spacing: Theme.spacingMedium

    // ═══════════════════════════════════════════════════════════
    // NAVIGATION TABS (B-SIDE TOGGLE)
    // ═══════════════════════════════════════════════════════════
    RowLayout {
        Layout.fillWidth: true
        spacing: 0
        
        AppButton {
            text: "Create"
            Layout.fillWidth: true
            highlighted: modeStack.currentIndex === 0
            onClicked: modeStack.currentIndex = 0
        }
        AppButton {
            text: "B-Side Chat"
            Layout.fillWidth: true
            highlighted: modeStack.currentIndex === 1
            onClicked: modeStack.currentIndex = 1
        }
    }

    StackLayout {
        id: modeStack
        Layout.fillWidth: true
        Layout.fillHeight: true
        currentIndex: 0

        // ═══════════════════════════════════════════════════════════
        // TAB 1: GENERATION & LIBRARY
        // ═══════════════════════════════════════════════════════════
        ColumnLayout {
            spacing: Theme.spacingMedium
            
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
                    SunoBridge.generate(promptInput.text, styleInput.text, false, modelSelector.currentText)
                    promptInput.text = ""
                    console.log("SunoBridge: Generating with model: " + modelSelector.currentText)
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: Theme.spacingMedium
                
                Text {
                    text: "Your Library"
                    color: Theme.accent
                    font: Theme.fontSubtitle
                    Layout.fillWidth: true
                }

                TextField {
                    id: searchBar
                    placeholderText: "Search library..."
                    Layout.preferredWidth: 150
                    font: Theme.fontCaption
                    color: Theme.textPrimary
                    background: Rectangle {
                        color: Theme.surfaceRaised
                        radius: Theme.radiusSmall
                        border.color: parent.activeFocus ? Theme.accent : Theme.border
                    }
                    onTextChanged: SunoBridge.filterText = text
                }
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

                onAtYEndChanged: {
                    if (atYEnd && SunoBridge.clips.length < SunoBridge.totalClips && searchBar.text === "") {
                        SunoBridge.refreshLibrary(Math.floor(SunoBridge.clips.length / 20) + 2)
                    }
                }
            }
        }

        // ═══════════════════════════════════════════════════════════
        // TAB 2: B-SIDE CHAD ORCHESTRATOR
        // ═══════════════════════════════════════════════════════════
        ColumnLayout {
            spacing: Theme.spacingSmall

            Text {
                text: "B-Side Orchestrator (Experimental)"
                color: Theme.accent
                font: Theme.fontSubtitle
            }

            ListView {
                id: chatList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: SunoBridge.chatHistory
                spacing: Theme.spacingSmall
                
                delegate: ColumnLayout {
                    width: chatList.width
                    spacing: 2
                    
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: chatText.implicitHeight + 16
                        color: modelData.role === "user" ? Theme.surfaceRaised : Theme.surface
                        radius: Theme.radiusSmall
                        border.color: modelData.role === "user" ? Theme.accent : "transparent"
                        opacity: modelData.role === "user" ? 1.0 : 0.8

                        Text {
                            id: chatText
                            anchors.fill: parent
                            anchors.margins: 8
                            text: modelData.content
                            color: Theme.textPrimary
                            wrapMode: Text.WordWrap
                            font: Theme.fontBody
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                TextField {
                    id: chatInput
                    Layout.fillWidth: true
                    placeholderText: "Command the orchestrator..."
                    background: Rectangle { 
                        color: Theme.surfaceRaised
                        radius: Theme.radiusSmall
                        border.color: parent.activeFocus ? Theme.accent : Theme.border
                    }
                    onAccepted: sendBtn.clicked()
                }
                
                AppButton {
                    id: sendBtn
                    text: "Send"
                    onClicked: {
                        if (chatInput.text.trim() !== "") {
                            SunoBridge.sendChatMessage(chatInput.text)
                            chatInput.text = ""
                        }
                    }
                }
            }
        }
    }
}
