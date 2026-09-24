import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ChadVis
import "settings"

ApplicationWindow {
    id: root

    visible: false
    width: 900
    height: 680
    minimumWidth: 760
    minimumHeight: 560
    title: "ChadVis Settings"

    readonly property var pages: [
        { title: "Account", eyebrow: "Suno session" },
        { title: "Audio", eyebrow: "Playback engine" },
        { title: "Visualizer", eyebrow: "projectM engine" },
        { title: "Recording", eyebrow: "Video export" },
        { title: "Karaoke", eyebrow: "Synced captions" },
        { title: "Appearance", eyebrow: "Color and contrast" },
        { title: "Performance", eyebrow: "Resource presets" },
        { title: "Shortcuts", eyebrow: "Keyboard reference" },
        { title: "Profiles", eyebrow: "Portable preferences" }
    ]

    property int currentPage: 0
    property string statusMessage: ""

    function saveSettings() {
        appearancePage.apply()
        SettingsBridge.save()
        statusMessage = "Settings saved"
        statusTimer.restart()
    }

    function requestReset() {
        resetDialog.open()
    }

    background: Rectangle {
        color: Theme.background
    }

    palette.window: Theme.background
    palette.windowText: Theme.textPrimary
    palette.base: Theme.surfaceRaised
    palette.alternateBase: Theme.backgroundAlt
    palette.text: Theme.textPrimary
    palette.button: Theme.surfaceRaised
    palette.buttonText: Theme.textPrimary
    palette.brightText: Theme.textPrimary
    palette.light: Theme.surfaceOverlay
    palette.midlight: Theme.borderLight
    palette.mid: Theme.border
    palette.dark: Theme.backgroundAlt
    palette.shadow: Theme.withAlpha(Theme.background, 0.65)
    palette.highlight: Theme.accent
    palette.highlightedText: Theme.textOnAccent
    palette.link: Theme.textLink
    palette.linkVisited: Theme.accentLight
    palette.toolTipBase: Theme.surfaceOverlay
    palette.toolTipText: Theme.textPrimary

    onClosing: {
        appearancePage.apply()
        SettingsBridge.save()
    }

    header: SettingsWindowHeader {
        title: root.pages[root.currentPage].title
        eyebrow: root.pages[root.currentPage].eyebrow
        onCloseRequested: root.close()
    }

    Item {
        anchors.fill: parent

        RowLayout {
            anchors.fill: parent
            spacing: 0

            SettingsPageRail {
                pages: root.pages
                currentPage: root.currentPage
                onNavigate: function(pageIndex) {
                    root.currentPage = pageIndex
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Theme.background

                StackLayout {
                    anchors.fill: parent
                    currentIndex: root.currentPage

                    AccountPage {}
                    AudioPage {}
                    VisualizerPage {}
                    RecordingPage {}
                    KaraokePage {}
                    AppearancePage { id: appearancePage }
                    PerformancePage {}
                    ShortcutsPage {}
                    ProfilesPage {}
                }
            }
        }
    }

    footer: SettingsWindowFooter {
        statusMessage: root.statusMessage
        onResetRequested: root.requestReset()
        onSaveRequested: root.saveSettings()
    }

    Dialog {
        id: resetDialog
        title: "Reset all settings?"
        modal: true
        width: 420
        standardButtons: Dialog.Yes | Dialog.No

        contentItem: ColumnLayout {
            spacing: Theme.spacingMedium

            Text {
                Layout.fillWidth: true
                text: "Audio, visualizer, recorder, shortcut and interface defaults will be restored. This cannot be undone."
                color: Theme.textPrimaryVariant
                font: Theme.fontBody
                wrapMode: Text.WordWrap
            }
        }

        onAccepted: {
            SettingsBridge.resetToDefaults()
            appearancePage.apply()
            root.statusMessage = "Defaults restored"
            statusTimer.restart()
        }
    }

    Timer {
        id: statusTimer
        interval: 2400
        repeat: false
        onTriggered: root.statusMessage = ""
    }

    Shortcut {
        sequence: "Esc"
        enabled: root.visible
        onActivated: root.close()
    }
}
