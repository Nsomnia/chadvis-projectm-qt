


Based on a comprehensive analysis of the provided header files, here is a detailed, ranked list of suggestions, architectural enhancements, fixes, and third-party library recommendations for the `nsomnia-chadvis-projectm-qt` codebase.

This list is designed for an autonomous agent (like opencode) to consume, explore, and implement. 

### Ranking System Definition
*   **[P0] Critical / Architectural Blockers**: Immediate fixes required for stability, thread-safety, or modern framework compliance.
*   **[P1] High Impact / Performance**: Major optimizations (zero-copy, lock-free) and highly valuable feature integrations.
*   **[P2] Enhancements / Features**: QoL improvements, library integrations, and robust API handling.
*   **[P3] Tech Debt / Cleanup**: Refactoring, directory structure updates, and modernization.

---

### 1. Concurrency & Performance (Audio & Video)
**[P0] Replace Mutexes in Audio Callbacks with Lock-Free Queues**
*   **File**: `VideoRecorderThread.hpp`, `AudioEngine.hpp`
*   **Issue**: `audioMutex_` is being locked inside audio processing callbacks (e.g., `pushAudioSamples`). Audio threads are real-time; acquiring a mutex can cause priority inversion and audio dropouts (glitching).
*   **Fix**: Introduce a lock-free Single-Producer-Single-Consumer (SPSC) queue.
*   **Third-Party**: Fetch `moodycamel::ReaderWriterQueue` via CPM. Use it to pass audio chunks from the Qt/SDL audio thread to the `VideoRecorderThread` and `VisualizerRenderer` without locking.

**[P1] Zero-Copy Hardware Video Encoding**
*   **File**: `VideoRecorderFFmpeg.hpp`, `FrameGrabber.hpp`
*   **Issue**: The current `AsyncFrameGrabber` uses PBOs to pull frames from the GPU to the CPU, and then pushes them back to FFmpeg for encoding.
*   **Enhancement**: Implement zero-copy hardware encoding (NVENC, VAAPI, VideoToolbox). Share the OpenGL context with FFmpeg's `hw_frames_ctx` (e.g., via CUDA/GL interop or DMA-BUF) so frames stay on the GPU.

**[P1] Centralize and Optimize Circular Buffers**
*   **File**: `AudioAnalyzer.hpp` (`CircularBuffer`), `VisualizerRenderer.hpp` (`AudioCircularBuffer`)
*   **Issue**: There are multiple custom circular buffer implementations. 
*   **Enhancement**: Unify these into a single template in `src/util/RingBuffer.hpp`. Ensure memory alignment (`alignas(64)`) to prevent false sharing if accessed across threads.

### 2. Qt 6 & QML Modernization
**[P0] Modernize QML Registration**
*   **File**: `BridgeRegistration.hpp`, `*Bridge.hpp`
*   **Issue**: The codebase uses manual singleton registration (e.g., `qmlRegisterSingletonType`, static `create()` functions).
*   **Fix**: Use Qt 6's declarative registration macros. Add `QML_ELEMENT` or `QML_SINGLETON` directly to the bridge classes, and utilize `qt_add_qml_module` in CMake. This enables `qmltc` (QML Type Compiler) ahead-of-time compilation for massive UI performance gains.

**[P0] Address Qt 6 RHI (Rendering Hardware Interface) Constraints**
*   **File**: `VisualizerItem.hpp`
*   **Issue**: Qt 6 uses RHI (Vulkan/Metal/D3D11) by default, but `VisualizerItem` directly invokes `QOpenGLFunctions_3_3_Core` via `beforeRendering()`.
*   **Fix**: Ensure the `Application` class explicitly forces the OpenGL backend (`QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL)`). *Future Proofing*: Evaluate wrapping ProjectM rendering in a `QQuickFramebufferObject` (QFBO) which plays nicer with Qt's modern threading.

**[P2] Eliminate QWebEngine Bloat**
*   **File**: `SunoPersistentAuth.hpp`, `SunoCookieDialog.hpp`
*   **Issue**: Pulling in `QWebEngineView` just to capture an auth cookie increases the application binary size by ~100-150MB and dramatically increases RAM usage.
*   **Enhancement**: Reverse-engineer the Clerk.dev auth flow (which Suno uses) to implement a lightweight headless REST auth, OR rely entirely on `SystemBrowserAuth` (OAuth style localhost redirect) and deprecate the WebEngine approach entirely.

### 3. Architecture & Code Quality
**[P1] Standardize Signal/Slot Mechanics**
*   **File**: `AudioEngine.hpp`, `SunoController.hpp`
*   **Issue**: Mixing `Q_OBJECT` with the custom `vc::Signal` (e.g., `Signal<> trackChanged;`). 
*   **Fix**: If a class is a `QObject`, use standard Qt `signals:`. Qt handles thread-safe cross-thread signal queuing automatically. The custom `vc::Signal` lacks queued connections, which is dangerous when the `VideoRecorderThread` emits a `statsUpdated` signal to the GUI thread.

