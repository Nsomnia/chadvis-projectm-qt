import QtQuick
import QtQuick.Layouts
import ChadVis
import "../panels"

Item {
    id: root

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingLarge
        spacing: Theme.spacingMedium

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingMedium

            ColumnLayout {
                spacing: Theme.spacingTiny

                Text {
                    text: "Create"
                    color: Theme.textPrimary
                    font: Theme.fontHeading
                }

                Text {
                    text: "Turn a prompt into music, or continue the workflow in B-Side Chat."
                    color: Theme.textSecondary
                    font: Theme.fontBody
                }
            }

            Item { Layout.fillWidth: true }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.border
        }

        SunoPanel {
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
    }
}
