# ChadVis Architecture

ChadVis is currently a Suno-first Qt Quick desktop client. Its library, creation, playback, account, and settings surfaces are primary; projectM is hosted as a native video surface. This document describes the implementation in the current tree rather than the earlier visualizer-first shell.

## Process Composition and Ownership

`vc::Application` is the composition root and lifecycle coordinator. It does not implement playback, rendering, networking, or QML behavior itself. During `Application::init()` it:

1. Loads configuration and initializes logging.
2. Creates the `QGuiApplication`.
3. Creates and initializes `AudioEngine` and `VideoRecorder`.
4. In GUI mode, creates `PresetManager`, `VisualizerWindow`, `LyricsSync`, and `SunoController`.
5. Creates one `QQmlApplicationEngine` and registers the C++ QML bridges.
6. Loads `src/qml/main.qml` from the Qt resource system.

`Application` owns those major objects with `std::unique_ptr`. Dependencies passed into managers and renderers, such as `AudioEngine*`, `SunoClient*`, and `AudioQueue*`, are non-owning. The destructor resets them explicitly in dependency order (`src/core/Application.cpp:59-70`): QML engine, `VisualizerWindow`, `VideoRecorder`, `SunoController`, `LyricsSync`, `PresetManager`, `AudioEngine`, then the `QGuiApplication`, then `Logger::shutdown()`. Nothing relies on static destruction order to save you.

The startup singleton is still exposed through `vc::Application::instance()` and the `APP` macro, but subsystem work remains in the audio, visualizer, recorder, lyrics, and Suno classes. `Application` is therefore the startup composition point, not the implementation of every subsystem.

## QML Application Shell

There is one `QQmlApplicationEngine`. `Application::init()` loads only `src/qml/main.qml`; `src/qml/SettingsWindow.qml` is declared inside that QML document and uses the same engine. There is no second C++ QML engine for Settings.

`main.qml` owns the primary `ApplicationWindow` and the persistent `NavRail`. The rail exposes seven destinations (`src/qml/main.qml:33-41`):

- Library
- Notifications
- Explore
- Create
- Listen
- Video
- Settings

Selecting a content destination changes `activeView`. The shell then shows the matching view. Selecting Settings opens the separate, application-modal `SettingsWindow`, which has its own page rail for Account, Audio, Visualizer, Recording, Karaoke, Appearance, Performance, Shortcuts, and Profiles (`src/qml/SettingsWindow.qml:102-110`).

The current shell does not use per-view `Loader` objects. All six content views — `LibraryView`, `NotificationsView`, `DiscoverView`, `CreateView`, `ListenView`, and `VideoView` — are declared together under the single view host and switched with `visible` and opacity (`src/qml/main.qml:304,316,331,346,358,372`). In particular, `VideoView`—not `ListenView`—contains the native visualizer container and remains instantiated for the process lifetime. Unloading it could destroy the `WindowContainer` binding and force the embedded `QWindow` and its OpenGL context to be recreated during navigation.

The primary window and Settings both call `SettingsBridge.save()` while closing. Configuration-backed `SettingsBridge` setters are also debounced and saved after two seconds without another change.

## Native Visualizer Embedding

The visualizer is a native window embedded into QML:

1. `Application` creates a `VisualizerWindow`, a `QWindow` subclass.
2. `VisualizerBridge::setVisualizerEngine()` retains a non-owning pointer to that window.
3. `VisualizerBridge::visualizerWindow` exposes it to QML as `QWindow*`.
4. `VideoView` assigns that pointer to a Qt Quick `WindowContainer`.

`VisualizerWindow` owns the `QOpenGLContext` and `VisualizerRenderer`. The renderer pulls float PCM off the visualizer side of `AudioQueue` and hands it to the projectM bridge, and in the normal display path projectM renders to the native window's default framebuffer. The projectM bridge, its off-screen render target, and preset discovery are documented in [../integration/PROJECTM.md](../integration/PROJECTM.md); that file is the authority for the engine path, this section is the authority for who owns the window and the GL context.

