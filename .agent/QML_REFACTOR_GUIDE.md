# QML Refactor Architecture Guide

> **For:** AI agents working on `refactor/qml-modern-gui`
> **Version:** 1.0.0 - 2026-03-27
> **Purpose:** QML-specific architectural decisions, patterns, and known issues

---

## Current State

The branch has completed an initial QML GUI refactor with:
- `src/qml/` — QML UI files (main.qml, panels, components, styles)
- `src/qml_bridge/` — C++ bridge classes exposing functionality to QML
- Basic accordion sidebar with glassmorphism theme
- VisualizerItem QQuickItem wrapping projectM OpenGL rendering

**What's working:**
- QML shell loads and renders
- ProjectM visualizer renders via VisualizerItem with OpenGL
- Theme system (cyan accent #00bcd4, glassmorphism)
- Basic playback panel bindings

**What needs work:** Everything in the TODO.md.

---

## Critical Architecture Decision: OpenGL vs RHI

### The Problem
Qt 6 QML defaults to **RHI (Rendering Hardware Interface)** — which may be Vulkan, Metal, or D3D11 depending on the platform. But projectM requires raw OpenGL 3.3 calls (`glGenFramebuffers`, `glDrawArrays`, etc.).

If QML is running on Vulkan and C++ tries to call `glClear`, the app **will segfault**.

### Current Fix (Temporary)
`Application.cpp` forces OpenGL backend:
```cpp
QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
```
And `VisualizerItem` uses `beginExternalCommands()`/`endExternalCommands()` for Qt 6 scene graph integration.

### Permanent Solution
Wrap projectM in a **QQuickFramebufferObject (QFBO)**:
1. QFBO provides a safe `QQuickFramebufferObject::Renderer` thread
2. Qt guarantees an active OpenGL context in that thread
3. ProjectM renders to an FBO
4. QML safely composites the FBO into the modern UI
5. This works regardless of QML's underlying RHI backend

---

## QML Bridge Pattern

All bridges follow this pattern:

```cpp
class FooBridge : public QObject {
    Q_OBJECT
    Q_PROPERTY(Type name READ name WRITE setName NOTIFY nameChanged)
    
public:
    explicit FooBridge(QObject* parent = nullptr);
    
    // Property accessors
    Type name() const;
    void setName(const Type& value);
    
public slots:
    void doAction();
    
signals:
    void nameChanged();
};
```

### Registration (in BridgeRegistration.cpp)
```cpp
// Current: manual singleton registration
qmlRegisterSingletonType<FooBridge>("Chadvis.Bridges", 1, 0, "FooBridge", 
    [](QQmlEngine*, QJSEngine*) -> QObject* { return new FooBridge(); });
```

**Future:** Use Qt 6 declarative macros (`QML_ELEMENT`, `QML_SINGLETON`) + `qt_add_qml_module` in CMake.

### Existing Bridges
| Bridge | Exposes | Key Properties |
|--------|---------|----------------|
| `AudioBridge` | AudioEngine state | `isPlaying`, `position`, `duration`, `volume` |
| `PlaylistBridge` | Playlist model | `currentIndex`, `trackCount`, `currentTitle` |
| `VisualizerBridge` | Visualizer control | `currentPreset`, `autoSwitch`, `presetList` |
| `RecordingBridge` | Recording state | `isRecording`, `elapsedTime`, `codec` |
| `SunoBridge` | Suno integration | `isLoggedIn`, `library`, `isSyncing` |
| `LyricsBridge` | Lyrics display | `currentLine`, `lyricsData`, `syncEnabled` |
| `PresetBridge` | Preset management | `presets`, `favorites`, `blacklist` |
| `ThemeBridge` | Theme singleton | `accentColor`, `bgColor`, `glassOpacity` |

---

## QML File Structure

```
src/qml/
├── main.qml                    # Root window, DropArea, visualizer
├── components/
│   ├── AccordionContainer.qml  # Sidebar container
│   ├── AccordionPanel.qml      # Collapsible panel
│   ├── AppButton.qml           # Styled button
│   └── AppSlider.qml           # Styled slider
├── panels/
│   ├── PlaybackPanel.qml       # Play/pause/seek/volume
│   ├── PlaylistPanel.qml       # Track list
│   ├── PresetsPanel.qml        # Visualizer presets
│   ├── RecordingPanel.qml      # Recording controls
│   ├── SunoPanel.qml           # Suno library browser
│   ├── LyricsPanel.qml         # Lyrics display
│   └── OverlayPanel.qml        # Text overlay editor
└── styles/
    └── Theme.qml               # Theme singleton (colors, spacing)
```

---

## Overlay Migration Plan

### Current (C++ OpenGL — to be deleted)
- `src/overlay/OverlayEngine.cpp` — Manages text elements
- `src/overlay/OverlayRenderer.cpp` — OpenGL rendering
- `src/overlay/TextAnimator.cpp` — Animation engine
- `src/overlay/TextElement.cpp` — Text element data

### Target (Pure QML)
- Use QtQuick `Text` items with `NumberAnimation` for movement
- Use `MultiEffect` (Qt 6) for glow/shadows — GPU-accelerated
- Use `DragHandler` + `PinchHandler` for WYSIWYG editing
- Save coordinates back to TOML config via `ConfigBridge`
- Layer on top of the QFBO visualizer in the Z-order

**This deletes hundreds of lines of legacy C++ GL code.**

---

## Settings Window Plan

### From `SettingsDialog.cpp` (40+ states, 6 tabs)
The QML version should use:
- `SwipeView` or custom `ListView` sidebar for tab navigation
- Qt Quick Controls 2 `Switch`, `SpinBox`, `ComboBox`
- Custom `AppSlider.qml` for ranges
- All bindings go through `ConfigBridge` singleton

### ConfigBridge Pattern
```cpp
// Instead of 40+ individual Q_PROPERTY, use section-based maps:
class ConfigBridge : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap audio READ audioConfig NOTIFY configChanged)
    Q_PROPERTY(QVariantMap recording READ recordingConfig NOTIFY configChanged)
    Q_PROPERTY(QVariantMap visualizer READ visualizerConfig NOTIFY configChanged)
    Q_PROPERTY(QVariantMap suno READ sunoConfig NOTIFY configChanged)
    // ...
public slots:
    void setAudioConfig(const QVariantMap& values);
    void save();
};
```

---

## Suno Library Model Plan

```cpp
class SunoLibraryModel : public QAbstractTableModel {
    Q_OBJECT
    // Columns: Title, Duration, Tags, Created, Status
    // Lazy-loading: only fetch rows visible in viewport
    // Sorting: delegate to SQL ORDER BY, not in-memory sort
    // Search: SQL LIKE filter
};
```

QML consumption:
```qml
TableView {
    model: sunoLibraryModel
    // Virtual scrolling handles 10,000+ items
}
```

---

## Known Issues on Current Branch

1. **VisualizerItem RHI warning:** `beginExternalCommands()` suppresses the warning but is not the permanent fix. QFBO is the answer.
2. **Theme singleton:** Currently uses `Theme.qml` singleton — may need to become C++ `ThemeBridge` for dynamic theme switching.
3. **Panel state:** Accordion panels don't persist open/closed state across sessions.
4. **Missing panels:** Several Widgets-era panels don't have QML equivalents yet (see TODO.md).

---

## Build Notes

```bash
# QML build requires:
# - Qt6 Quick, QuickControls2, Qml
# - qt_add_qml_module() in CMakeLists.txt
# - QML_IMPORT_PATH set for IDE support

# Current CMake setup is functional. Changes to QML files
# don't require full rebuild — only C++ bridge changes do.
```
