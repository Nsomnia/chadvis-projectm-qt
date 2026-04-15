# QML Component Design - Accordion System

> **Focus:** Space-efficient collapsible panels for complex UI
> **Priority:** High - Core UX consideration

---

## Accordion Design Philosophy

Given the density of controls (playback, playlist, presets, recording, overlays, lyrics, suno), an accordion-style collapsible system is ideal for:
- Keeping visual focus on the ProjectM canvas
- Reducing visual clutter
- Allowing quick access to any panel without mode switching
- Mobile-friendly future-proofing

---

## Proposed Accordion Architecture

### Layout Structure

```
┌────────────────────────────────────────────────────────────┐
│ ┌────────────┐  ┌──────────────────────────────────────┐  │
│ │            │  │                                      │  │
│ │  SIDEBAR   │  │        PROJECTM VISUALIZER          │  │
│ │  (icons)   │  │           (main canvas)             │  │
│ │            │  │                                      │  │
│ │  ▶ Play    │  │                                      │  │
│ │  ▶ List    │  │                                      │  │
│ │  ▶ Preset  │  │                                      │  │
│ │  ▶ Rec     │  │                                      │  │
│ │  ▶ Overlay │  │                                      │  │
│ │  ▶ Lyrics  │  │                                      │  │
│ │  ▶ Suno    │  │                                      │  │
│ │            │  └──────────────────────────────────────┘  │
│ │            │  ┌──────────────────────────────────────┐  │
│ │            │  │  [EXPANDED PANEL CONTENT]            │  │
│ │            │  │  (fills remaining vertical space)    │  │
│ │            │  └──────────────────────────────────────┘  │
│ └────────────┘                                             │
└────────────────────────────────────────────────────────────┘
```

### Alternative: Collapsible Accordion Sidebar

```
┌──────────┬─────────────────────────────────────────────────┐
│ ▼ PLAY   │                                                 │
│   ┌────┐ │                                                 │
│   │▶⏸⏹│ │        PROJECTM VISUALIZER                      │
│   │━━━━│ │        (expands to fill available space)        │
│   │ VOL│ │                                                 │
│   └────┘ │                                                 │
│ ◀ LIST   │                                                 │
│ ◀ PRESET │                                                 │
│ ◀ REC    │                                                 │
│ ◀ OVR    │                                                 │
│ ◀ LYRICS │                                                 │
│ ◀ SUNO   │                                                 │
└──────────┴─────────────────────────────────────────────────┘
```

---

## QML Accordion Components

### 1. AccordionContainer.qml

Primary container managing accordion state and animations.

```qml
// AccordionContainer.qml
import QtQuick 2.15
import QtQuick.Layouts 1.15

ColumnLayout {
    id: root
    
    property string expandedPanel: "playback"
    property int animationDuration: 250
    
    // Panels register themselves here
    property var panels: []
    
    function togglePanel(panelId) {
        if (expandedPanel === panelId) {
            expandedPanel = ""  // Collapse all
        } else {
            expandedPanel = panelId
        }
    }
    
    spacing: 2
    
    Repeater {
        model: panels
        delegate: AccordionPanel {
            panelId: modelData.id
            title: modelData.title
            icon: modelData.icon
            isExpanded: expandedPanel === modelData.id
            
            contentComponent: modelData.component
            
            onHeaderClicked: root.togglePanel(panelId)
        }
    }
}
```

### 2. AccordionPanel.qml

Individual collapsible panel with header and content area.

```qml
// AccordionPanel.qml
import QtQuick 2.15
import QtQuick.Layouts 1.15

Item {
    id: root
    
    property string panelId
    property string title
    property string icon
    property bool isExpanded: false
    property Component contentComponent
    property int collapsedHeight: 40
    property int expandedHeight: 300
    
    signal headerClicked()
    
    implicitHeight: isExpanded ? expandedHeight : collapsedHeight
    
    Behavior on implicitHeight {
        NumberAnimation { duration: 250; easing.type: Easing.OutCubic }
    }
    
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        
        // Header
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: collapsedHeight
            
            color: isExpanded ? Theme.accent : Theme.surface
            radius: Theme.radiusMedium
            
            // Glassmorphism effect
            opacity: 0.9
            
            RowLayout {
                anchors.fill: parent
                anchors.margins: Theme.spacingSmall
                
                Image {
                    source: root.icon
                    Layout.preferredWidth: 20
                    Layout.preferredHeight: 20
                }
                
                Text {
                    text: root.title
                    color: isExpanded ? Theme.background : Theme.textPrimary
                    font: Theme.fontBody
                    Layout.fillWidth: true
                }
                
                Text {
                    text: isExpanded ? "▼" : "▶"
                    color: isExpanded ? Theme.background : Theme.textSecondary
                    font.pixelSize: 12
                    
                    RotationAnimator on rotation {
                        running: true
                        from: isExpanded ? 0 : 90
                        to: isExpanded ? 90 : 0
                        duration: 250
                    }
                }
            }
            
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: root.headerClicked()
            }
        }
        
        // Content Area
        Rectangle {
            id: contentArea
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: isExpanded
            clip: true
            
            color: Theme.surface
            radius: Theme.radiusMedium
            opacity: 0.95
            
            // Content is loaded dynamically
            Loader {
                anchors.fill: parent
                anchors.margins: Theme.spacingSmall
                active: root.isExpanded  // Only instantiate when expanded
                sourceComponent: root.contentComponent
            }
        }
    }
}
```