There is no `VisualizerItem` or other projectM `QQuickItem` in the current source or build. The deleted `VisualizerItem` path must not be reintroduced as a second rendering architecture.

## Audio, Recording, and Network Ownership

`AudioEngine` owns playback, the playlist, analyzer state, and `AudioQueue`. `QAudioBufferOutput` supplies PCM buffers; `AudioEngine::processAudioBuffer()` converts them and pushes them to the visualizer, recorder, and analyzer queues. A `vc::JThread` runs `analyzerWorker()` and consumes the analyzer queue. On toolchains with `std::jthread`, `vc::JThread` is an alias; otherwise it is the repository's stoppable, joinable fallback.

`VideoRecorder` owns a `VideoRecorderThread` worker. That worker consumes queued video frames, drains the recorder audio queue when available, and performs FFmpeg encoding away from the QML/render loop. `VisualizerRenderer` owns the GL-side capture resources, while the recorder owns encoding state and output.

`SunoClient` owns its `QNetworkAccessManager` and authenticated request queue. The queue timer dispatches at most one queued request per second. The source does not create a separate Suno network thread; Qt delivers asynchronous manager replies through the event loop. `SunoController` creates a separate `QNetworkAccessManager` for `SunoDownloader`/download-queue work.

## QML Bridge Registration and Lifetime

`qml_bridge::registerBridges()` registers the QML singleton factories before `main.qml` is loaded and then injects the backend pointers. Registration does not necessarily construct each singleton: the factory can remain lazy until first QML access.

`QmlSingletonBridge` makes the ownership policy explicit:

- `CachedQmlParented` creates one instance and parents it to the `QQmlEngine`. `SettingsBridge` and `PresetBridge` use this policy.
- `CachedUnparented` creates one cached instance without a QObject parent. `SunoBridge` uses this policy because it also retains non-owning backend pointers that must be installed independently of QML construction.

`ThemeBridge` is **deliberately not registered** (`src/qml_bridge/BridgeRegistration.cpp:34-37`). The QML-side `styles/Theme.qml` singleton owns the name `Theme` and carries the full token set. Registering the C++ `ThemeBridge` under that same name would shadow the QML singleton and leave most tokens — `fontDisplay`, `spacingXL`, the shadow scale, and the rest — undefined at runtime. It is still built and linked; it is simply not exposed as a QML singleton, and it must stay that way unless the token set is unified first.

`SunoBridge` has a startup-order invariant. `registerBridges()` calls `SunoBridge::setSunoController()` before `QQmlApplicationEngine::load()`, so the static controller pointer is set while the singleton is still unconstructed. The first QML access constructs `SunoBridge`, whose constructor wires the already-installed controller. If the singleton already exists, `setSunoController()` must wire it. For that reason, wiring exists in both the constructor and `setSunoController()`; removing either path can leave library, authentication, account, and error signals undelivered.

## Suno Request and Data Flow

The primary library flow is:

`SunoClient` → `SunoLibraryManager` → `SunoController` → `SunoBridge` → QML

In the opposite direction, QML calls enter `SunoBridge`, which delegates to `SunoController` and its managers.

- `SunoClient` is the rate-limited HTTP surface. Authentication policy and Clerk protocol details are delegated to `src/suno/auth/`, including credential storage, JWT handling, request headers, and bearer refresh.
- `SunoLibraryManager` requests `feed/v3` pages, follows the returned cursor, accumulates clips, saves each page to `SunoDatabase`, and emits progressive updates.
- `SunoController` is the facade over `SunoClient`, `SunoLibraryManager`, `SunoAccountManager`, `SunoDownloader`, `SunoLyricsManager`, `SunoDatabase`, and `SunoOrchestrator`. When authentication becomes valid, it bootstraps the session catalog and billing snapshot.
- `SunoBridge` converts clip state into QML properties and forwards generation, search, pagination, chat, authentication, account, and error events to the views.

All centralized Suno URL constants live in `src/suno/SunoEndpoints.hpp`. Entries are marked `[T1]` when observed in the repository's listed captures and `[LEAD]` when they are recon-only or constructed. Code must preserve those tiers and must not promote a `[LEAD]` without capture evidence.
