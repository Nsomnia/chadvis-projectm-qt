# QML Modern GUI Refactor Guide

## Architecture
The application has transitioned from a Qt Widgets-based UI to a modern **Qt Quick (QML)** interface. 

### Key Components
- **Main.qml**: The root application window, implementing a responsive `SplitView` for desktop and `Drawer` for mobile/compact views.
- **VisualizerItem**: A custom C++ `QQuickItem` that bridges projectM v4 rendering into the QML scene graph.
- **Bridges**: C++/QML bridge classes (e.g., `AudioBridge`, `RecordingBridge`) provide reactive data and control flow between the engine and the UI.

## Layout System
- **Desktop Sidebar**: Automatically visible when window width > 1200px.
- **Hamburger Menu**: Provides access to panels via a `Drawer` when space is constrained.
- **Accordion Panels**: UI modules (Playback, Library, Presets, etc.) are organized in an expandable accordion container.

## Styling
Global styles are managed via `src/qml/styles/Theme.qml`.
- **Accent Color**: Cyan (#00bcd4) by default, user-customizable.
- **Background**: Dark (#1a1a1a) for maximum visualizer contrast.

---
*Version: 2.0.0 - 2026-04-20*
