import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import ChadVis
import "../components"

ColumnLayout {
    id: root
    spacing: Theme.spacingMedium

    Text {
        text: "Create Magic"
        color: Theme.accent
        font: Theme.fontSubtitle
    }

    AppTextField {
        id: promptInput
        Layout.fillWidth: true
        placeholderText: "Describe your vibe..."
    }

    RowLayout {
        Layout.fillWidth: true

        AppTextField {
            id: styleInput
            Layout.fillWidth: true
            placeholderText: "Style/Tags..."
        }

        AppComboBox {
            id: modelSelector
            Layout.preferredWidth: 190
            model: SunoBridge.models
            textRole: "name"
            valueRole: "external_key"
            enabled: SunoBridge.isAuthenticated && count > 0
        }
    }

    Text {
        Layout.fillWidth: true
        visible: SunoBridge.models.length === 0
        text: SunoBridge.isAuthenticated
              ? "Loading available models from your Suno session…"
              : "Sign in with a bearer token or session cookie to load available models."
        color: Theme.textSecondary
        font: Theme.fontCaption
        wrapMode: Text.WordWrap
    }

    Text {
        Layout.fillWidth: true
        visible: SunoBridge.generationStatus.length > 0
        text: SunoBridge.generationStatus
        color: Theme.warning
        font: Theme.fontCaption
        wrapMode: Text.WordWrap
    }

    AppButton {
        Layout.fillWidth: true
        text: SunoBridge.generationAvailable ? "Generate Song" : "Generation unavailable"
        enabled: SunoBridge.generationAvailable
                 && SunoBridge.isAuthenticated
                 && SunoBridge.models.length > 0
                 && promptInput.text.trim().length > 0
        onClicked: SunoBridge.generate(
                      promptInput.text,
                      styleInput.text,
                      false,
                      modelSelector.currentValue)
        ToolTip.visible: hovered
        ToolTip.text: SunoBridge.generationStatus
        ToolTip.delay: 300
    }

    Text {
        Layout.fillWidth: true
        visible: SunoBridge.errorMessage.length > 0
        text: SunoBridge.errorMessage
        color: Theme.error
        font: Theme.fontCaption
        wrapMode: Text.WordWrap
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

        AppTextField {
            id: searchBar
            placeholderText: "Search library..."
            Layout.preferredWidth: 170
            font: Theme.fontCaption
            color: Theme.textPrimary
            text: SunoBridge.filterText
            onTextChanged: {
                if (text !== SunoBridge.filterText)
                    SunoBridge.searchLibrary(text)
            }
        }
    }

    ListView {
        id: libraryList
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        model: SunoBridge.clips

        delegate: ItemDelegate {
            Layout.fillWidth: true
            height: 60

            contentItem: RowLayout {
                spacing: Theme.spacingMedium

                Rectangle {
                    Layout.preferredWidth: 48
                    Layout.preferredHeight: 48
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

                    Text {
                        width: parent.width
                        text: modelData.title || "Untitled"
                        color: Theme.textPrimary
                        font: Theme.fontBody
                        elide: Text.ElideRight
                    }

                    Text {
                        width: parent.width
                        text: [modelData.model_name, modelData.duration].filter(Boolean).join(" · ")
                        color: Theme.textSecondary
                        font: Theme.fontCaption
                        elide: Text.ElideRight
                        visible: text.length > 0
                    }
                }

                Text {
                    text: modelData.status === "complete" ? "Ready" : "Creating…"
                    color: modelData.status === "complete" ? Theme.accent : Theme.textSecondary
                    font: Theme.fontCaption
                }
            }
        }

        onAtYEndChanged: {
            if (atYEnd && SunoBridge.hasMorePages && !SunoBridge.loading)
                SunoBridge.requestNextLibraryPage()
        }

        footer: Item {
            width: libraryList.width
            height: SunoBridge.loading ? 36 : 0
            visible: SunoBridge.loading

            RowLayout {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.verticalCenter: parent.verticalCenter

                BusyIndicator {
                    running: SunoBridge.loading
                    Layout.preferredWidth: 20
                    Layout.preferredHeight: 20
                }

                Text {
                    text: "Loading more…"
                    color: Theme.textSecondary
                    font: Theme.fontCaption
                }
            }
        }
    }
}
