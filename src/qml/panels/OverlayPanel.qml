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
        background: Rectangle { color: Theme.surfaceRaised }

        RowLayout {
            anchors.fill: parent
            spacing: Theme.spacingSmall

            Label {
                text: "Text Overlays"
                color: Theme.textPrimary
                font.pixelSize: Theme.fontBody.pixelSize
                font.bold: true
            }

            Item { Layout.fillWidth: true }

            ToolButton {
                text: "+"
                onClicked: addOverlayDialog.open()
                Accessible.name: "Add a text overlay"
            }
        }
    }

    ListView {
        id: overlayList
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true

        model: OverlayBridge.overlays
        Accessible.role: Accessible.List
        Accessible.name: "Text overlays"
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
            color: Theme.textPrimaryVariant
            font.pixelSize: Theme.fontBody.pixelSize
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
                color: Theme.textPrimary
            }

            TextField {
                id: textInput
                Layout.fillWidth: true
                placeholderText: "Overlay text..."
                Accessible.name: "Overlay text"
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
                    color: Theme.textPrimaryVariant
                }

                Slider {
                    id: xSlider
                    Layout.fillWidth: true
                    Accessible.name: "Horizontal position"
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
                    color: Theme.textPrimaryVariant
                }

                SpinBox {
                    from: 12
                    to: 72
                    Accessible.name: "Font size"
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
                    Accessible.name: "Bold"
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
                    color: Theme.textPrimaryVariant
                }

                Rectangle {
                    width: 32
                    height: 32
                    radius: Theme.radiusSmall
                    color: root.selectedOverlay >= 0 ? OverlayBridge.overlays[root.selectedOverlay].color : "#FFFFFF"
                    border.color: Theme.border

                    // A colour swatch that opens a dialog: a button, and the
                    // only way in was the mouse.
                    activeFocusOnTab: true
                    Accessible.role: Accessible.Button
                    Accessible.name: "Overlay colour"
                    Accessible.onPressAction: colorDialog.open()

                    Keys.onReturnPressed: colorDialog.open()
                    Keys.onSpacePressed: colorDialog.open()

                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            parent.forceActiveFocus()
                            colorDialog.open()
                        }
                    }
                }

                Label {
                    text: "Opacity:"
                    color: Theme.textPrimaryVariant
                }

                Slider {
                    Layout.fillWidth: true
                    Accessible.name: "Opacity"
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
                Accessible.name: "Animation"
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
                    color: Theme.surfaceRaised
                    border.color: Theme.border
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
                Accessible.name: "New overlay text"
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
                    border.color: Theme.border

                    activeFocusOnTab: true
                    Accessible.role: Accessible.Button
                    Accessible.name: "Use colour " + modelData
                    Accessible.onPressAction: pickColour()

                    Keys.onReturnPressed: pickColour()
                    Keys.onSpacePressed: pickColour()

                    function pickColour() {
                        if (root.selectedOverlay >= 0) {
                            let overlay = OverlayBridge.overlays[root.selectedOverlay]
                            overlay.color = modelData
                            OverlayBridge.updateOverlay(root.selectedOverlay, overlay)
                        }
                        colorDialog.close()
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: parent.pickColour()
                    }
                }
            }
        }
    }

    component OverlayDelegate: Rectangle {
        id: delegate
        height: 48
        color: isSelected ? Theme.accent : (mouseArea.containsMouse ? Theme.surfaceRaised : Theme.surface)
        opacity: isSelected ? 0.2 : 1.0
        radius: Theme.radiusSmall

        property bool isSelected: false

        signal selectClicked()
        signal deleteClicked()

        // Whole-row MouseArea declared BEFORE the controls: later siblings
        // stack above it, so the delete button takes its own click. Declared
        // last, this full-bleed area sat on top of the button and ate it —
        // ClipCard.qml documents the trap.
        MouseArea {
            id: mouseArea
            anchors.fill: parent
            hoverEnabled: true
            onClicked: delegate.selectClicked()
        }

        // The row is the selection target, so it is a button named by the overlay
        // it edits, reachable without a mouse.
        activeFocusOnTab: true
        Accessible.role: Accessible.Button
        Accessible.name: "Edit overlay: "
                         + (modelData && modelData.text ? modelData.text : "untitled overlay")
        Accessible.onPressAction: delegate.selectClicked()

        Keys.onReturnPressed: delegate.selectClicked()
        Keys.onSpacePressed: delegate.selectClicked()

        RowLayout {
            anchors.fill: parent
            anchors.margins: Theme.spacingSmall
            spacing: Theme.spacingSmall

            Text {
                text: modelData ? modelData.text : ""
                color: Theme.textPrimary
                font.pixelSize: Theme.fontBody.pixelSize
                elide: Text.ElideRight
                Layout.fillWidth: true
            }

	AppButton {
		icon: "qrc:/qt/qml/ChadVis/resources/icons/delete.svg"
		implicitWidth: 36
		implicitHeight: 36
		onClicked: delegate.deleteClicked()
		Accessible.name: "Delete overlay: "
		                 + (modelData && modelData.text ? modelData.text : "untitled overlay")
	}
        }

    }
}