**[P2] Modern C++ Error Handling**
*   **File**: `util/Result.hpp`
*   **Issue**: The custom `Result<T>` is good but lacks ecosystem compatibility.
*   **Enhancement**: Migrate to `tl::expected` (via CPM) or `std::expected` (if fully on C++23). This provides standard monadic operations (`and_then`, `transform`, `or_else`) and integrates seamlessly with modern C++ libraries.

**[P3] Graveyard Cleanup & Directory Restructure**
*   **File**: `.backup_graveyard/ui.*` and `src/ui/`
*   **Issue**: The codebase is mid-transition from Qt Widgets to QML. `src/ui/` currently holds Auth and Controllers, which doesn't match the QML architecture.
*   **Fix**: 
    *   Delete `.backup_graveyard` to clear context bloat for the agent.
    *   Rename `src/ui/controllers` to `src/qml_bridge/controllers`.
    *   Move `SunoPersistentAuth` and `SystemBrowserAuth` to `src/suno/auth/`.

### 4. Third-Party Library Recommendations (Via CPM)
**[P1] Fast JSON Parsing (simdjson / nlohmann_json)**
*   **File**: `SunoLyrics.hpp`, `SunoController.cpp`
*   **Issue**: Qt's `QJsonDocument` is heavily heap-allocating and relatively slow for large API responses.
*   **Library**: Add `nlohmann/json` or `simdjson` via CPM. Create automatic struct mappers using `NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE` to parse `SunoClip` and `SunoMetadata` instantly.

**[P1] Local Audio Alignment (whisper.cpp)**
*   **File**: `LyricAligner.hpp`
*   **Issue**: `PlaceholderAligner` is currently unimplemented.
*   **Library**: Import `ggerganov/whisper.cpp` via CPM. Implement a local forced-alignment engine to generate precise `.srt` / `.lrc` word-level timestamps directly from the audio file when Suno API doesn't provide them.

**[P2] Robust HTTP Client (cpr)**
*   **File**: `SunoClient.hpp`
*   **Issue**: `QNetworkAccessManager` is verbose and requires heavy signal/slot tracking for queues (`std::deque<PendingRequest>`).
*   **Library**: Add `libcpr/cpr` (C++ Requests). It handles session cookies, bearer tokens, and synchronous/asynchronous requests using standard `std::future`, dramatically simplifying `SunoClient`.

**[P2] High-Performance FFT (FFTW3 or muFFT)**
*   **File**: `AudioAnalyzer.hpp`
*   **Issue**: Code mentions KissFFT, which is decent but not optimally SIMD-accelerated on all platforms.
*   **Library**: Include `muFFT` or `FFTW` to minimize CPU footprint on the audio thread during 60fps spectrum analysis.

### 5. Feature Enhancements
**[P2] Karaoke Renderer Upgrades**
*   **File**: `LyricsRenderer.hpp`
*   **Enhancement**: Add "Ruby Text" (Furigana-style phonetic guides) support for international lyrics. Implement vertex-shader-based text glow rather than CPU-side `QPainter` shadow drawing, which is very slow at 60fps when rendering over 4K video.

**[P2] Hardware Audio Resampling**
*   **File**: `VideoRecorderFFmpeg.hpp`
*   **Enhancement**: `libswresample` is used to convert audio. Ensure it is configured to use AVX/NEON optimizations, or switch to `libsoxr` (SoX Resampler) for strictly higher quality audio output during video generation.

**[P3] Configuration Migration Tool**
*   **File**: `ConfigLoader.hpp`, `ConfigParsers.hpp`
*   **Enhancement**: TOML is great. Ensure there is a robust versioning strategy in the TOML file (e.g., `config_version = 1`) so that future updates to the struct don't break existing user configs or crash the TOML parser.

---

### Suggested Agent Implementation Plan (For Opencode)

1. **Phase 1: Cleanup & Migration (`P0` / `P3`)**
   * Instruct the agent to delete the graveyard, restructure the `src/ui` directory, and replace all `vc::Signal` instances in `QObject` inherited classes with standard Qt signals.
   * Update QML bridges to use Qt 6 declarative macros.

2. **Phase 2: Thread Safety & Concurrency (`P0`)**
   * Introduce a lock-free queue library (like moodycamel) via `CMakeLists.txt` using `CPMAddPackage`.
   * Rewrite `VideoRecorderThread::pushAudioSamples` and `AudioAnalyzer::analyze` to strictly avoid `std::mutex` locking in the audio callback.

3. **Phase 3: Dependency Modernization (`P1`)**
   * Swap `QJsonDocument` for `nlohmann_json` in the `suno` directory.
   * Replace `QNetworkAccessManager` in `SunoClient` with `cpr` for cleaner async/await style API flows.

4. **Phase 4: Advanced Features (`P1` / `P2`)**
   * Implement `whisper.cpp` into `PlaceholderAligner`.
   * Refactor FFmpeg Video Encoding to attempt GPU-based `hw_frames_ctx` allocation.