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

`AudioEngine` owns playback, the playlist, and `AudioQueue`. The primary PCM tap is `QAudioBufferOutput`; `AudioEngine::processAudioBuffer()` converts its buffers and pushes them to the visualizer and recorder queues.

**The tap is conditional, because `QAudioBufferOutput` is FFmpeg-backend-only.** Qt's reference page says so outright, and it was measured here: a 1.2 s tone produced **zero** `audioBufferReceived` emissions while playing normally, with `Available backends QList("darwin")` and a Qt multimedia build that ships `libdarwinmediaplugin` and no `libffmpegmediaplugin`. Setting `QT_MEDIA_BACKEND=ffmpeg` does not help — Qt logs the request and falls back to the only backend it has. `VisualizerAudioTap` is the fallback: when no buffer has arrived, it decodes the selected **local** file with the repository's own FFmpeg decoder and pushes the same PCM into the same queue. `QMediaPlayer` remains the only audio output and the master clock; the tap is not a second playback path. The first delivered buffer clears it for the rest of the session, so a backend that does support `QAudioBufferOutput` pays nothing. That is also what keeps the queue single-producer — see `INVARIANTS.md`.

`VideoRecorder` owns a `VideoRecorderThread` worker. That worker consumes queued video frames, drains the recorder audio queue when available, and performs FFmpeg encoding away from the QML/render loop. `VisualizerRenderer` owns the GL-side capture resources, while the recorder owns encoding state and output.

`SunoClient` owns its `QNetworkAccessManager` and authenticated request queue. The queue timer dispatches at most one queued request per second. The source does not create a separate Suno network thread; Qt delivers asynchronous manager replies through the event loop. `SunoController` creates a separate `QNetworkAccessManager` for `SunoDownloader`/download-queue work.

## QML Bridge Registration and Lifetime

`qml_bridge::registerBridges()` registers the QML singleton factories before `main.qml` is loaded and then injects the backend pointers. Registration does not necessarily construct each singleton: the factory can remain lazy until first QML access.

`QmlSingletonBridge` makes the ownership policy explicit:

- `CachedQmlParented` creates one instance and parents it to the `QQmlEngine`. `SettingsBridge` and `PresetBridge` use this policy.
- `CachedUnparented` creates one cached instance without a QObject parent. `SunoBridge` uses this policy because it also retains non-owning backend pointers that must be installed independently of QML construction.

**`Theme` is QML-only, and it stays that way.** `src/qml_bridge/ThemeBridge.{hpp,cpp}` existed to expose accent/background as QML properties with a `Config::save()` on each write, and was registered under the name `Theme`. It could never be: the QML-side `styles/Theme.qml` singleton already owns that name and carries the full token set, so registering a C++ bridge under it would shadow the QML singleton and leave most tokens — `fontDisplay`, `spacingXL`, the shadow scale, and the rest — undefined at runtime. So the bridge was compiled, linked, and unreachable (T0055).

It has been **deleted, not repaired**. The role that meant to play — persisting the two appearance colours — is filled by `SettingsBridge`'s `accentColor`/`backgroundColor` properties, which `Theme.applyAccent()` and `Theme.applyBackground()` write through and which main.qml's `Component.onCompleted` restores from. The ordering there is load-bearing: seeding from `Theme.qml`'s own `onCompleted` does not work, because a singleton in a `qt_add_qml_module` is instantiated lazily and is not ordered after `SettingsBridge`'s construction (T0047). A bridge that cannot be registered under its intended name and whose only purpose is already served elsewhere is not a bridge.

There is exactly **one owner of the theme now**: `styles/Theme.qml` for the tokens, `SettingsBridge` for the persisted values. That remains true, and a **theme is a definition rather than a set of loose colours** ([THEME-ENGINE-PLAN.md](THEME-ENGINE-PLAN.md), T0190).

A theme definition supplies the tokens it cares about as `#RRGGBBAA` strings in `styles/ThemeDefinitions.qml`; every token it does not supply is **derived** from `accent` and `background` by `Theme.ramp()` — moving each channel a fraction of the distance toward the opposite pole. So one mechanism covers a named theme, a theme with three overrides, and a user's own two colours, and adding a fifth built-in is one data entry rather than a code change. Derivation is relative, not multiplicative, deliberately: `Qt.lighter(black, 2)` is still black, so the previous `Qt.lighter(resolved, 1.05..1.35)` chain derived black from black five times over and flattened the whole UI to one shade.

`overrides` is **replaced wholesale, never mutated in place** — QML does not reliably re-evaluate a binding that reads a member of a `var` object mutated underneath it. `Theme.applyThemeById()` is the only function that assigns it, and startup, the Settings selector, the colour picker and reset all go through it. A second apply path, in QML or in C++, is a bug: two paths are how the palette drifts out of sync with the persisted config.