### 3. AccordionSidebar.qml

Complete sidebar with icon + accordion combination.

```qml
// AccordionSidebar.qml
import QtQuick 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: root
    
    property int collapsedWidth: 60
    property int expandedWidth: 280
    property bool sidebarExpanded: true
    
    color: Theme.background
    implicitWidth: sidebarExpanded ? expandedWidth : collapsedWidth
    
    Behavior on implicitWidth {
        NumberAnimation { duration: 250; easing.type: Easing.OutCubic }
    }
    
    RowLayout {
        anchors.fill: parent
        spacing: 0
        
        // Icon strip (always visible)
        ColumnLayout {
            Layout.preferredWidth: 60
            Layout.fillHeight: true
            spacing: 4
            
            Repeater {
                model: [
                    { id: "playback", icon: "qrc:/icons/play.svg" },
                    { id: "playlist", icon: "qrc:/icons/playlist.svg" },
                    { id: "presets", icon: "qrc:/icons/preset.svg" },
                    { id: "recording", icon: "qrc:/icons/record.svg" },
                    { id: "overlays", icon: "qrc:/icons/overlay.svg" },
                    { id: "lyrics", icon: "qrc:/icons/lyrics.svg" },
                    { id: "suno", icon: "qrc:/icons/suno.svg" }
                ]
                
                delegate: IconButton {
                    icon.source: modelData.icon
                    checked: accordionContainer.expandedPanel === modelData.id
                    onClicked: {
                        if (checked) {
                            accordionContainer.togglePanel(modelData.id)
                        }
                    }
                }
            }
            
            Item { Layout.fillHeight: true }  // Spacer
            
            // Expand/collapse toggle
            IconButton {
                icon.source: sidebarExpanded ? "qrc:/icons/collapse.svg" : "qrc:/icons/expand.svg"
                onClicked: sidebarExpanded = !sidebarExpanded
            }
        }
        
        // Accordion content area
        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: root.implicitWidth - 60
            visible: sidebarExpanded
            clip: true
            
            AccordionContainer {
                id: accordionContainer
                anchors.fill: parent
                anchors.margins: 4
                
                panels: [
                    { id: "playback", title: "Playback", icon: "qrc:/icons/play.svg", component: playbackComponent },
                    { id: "playlist", title: "Library", icon: "qrc:/icons/playlist.svg", component: playlistComponent },
                    { id: "presets", title: "Presets", icon: "qrc:/icons/preset.svg", component: presetsComponent },
                    { id: "recording", title: "Recording", icon: "qrc:/icons/record.svg", component: recordingComponent },
                    { id: "overlays", title: "Overlays", icon: "qrc:/icons/overlay.svg", component: overlaysComponent },
                    { id: "lyrics", title: "Lyrics", icon: "qrc:/icons/lyrics.svg", component: lyricsComponent },
                    { id: "suno", title: "Suno", icon: "qrc:/icons/suno.svg", component: sunoComponent }
                ]
            }
        }
    }
    
    // Component definitions for lazy loading
    Component { id: playbackComponent; PlaybackPanel { } }
    Component { id: playlistComponent; PlaylistPanel { } }
    Component { id: presetsComponent; PresetsPanel { } }
    Component { id: recordingComponent; RecordingPanel { } }
    Component { id: overlaysComponent; OverlaysPanel { } }
    Component { id: lyricsComponent; LyricsPanel { } }
    Component { id: sunoComponent; SunoPanel { } }
}
```

---

## Panel Content Components

### PlaybackPanel.qml (Accordion Content)

Compact playback controls for accordion panel.

