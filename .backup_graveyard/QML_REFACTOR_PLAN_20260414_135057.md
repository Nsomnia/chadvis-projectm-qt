# QML Modern GUI Refactor - Detailed Implementation Plan

> **Branch:** `refactor/qml-modern-gui`
> **Version:** 1.0
> **Last Updated:** 2026-03-16

---

## Executive Summary

This document outlines the complete plan to replace the current classic Qt Widgets GUI with a modern QML-based interface. The goal is to create a sleek, beautiful, and performant UI that showcases "Arch btw" excellence.

### Why QML?

1. **Declarative UI** - Cleaner, more maintainable code
2. **Hardware Acceleration** - GPU-accelerated rendering via Scene Graph
3. **Animations** - Built-in animation framework for smooth transitions
4. **Designer Friendly** - QML is easier to visually design and iterate
5. **Modern Aesthetics** - Better support for modern UI patterns (glassmorphism, blur, etc.)

---

## Current State Analysis

### Existing UI Architecture

```
src/ui/
├── MainWindow.cpp/hpp        # Main window (QMainWindow)
├── PlayerControls.cpp/hpp    # Transport controls (QWidget)
├── PlaylistView.cpp/hpp      # Playlist display (QTreeWidget)
├── PresetBrowser.cpp/hpp     # Preset browser (QTreeWidget)
├── RecordingControls.cpp/hpp # Recording controls (QWidget)
├── OverlayEditor.cpp/hpp     # Overlay configuration (QWidget)
├── KaraokeWidget.cpp/hpp     # Karaoke display (QWidget)
├── LyricsPanel.cpp/hpp       # Lyrics panel (QWidget)
├── SettingsDialog.cpp/hpp    # Settings dialog (QDialog)
├── SidebarWidget.cpp/hpp     # Modern sidebar (QWidget)
├── SunoBrowser.cpp/hpp       # Suno browser (QWidget)
├── VisualizerPanel.cpp/hpp   # Visualizer container (QWidget)
└── widgets/
    ├── GlowButton.cpp/hpp    # Custom glow button
    ├── CyanSlider.cpp/hpp    # Custom slider
    └── ToggleSwitch.cpp/hpp  # Custom toggle
```

