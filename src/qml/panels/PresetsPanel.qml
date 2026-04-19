import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import Qt5Compat.GraphicalEffects
import ChadVis
import "../components"

ColumnLayout {
    id: root

    property string searchQuery: ""
    property string selectedCategory: ""

    spacing: Theme.spacingMedium

    ColumnLayout {
        Layout.fillWidth: true
        Layout.margins: Theme.spacingSmall
        spacing: Theme.spacingSmall

        TextField {
            id: searchField
            Layout.fillWidth: true
            placeholderText: "Search presets..."
            text: root.searchQuery
            onTextChanged: root.searchQuery = text

            background: Rectangle {
                radius: Theme.radiusSmall
                color: Theme.surfaceVariant
                border.color: Theme.outline
                border.width: searchField.activeFocus ? 2 : 1
            }

            color: Theme.onSurface
            font.pixelSize: Theme.fontSizeMedium
            leftPadding: Theme.spacingMedium
            rightPadding: Theme.spacingMedium
            topPadding: Theme.spacingSmall
            bottomPadding: Theme.spacingSmall
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSmall

            ComboBox {
                id: categoryCombo
                Layout.fillWidth: true
                model: ["All", "Favorites", "Blacklisted"].concat(PresetBridge.categories)
                currentIndex: {
                    if (root.selectedCategory === "") return 0
                    if (root.selectedCategory === "__favorites__") return 1
                    if (root.selectedCategory === "__blacklisted__") return 2
                    var cats = PresetBridge.categories
                    for (var i = 0; i < cats.length; i++) {
                        if (cats[i] === root.selectedCategory) return i + 3
                    }
                    return 0
                }
                onActivated: {
                    var val = model[index]
                    if (val === "All") root.selectedCategory = ""
                    else if (val === "Favorites") root.selectedCategory = "__favorites__"
                    else if (val === "Blacklisted") root.selectedCategory = "__blacklisted__"
                    else root.selectedCategory = val
                }

                background: Rectangle {
                    radius: Theme.radiusSmall
                    color: Theme.surfaceVariant
                    border.color: Theme.outline
                }

                contentItem: Text {
                    text: categoryCombo.displayText
                    color: Theme.onSurface
                    font.pixelSize: Theme.fontSizeMedium
                    verticalAlignment: Text.AlignVCenter
                    leftPadding: Theme.spacingSmall
                }
            }

	AppButton {
		icon: "qrc:/qt/qml/ChadVis/resources/icons/random.svg"
		implicitWidth: 44
		implicitHeight: 44
		onClicked: PresetBridge.selectRandom()
	}
        }
    }

    ListView {
        id: presetList
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true

        model: PresetBridge.filteredPresets()
        delegate: PresetDelegate {
            width: presetList.width
            onSelected: PresetBridge.selectByIndex(modelData.index)
            onFavoriteToggled: PresetBridge.toggleFavorite(modelData.index)
            onBlacklistToggled: PresetBridge.toggleBlacklist(modelData.index)
        }

        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AsNeeded
        }

        highlight: Rectangle {
            color: Theme.primary
            opacity: 0.2
            radius: Theme.radiusSmall
        }
        highlightFollowsCurrentItem: true
        highlightMoveDuration: 150

        Connections {
            target: PresetBridge
            function onPresetsChanged() {
                presetList.model = PresetBridge.filteredPresets()
            }
        }
    }

    component PresetDelegate: Rectangle {
        id: delegate
        height: 64
        color: mouseArea.containsMouse ? Theme.surfaceVariant : Theme.surface
        radius: Theme.radiusSmall

        property bool isFavorite: modelData ? modelData.favorite : false
        property bool isBlacklisted: modelData ? modelData.blacklisted : false
        property int rating: modelData ? modelData.rating : 0

        signal selected()
        signal favoriteToggled()
        signal blacklistToggled()

        RowLayout {
            anchors.fill: parent
            anchors.margins: Theme.spacingSmall
            spacing: Theme.spacingSmall

	Image {
		source: delegate.isFavorite ? "qrc:/qt/qml/ChadVis/resources/icons/star-filled.svg" : (delegate.isBlacklisted ? "qrc:/qt/qml/ChadVis/resources/icons/blacklist.svg" : "")
		sourceSize.width: 24
		sourceSize.height: 24
		Layout.preferredWidth: visible ? 24 : 0
		Layout.preferredHeight: visible ? 24 : 0
		fillMode: Image.PreserveAspectFit
		visible: delegate.isFavorite || delegate.isBlacklisted

		ColorOverlay {
			anchors.fill: parent
			source: parent
			color: delegate.isFavorite ? Theme.accent : Theme.onSurfaceVariant
		}
	}

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                Text {
                    text: modelData ? modelData.name : ""
                    color: delegate.isBlacklisted ? Theme.onSurfaceVariant : Theme.onSurface
                    font.pixelSize: Theme.fontSizeMedium
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }

                Text {
                    text: modelData ? (modelData.author ? modelData.author : modelData.category) : ""
                    color: Theme.onSurfaceVariant
                    font.pixelSize: Theme.fontSizeSmall
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
            }

	Row {
		spacing: 2
		Layout.alignment: Qt.AlignVCenter

		Repeater {
			model: 5
			Image {
				source: index < delegate.rating ? "qrc:/qt/qml/ChadVis/resources/icons/star-filled.svg" : "qrc:/qt/qml/ChadVis/resources/icons/star-outline.svg"
				sourceSize.width: 16
				sourceSize.height: 16
				Layout.preferredWidth: 16
				Layout.preferredHeight: 16
				fillMode: Image.PreserveAspectFit
				ColorOverlay {
					anchors.fill: parent
					source: parent
					color: index < delegate.rating ? Theme.accent : Theme.onSurfaceVariant
				}
				MouseArea {
					anchors.fill: parent
					onClicked: PresetBridge.setRating(modelData.index, index + 1)
				}
			}
		}
	}

	AppButton {
		icon: delegate.isFavorite ? "qrc:/qt/qml/ChadVis/resources/icons/star-filled.svg" : "qrc:/qt/qml/ChadVis/resources/icons/star-outline.svg"
		implicitWidth: 36
		implicitHeight: 36
		highlighted: delegate.isFavorite
		onClicked: delegate.favoriteToggled()
	}

	AppButton {
		icon: delegate.isBlacklisted ? "qrc:/qt/qml/ChadVis/resources/icons/blacklist.svg" : "qrc:/qt/qml/ChadVis/resources/icons/random.svg"
		implicitWidth: 36
		implicitHeight: 36
		onClicked: delegate.blacklistToggled()
	}
        }

        MouseArea {
            id: mouseArea
            anchors.fill: parent
            hoverEnabled: true
            onDoubleClicked: delegate.selected()
        }
    }
}