`accent_color` and `visualizer_background` stay in `config.toml` because they are the two values a `custom` theme needs, and every key `ConfigParsers` writes is in a `CHADVIS_*_FIELDS` table — `serialize()` rebuilds the whole file, so a field absent from that table is silently dropped on the next save.

**`--theme` is session-only, structurally** (T0192). It is a `CONSTANT` QML property mirroring a static, set before the QML engine loads. It is not in `ConfigParsers` and not in the `SettingsBridgeSettings.inc` table, because the old code called `applyOverride(opts.theme, CONFIG.ui().theme, ...)` — which writes the live `Config` and is therefore persisted by the next save. That was invisible while the flag did nothing; the moment the engine made it real, every launch with `--theme nord` would have overwritten the user's saved theme.

`SettingsBridgeSettings.inc` is an X-macro table included twice — once to generate getters and setters, once to generate `resetToDefaults()` as emits. Adding a property needs one line there **plus three** in `SettingsBridge.hpp` (`Q_PROPERTY`, getter/setter, signal), and forgetting the emit-only redefinition of the macro compiles as a function definition inside a function.

`SunoBridge` has a startup-order invariant. `registerBridges()` calls `SunoBridge::setSunoController()` before `QQmlApplicationEngine::load()`, so the static controller pointer is set while the singleton is still unconstructed. The first QML access constructs `SunoBridge`, whose constructor wires the already-installed controller. If the singleton already exists, `setSunoController()` must wire it. For that reason, wiring exists in both the constructor and `setSunoController()`; removing either path can leave library, authentication, account, and error signals undelivered.

## Authentication

Google sign-in is a **system-browser flow, not an OAuth client** (T0181). Clerk
owns the exchange: the OAuth `redirect_uri` is
`https://auth.suno.com/social/complete/google-oauth2/` and the authorization
`code` never leaves the browser↔Clerk boundary — [suno_api/LOGIN-CAPTURE-FINALIZATION.md](../suno_api/LOGIN-CAPTURE-FINALIZATION.md)
records that no `127.0.0.1` address appears anywhere in a 1379-entry HAR. So
this program has no client_id, no redirect_uri, no code to exchange, and
nothing to wait for.

What that leaves, and what exists:

1. `AuthCoordinator::beginGoogleSignIn()` opens the system browser on Suno's
   sign-in page and reports only whether the launch succeeded. It has no
   timeout, no state machine, and no cancel — there is nothing to cancel.
2. The user completes Google login in their browser.
3. The credential comes back through the **paste field** on the Account
   settings page ("Paste session cookie header instead"), and is stored via
   `CredentialStore`, which on a secure backend means the OS keychain.
4. `ClerkAuthClient::fetchBearer()` does `GET /v1/client` and reads
   `sessions[0].last_active_token.jwt`; `touch()` POSTs
   `/v1/client/sessions/{sid}/tokens` for the same JWT.

The loopback machinery that used to occupy this section (`LoopbackListener`,
`OAuthLoginService`, `OAuthTransaction`) is deleted — it bound a socket no
request could reach, and its capture gate meant the feature failed 100% of the
time. **Do not reintroduce it** without a capture showing a `127.0.0.1`
redirect_uri, which this one rules out.

The open question is automating step 3. A bookmarklet that POSTs
`document.cookie` to a local listener would remove the paste, but
`document.cookie` cannot read an `HttpOnly` cookie, and Clerk's `Set-Cookie`
attributes were stripped from the HAR. That is board item T0182 and it is
blocked on a capture, not on code.

## Suno Request and Data Flow

The primary library flow is:

`SunoClient` → `SunoLibraryManager` → `SunoController` → `SunoBridge` → QML

In the opposite direction, QML calls enter `SunoBridge`, which delegates to `SunoController` and its managers.

- `SunoClient` is the rate-limited HTTP surface. Authentication policy and Clerk protocol details are delegated to `src/suno/auth/`, including credential storage, JWT handling, request headers, and bearer refresh.
- `SunoLibraryManager` requests `feed/v3` pages, follows the returned cursor, accumulates clips, saves each page to `SunoDatabase`, and emits progressive updates.
- `SunoController` is the facade over `SunoClient`, `SunoLibraryManager`, `SunoAccountManager`, `SunoDownloader`, `SunoLyricsManager`, `SunoDatabase`, and `SunoOrchestrator`. When authentication becomes valid, it bootstraps the session catalog and billing snapshot.
- `SunoBridge` converts clip state into QML properties and forwards generation, search, pagination, chat, authentication, account, and error events to the views.

All centralized Suno URL constants live in `src/suno/SunoEndpoints.hpp`. Entries are marked `[T1]` when observed in the repository's listed captures and `[LEAD]` when they are recon-only or constructed. Code must preserve those tiers and must not promote a `[LEAD]` without capture evidence.