```qml
// PlaybackPanel.qml
import QtQuick 2.15
import QtQuick.Layouts 1.15

ColumnLayout {
    spacing: Theme.spacingMedium
    
    // Now Playing
    RowLayout {
        Layout.fillWidth: Layout
        
        Image {
            source: AudioBridge.currentTrack.albumArt || "qrc:/icons/default-album.svg"
            Layout.preferredSize: 48
            fillMode: Image.PreserveAspectCrop
            
            Rectangle {
                anchors.fill: parent
                color: "transparent"
                border.color: Theme.accent
                border.width: 1
                radius: Theme.radiusSmall
            }
        }
        
        ColumnLayout {
            Layout.fillWidth: true
            
            Text {
                text: AudioBridge.currentTrack.title || "Not Playing"
                color: Theme.textPrimary
                font: Theme.fontBody
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            
            Text {
                text: AudioBridge.currentTrack.artist || ""
                color: Theme.textSecondary
                font: Theme.fontCaption
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
        }
    }
    
    // Transport Controls
    RowLayout {
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignHCenter
        
        AppButton { icon: "prev"; onClicked: AudioBridge.previous() }
        AppButton { 
            icon: AudioBridge.playbackState === AudioBridge.Playing ? "pause" : "play"
            highlighted: true
            onClicked: AudioBridge.playPause() 
        }
        AppButton { icon: "stop"; onClicked: AudioBridge.stop() }
        AppButton { icon: "next"; onClicked: AudioBridge.next() }
    }
    
    // Seek Bar
    ColumnLayout {
        Layout.fillWidth: true
        
        AppSlider {
            Layout.fillWidth: true
            from: 0
            to: AudioBridge.duration
            value: AudioBridge.position
            onMoved: AudioBridge.seek(value)
        }
        
        RowLayout {
            Layout.fillWidth: true
            
            Text {
                text: formatTime(AudioBridge.position)
                color: Theme.textSecondary
                font: Theme.fontCaption
            }
            
            Item { Layout.fillWidth: true }
            
            Text {
                text: formatTime(AudioBridge.duration)
                color: Theme.textSecondary
                font: Theme.fontCaption
            }
        }
    }
    
    // Volume
    RowLayout {
        Layout.fillWidth: true
        
        AppButton { 
            icon: AudioBridge.volume > 0 ? "volume-high" : "volume-mute"
            onClicked: AudioBridge.toggleMute()
        }
        
        AppSlider {
            Layout.fillWidth: true
            from: 0
            to: 100
            value: AudioBridge.volume * 100
            onMoved: AudioBridge.setVolume(value / 100)
        }
    }
    
    function formatTime(ms) {
        var s = Math.floor(ms / 1000)
        var m = Math.floor(s / 60)
        s = s % 60
        return m + ":" + (s < 10 ? "0" : "") + s
    }
}
```

---

## UX Considerations

### Accordion Behavior

1. **Single Expansion**: Only one panel expanded at a time (classic accordion)
   - Option: Allow multiple expanded (configurable)

2. **Auto-Collapse**: When switching panels, previous panel collapses
   - Animation: Smooth height transition (250ms OutCubic)

3. **State Persistence**: Remember expanded state between sessions

4. **Keyboard Navigation**:
   - `Tab` cycles through panel headers
   - `Enter/Space` toggles panel
   - `Escape` collapses all

5. **Touch Support**:
   - Swipe gestures to expand/collapse
   - Large touch targets (min 44px)

### Space Allocation

```
Window Height: 800px (example)
├── TopBar: 40px
├── Visualizer Canvas: 510px (fills available space)
└── Bottom Accordion: 250px (expanded panel)
    ├── Playback Header: 40px
    ├── Playlist Header: 40px
    ├── Expanded Content: 250px
    └── Other Headers: 40px each
```

---

## Implementation Order

1. `AccordionPanel.qml` - Basic expand/collapse
2. `AccordionContainer.qml` - State management
3. `AccordionSidebar.qml` - Icon strip integration
4. `PlaybackPanel.qml` - First content panel
5. `PlaylistPanel.qml` - List-based content
6. Remaining panels...

---

## Advantages Over Current Tab System

| Aspect | Current Tabs | Accordion |
|--------|-------------|-----------|
| Visual Focus | Panels cover visualizer | Visualizer always visible |
| Quick Access | Tab switching required | Single click to expand |
| Information Density | One panel visible | Headers always visible |
| Mobile Friendly | Poor | Excellent |
| Screen Real Estate | Fixed dock width | Flexible, collapsible |

---

*"Accordion: Because cramming 7 panels into one window shouldn't require a scroll degree."* 🪗
