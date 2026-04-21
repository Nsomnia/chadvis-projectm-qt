import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import ChadVis
import "../components"

ColumnLayout {
    id: root

    property int selectedOverlay: -1

    spacing: 0

    ToolBar {
        Layout.fillWidth: true
        background: Rectangle { color: Theme.surfaceVariant }

        RowLayout {
            anchors.fill: parent
            spacing: Theme.spacingSmall

            Label {
                text: "Text Overlays"
                color: Theme.onSurface
                font.pixelSize: Theme.fontSizeMedium
                font.bold: true
            }

            Item { Layout.fillWidth: true }

            ToolButton {
                text: "+"
                onClicked: addOverlayDialog.open()
            }
        }
    }

    ListView {
        id: overlayList
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true

        model: OverlayBridge.overlays
        delegate: OverlayDelegate {
            width: overlayList.width
            isSelected: index === root.selectedOverlay
            onSelectClicked: root.selectedOverlay = index
            onDeleteClicked: OverlayBridge.removeOverlay(index)
        }

        Label {
            anchors.centerIn: parent
            visible: OverlayBridge.overlays.length === 0
            text: "No overlays.\nClick + to add."
            color: Theme.onSurfaceVariant
            font.pixelSize: Theme.fontSizeMedium
            horizontalAlignment: Text.AlignHCenter
        }

        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AsNeeded
        }
    }

    Pane {
        Layout.fillWidth: true
        visible: root.selectedOverlay >= 0
        background: Rectangle { color: Theme.surface }

        ColumnLayout {
            anchors.fill: parent
            spacing: Theme.spacingSmall

            Label {
                text: "Edit Overlay"
                font.bold: true
                color: Theme.onSurface
            }

            TextField {
                id: textInput
                Layout.fillWidth: true
                placeholderText: "Overlay text..."
                text: root.selectedOverlay >= 0 ? OverlayBridge.overlays[root.selectedOverlay].text : ""
                onTextChanged: {
                    if (root.selectedOverlay >= 0 && activeFocus) {
                        let overlay = OverlayBridge.overlays[root.selectedOverlay]
                        overlay.text = text
                        OverlayBridge.updateOverlay(root.selectedOverlay, overlay)
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingMedium

                Label {
                    text: "Position:"
                    color: Theme.onSurfaceVariant
                }

                Slider {
                    id: xSlider
                    Layout.fillWidth: true
                    from: 0
                    to: 100
                    value: root.selectedOverlay >= 0 ? OverlayBridge.overlays[root.selectedOverlay].x * 100 : 0
                    onValueChanged: {
                        if (root.selectedOverlay >= 0 && activeFocus) {
                            let overlay = OverlayBridge.overlays[root.selectedOverlay]
                            overlay.x = value / 100
                            OverlayBridge.updateOverlay(root.selectedOverlay, overlay)
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingMedium

                Label {
                    text: "Font Size:"
                    color: Theme.onSurfaceVariant
                }

                SpinBox {
                    from: 12
                    to: 72
                    value: root.selectedOverlay >= 0 ? OverlayBridge.overlays[root.selectedOverlay].fontSize : 24
                    onValueChanged: {
                        if (root.selectedOverlay >= 0 && activeFocus) {
                            let overlay = OverlayBridge.overlays[root.selectedOverlay]
                            overlay.fontSize = value
                            OverlayBridge.updateOverlay(root.selectedOverlay, overlay)
                        }
                    }
                }

                CheckBox {
                    text: "Bold"
                    checked: root.selectedOverlay >= 0 ? OverlayBridge.overlays[root.selectedOverlay].bold : false
                    onCheckedChanged: {
                        if (root.selectedOverlay >= 0) {
                            let overlay = OverlayBridge.overlays[root.selectedOverlay]
                            overlay.bold = checked
                            OverlayBridge.updateOverlay(root.selectedOverlay, overlay)
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingMedium

                Label {
                    text: "Color:"
                    color: Theme.onSurfaceVariant
                }

                Rectangle {
                    width: 32
                    height: 32
                    radius: Theme.radiusSmall
                    color: root.selectedOverlay >= 0 ? OverlayBridge.overlays[root.selectedOverlay].color : "#FFFFFF"
                    border.color: Theme.outline

                    MouseArea {
                        anchors.fill: parent
                        onClicked: colorDialog.open()
                    }
                }

                Label {
                    text: "Opacity:"
                    color: Theme.onSurfaceVariant
                }

                Slider {
                    Layout.fillWidth: true
                    from: 0
                    to: 100
                    value: root.selectedOverlay >= 0 ? OverlayBridge.overlays[root.selectedOverlay].opacity * 100 : 100
                    onValueChanged: {
                        if (root.selectedOverlay >= 0 && activeFocus) {
                            let overlay = OverlayBridge.overlays[root.selectedOverlay]
                            overlay.opacity = value / 100
                            OverlayBridge.updateOverlay(root.selectedOverlay, overlay)
                        }
                    }
                }
            }

            ComboBox {
                id: animationCombo
                Layout.fillWidth: true
                model: ["None", "Fade Pulse", "Scroll Left", "Scroll Right", "Bounce"]
                currentIndex: root.selectedOverlay >= 0 ? OverlayBridge.overlays[root.selectedOverlay].animation : 0
                onActivated: {
                    if (root.selectedOverlay >= 0) {
                        let overlay = OverlayBridge.overlays[root.selectedOverlay]
                        overlay.animation = index
                        OverlayBridge.updateOverlay(root.selectedOverlay, overlay)
                    }
                }

                background: Rectangle {
                    radius: Theme.radiusSmall
                    color: Theme.surfaceVariant
                    border.color: Theme.outline
                }
            }
        }
    }

    Dialog {
        id: addOverlayDialog
        title: "Add Text Overlay"
        standardButtons: Dialog.Ok | Dialog.Cancel

        ColumnLayout {
            anchors.fill: parent
            spacing: Theme.spacingMedium

            TextField {
                id: newOverlayText
                Layout.fillWidth: true
                placeholderText: "Enter text..."
            }
        }

        onAccepted: {
            OverlayBridge.addOverlay(newOverlayText.text || "New Overlay")
            root.selectedOverlay = OverlayBridge.overlays.length - 1
            newOverlayText.clear()
        }
    }

    Dialog {
        id: colorDialog
        title: "Choose Color"
        standardButtons: Dialog.Ok | Dialog.Cancel

        GridLayout {
            columns: 6
            Repeater {
                model: ["#FFFFFF", "#FF0000", "#00FF00", "#0000FF",
                        "#FFFF00", "#FF00FF", "#00FFFF", "#FFA500",
                        "#800080", "#008000", "#000080", "#800000"]

                Rectangle {
                    width: 32
                    height: 32
                    radius: Theme.radiusSmall
                    color: modelData
                    border.color: Theme.outline

                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            if (root.selectedOverlay >= 0) {
                                let overlay = OverlayBridge.overlays[root.selectedOverlay]
                                overlay.color = modelData
                                OverlayBridge.updateOverlay(root.selectedOverlay, overlay)
                            }
                            colorDialog.close()
                        }
                    }
                }
            }
        }
    }

    component OverlayDelegate: Rectangle {
        id: delegate
        height: 48
        color: isSelected ? Theme.primary : (mouseArea.containsMouse ? Theme.surfaceVariant : Theme.surface)
        opacity: isSelected ? 0.2 : 1.0
        radius: Theme.radiusSmall

        property bool isSelected: false

        signal selectClicked()
        signal deleteClicked()

        RowLayout {
            anchors.fill: parent
            anchors.margins: Theme.spacingSmall
            spacing: Theme.spacingSmall

            Text {
                text: modelData ? modelData.text : ""
                color: Theme.onSurface
                font.pixelSize: Theme.fontSizeMedium
                elide: Text.ElideRight
                Layout.fillWidth: true
            }

	AppButton {
		icon: "qrc:/qt/qml/ChadVis/resources/icons/delete.svg"
		implicitWidth: 36
		implicitHeight: 36
		onClicked: delegate.deleteClicked()
	}
        }

        MouseArea {
            id: mouseArea
            anchors.fill: parent
            hoverEnabled: true
            onClicked: delegate.selectClicked()
        }
    }
}