### Theme System
- QSS stylesheets: `dark.qss`, `nord.qss`, `gruvbox.qss`
- Runtime theme switching
- Cyan accent color (#00bcd4) primary

### Core Backend Components (Keep)
- `AudioEngine` - Audio playback and analysis
- `VideoRecorder` - FFmpeg recording
- `OverlayEngine` - OpenGL overlay rendering
- `SunoController` - Suno API integration
- `VisualizerWindow` - OpenGL ProjectM rendering (QWindow)

---

## Target Architecture

### New Directory Structure

```
src/
├── qml/
│   ├── main.qml                 # Root QML file
│   ├── components/              # Reusable QML components
│   │   ├── AppButton.qml
│   │   ├── AppSlider.qml
│   │   ├── AppToggle.qml
│   │   ├── AppPanel.qml
│   │   ├── AppTextField.qml
│   │   ├── AppComboBox.qml
│   │   ├── AppScrollBar.qml
│   │   └── AppProgressBar.qml
│   ├── controls/                # Media control components
│   │   ├── TransportControls.qml
│   │   ├── SeekBar.qml
│   │   ├── VolumeControl.qml
│   │   └── TrackInfo.qml
│   ├── navigation/              # Navigation components
│   │   ├── SidebarNavigation.qml
│   │   ├── NavigationTab.qml
│   │   ├── TopBar.qml
│   │   └── StatusBar.qml
│   ├── panels/                  # Main content panels
│   │   ├── PlaylistPanel.qml
│   │   ├── PresetPanel.qml
│   │   ├── RecordingPanel.qml
│   │   ├── OverlayPanel.qml
│   │   ├── LyricsPanel.qml
│   │   └── SunoPanel.qml
│   ├── screens/                 # Main application screens
│   │   ├── MainScreen.qml
│   │   └── SettingsScreen.qml
│   ├── styles/                  # QML styling
│   │   ├── Theme.qml            # Theme singleton
│   │   └── themes/
│   │       ├── Dark.qml
│   │       ├── Nord.qml
│   │       ├── Gruvbox.qml
│   │       └── Catppuccin.qml
│   └── assets/                  # QML-embedded assets
│       ├── icons/
│       └── fonts/
├── qml_bridge/                  # C++ QML bridge types
│   ├── AudioBridge.cpp/hpp
│   ├── VisualizerBridge.cpp/hpp
│   ├── PlaylistBridge.cpp/hpp
│   ├── RecordingBridge.cpp/hpp
│   ├── SunoBridge.cpp/hpp
│   ├── OverlayBridge.cpp/hpp
│   └── BridgeRegistration.cpp
└── ui/                          # (DEPRECATED - will be removed)
```

---

## Implementation Phases

### Phase 1: Foundation (Week 1)

**Objectives:**
- Set up QML infrastructure
- Create basic bridge types
- Establish theming system

**Tasks:**
1. Add Qt6 Quick/QML to CMakeLists.txt
2. Create `src/qml/` directory structure
3. Implement `AudioBridge` - expose AudioEngine signals/slots
4. Implement `PlaylistBridge` - expose Playlist model
5. Create basic `Theme.qml` singleton
6. Create first test component `main.qml`

**Deliverables:**
- Compiling QML integration
- Basic audio bridge working
- Theme singleton functional

### Phase 2: Core Components (Week 2)

**Objectives:**
- Build reusable component library
- Match/exceed current widget aesthetics

**Tasks:**
1. `AppButton.qml` - Glassmorphism button with glow
2. `AppSlider.qml` - Custom styled slider
3. `AppToggle.qml` - Modern toggle switch
4. `AppPanel.qml` - Glassmorphism panel container
5. `AppTextField.qml` - Modern text input
6. `AppComboBox.qml` - Modern dropdown
7. `AppScrollBar.qml` - Custom scrollbar
8. `AppProgressBar.qml` - Progress indicator

**Design Specifications:**

```qml
// AppButton styling
// - Background: rgba(42, 42, 42, 0.8)
// - Border: 1px solid rgba(0, 188, 212, 0.3)
// - Border radius: 8px
// - Glow: 0 0 10px rgba(0, 188, 212, 0.5) on hover
// - Text: #e0e0e0
// - Font: 14px Inter
```

### Phase 3: Media Controls (Week 3)

**Objectives:**
- Implement playback controls
- Create seek bar and volume controls

**Tasks:**
1. `TransportControls.qml` - Play/Pause/Stop/Next/Prev
2. `SeekBar.qml` - Position slider with time display
3. `VolumeControl.qml` - Volume slider with mute
4. `TrackInfo.qml` - Now playing display

**AudioBridge Signals Needed:**
```cpp
Q_SIGNAL void playbackStateChanged(PlaybackState state);
Q_SIGNAL void positionChanged(qint64 position);
Q_SIGNAL void durationChanged(qint64 duration);
Q_SIGNAL void trackChanged(const MediaMetadata& metadata);
```

### Phase 4: Navigation & Panels (Week 4)

**Objectives:**
- Implement sidebar navigation
- Create all content panels

**Tasks:**
1. `SidebarNavigation.qml` - Vertical icon sidebar
2. `NavigationTab.qml` - Individual tab button
3. `PlaylistPanel.qml` - Playlist display
4. `PresetPanel.qml` - Preset browser
5. `RecordingPanel.qml` - Recording controls
6. `OverlayPanel.qml` - Overlay configuration
7. `LyricsPanel.qml` - Lyrics display
8. `SunoPanel.qml` - Suno browser

**Navigation State:**
```qml
QtObject {
    id: navState
    property string currentPanel: "playlist"
    property bool sidebarExpanded: true
}
```

### Phase 5: Visualizer Integration (Week 5)

**Objectives:**
- Integrate OpenGL ProjectM renderer with QML
- Handle fullscreen and windowed modes

**Challenges:**
- `VisualizerWindow` is a `QWindow`, needs `QQuickItem` wrapper
- OpenGL context sharing between Qt Quick and ProjectM
- Frame timing synchronization

**Solution:**
Create `VisualizerItem` - a `QQuickItem` that:
1. Creates a shared OpenGL context
2. Renders ProjectM to an FBO
3. Blits FBO to QQuick Scene Graph texture

```cpp
class VisualizerItem : public QQuickItem {
    Q_OBJECT
    QML_ELEMENT
public:
    // Properties exposed to QML
    Q_PROPERTY(bool fullscreen READ fullscreen WRITE setFullscreen NOTIFY fullscreenChanged)
    Q_PROPERTY(int fps READ fps WRITE setFps NOTIFY fpsChanged)
    
protected:
    QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) override;
};
```

### Phase 6: Settings & Themes (Week 6)

**Objectives:**
- Implement settings screens
- Complete theme system

**Tasks:**
1. `SettingsScreen.qml` - Settings modal/drawer
2. `AudioSettings.qml` - Audio configuration
3. `VisualizerSettings.qml` - Visualizer settings
4. `RecordingSettings.qml` - Encoder settings
5. `SunoSettings.qml` - Suno API settings
6. Complete all theme QML files
7. Theme persistence to config

### Phase 7: Polish & Migration (Week 7)

**Objectives:**
- Final polish and cleanup
- Remove old UI code

**Tasks:**
1. Performance optimization
2. Accessibility improvements
3. Remove `src/ui/` directory (move to `.backup_graveyard/`)
4. Update `main.cpp` to use QML
5. Update documentation
6. Final testing

---

## Technical Specifications

### QML Module Definition (CMakeLists.txt)

```cmake
qt_add_qml_module(project_lib
    URI ChadVis
    VERSION 1.0
    QML_FILES
        src/qml/main.qml
        src/qml/components/AppButton.qml
        src/qml/components/AppSlider.qml
        # ... all QML files
    RESOURCES
        src/qml/assets/icons/play.svg
        # ... all assets
    SOURCES
        src/qml_bridge/AudioBridge.cpp
        # ... all bridge sources
)
```

### Bridge Pattern

Each bridge follows this pattern:

```cpp
// AudioBridge.hpp
#pragma once
#include <QObject>
#include <QtQml/qqml.h>

class AudioBridge : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    
    // Properties
    Q_PROPERTY(PlaybackState playbackState READ playbackState NOTIFY playbackStateChanged)
    Q_PROPERTY(qint64 position READ position NOTIFY positionChanged)
    Q_PROPERTY(qint64 duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(qreal volume READ volume WRITE setVolume NOTIFY volumeChanged)
    
public:
    enum PlaybackState {
        Stopped = 0,
        Playing = 1,
        Paused = 2
    };
    Q_ENUM(PlaybackState)
    
    // Methods callable from QML
    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();
    Q_INVOKABLE void seek(qint64 position);
    
signals:
    void playbackStateChanged();
    void positionChanged();
    void durationChanged();
    void volumeChanged();
    void trackChanged(const QVariant& metadata);
};
```

### Theme System

```qml
// Theme.qml
pragma Singleton
import QtQuick 2.15

QtObject {
    id: theme
    
    // Primary colors
    readonly property color accent: "#00bcd4"      // Cyan
    readonly property color accentHover: "#33d4e8"
    readonly property color accentPressed: "#0097a7"
    
    // Background colors
    readonly property color background: "#1a1a1a"
    readonly property color surface: "#252525"
    readonly property color surfaceRaised: "#2d2d2d"
    
    // Text colors
    readonly property color textPrimary: "#e0e0e0"
    readonly property color textSecondary: "#888888"
    readonly property color textDisabled: "#606060"
    
    // Semantic colors
    readonly property color success: "#00ff88"
    readonly property color error: "#ff4444"
    readonly property color warning: "#ffaa00"
    
    // Metrics
    readonly property int radiusSmall: 4
    readonly property int radiusMedium: 8
    readonly property int radiusLarge: 12
    
    readonly property int spacingTiny: 4
    readonly property int spacingSmall: 8
    readonly property int spacingMedium: 16
    readonly property int spacingLarge: 24
    
    // Fonts
    readonly property font fontBody: Qt.font({ family: "Inter", pixelSize: 14 })
    readonly property font fontHeading: Qt.font({ family: "Inter", pixelSize: 18, weight: Font.Bold })
    readonly property font fontCaption: Qt.font({ family: "Inter", pixelSize: 12 })
}
```

---

## Asset Requirements

### Icons Needed (SVG, 24x24 default)

| Icon | Description | Style |
|------|-------------|-------|
| play | Play button | Filled triangle |
| pause | Pause button | Two vertical bars |
| stop | Stop button | Filled square |
| next | Next track | Right triangle + bar |
| prev | Previous track | Left triangle + bar |
| volume-high | High volume | Speaker + 3 waves |
| volume-low | Low volume | Speaker + 1 wave |
| volume-mute | Muted | Speaker + X |
| shuffle | Shuffle mode | Crossed arrows |
| repeat | Repeat mode | Circular arrows |
| repeat-one | Repeat one | Circular arrows + 1 |
| record | Recording | Filled circle (red) |
| settings | Settings | Gear |
| playlist | Playlist | List with music notes |
| preset | Presets | Grid/visual |
| suno | Suno | Cloud/music |
| lyrics | Lyrics | Text lines |
| overlay | Overlay | Layers |
| sidebar | Sidebar | Panel icon |
| fullscreen | Fullscreen | Expand arrows |
| windowed | Windowed | Shrink arrows |

### Font Requirements

- **Inter** - Primary UI font (OFL licensed)
  - Weights: Regular, Medium, Bold
  - Included in app resources

---

## Risk Assessment

### High Risk
1. **Visualizer Integration** - OpenGL/QML context sharing is complex
   - Mitigation: Use `QQuickItem` with custom Scene Graph node
   
2. **Recording Frame Capture** - PBO capture needs to work with QML
   - Mitigation: VisualizerItem handles PBO capture internally

### Medium Risk
1. **Performance** - QML adds overhead
   - Mitigation: Profile early, use `layer.enabled` sparingly
   
2. **WebEngine Integration** - SunoBrowser uses QWebEngineView
   - Mitigation: Use `QQuickWidget` or migrate to Qt WebEngine QML

### Low Risk
1. **Theme Migration** - Straightforward QML styling
2. **Component Creation** - Well-understood QML patterns

---

## Success Criteria

1. **Visual Quality**
   - Glassmorphism effects working
   - Smooth 60fps animations
   - Theme switching without restart
   
2. **Functional Parity**
   - All current features working
   - No regressions in playback
   - Recording functionality intact
   
3. **Performance**
   - UI latency < 16ms
   - Memory usage comparable or better
   - No frame drops during visualization
   
4. **Maintainability**
   - Clean component separation
   - Documented QML API
   - Easy to add new features

---

## Timeline Summary

| Phase | Duration | Key Deliverable |
|-------|----------|-----------------|
| 1. Foundation | 1 week | QML compiling, basic bridges |
| 2. Components | 1 week | Complete component library |
| 3. Controls | 1 week | Media controls working |
| 4. Panels | 1 week | All panels implemented |
| 5. Visualizer | 1 week | OpenGL integration working |
| 6. Settings | 1 week | Settings and themes complete |
| 7. Polish | 1 week | Old UI removed, final testing |

**Total Duration: ~7 weeks** (can be parallelized)

---

## Questions for User

1. **Font Preference**: Inter, Roboto, or system default?
2. **Theme Priority**: Which themes should be implemented first?
3. **Animation Style**: Subtle or prominent animations?
4. **Sidebar Default**: Expanded or collapsed by default?
5. **Fullscreen Behavior**: Overlay controls or hidden controls?

---

*"Maximum effort. No compromises. Ship it like it's Arch Linux."* 🚀
