# ChadVis Codebase Deep Analysis — Agentic Audit Report

---

## CRITICAL / P0 — Security, Correctness, Data Loss

### 1. `AudioAnalyzer::performFFT` — Static Local Variables in Potentially Multi-Threaded Context
**File:** `src/audio/AudioAnalyzer.cpp:73-77`
```cpp
static std::array<f32, FFT_SIZE> work;
static std::array<f32, FFT_SIZE> output;
```
**Problem:** `static` locals are shared across all calls. If `analyze()` were ever called from multiple threads (or if the analyzer thread races with a reset), these are data races. Even single-threaded, they prevent reentrancy and make the class non-reentrant.
**Fix:** Make `work` and `output` member variables or use `thread_local`.

### 2. `AudioAnalyzer::pcmBuffer_` — CircularBuffer Copies on FFT Read
**File:** `src/audio/AudioAnalyzer.cpp:79-81`
```cpp
std::array<f32, FFT_SIZE> contiguous;
for (usize i = 0; i < FFT_SIZE; ++i) {
    contiguous[i] = input[i];  // O(N) copy every frame
}
```
**Problem:** `CircularBuffer` already provides `getSpans()` returning up to two contiguous spans. This O(N) copy defeats the purpose of the circular buffer optimization comment ("O(1) push"). PFFFT can process two spans or the copy can use `memcpy` on spans.
**Fix:** Use `input.getSpans()`, copy via `memcpy` from spans, or restructure PFFFT call to handle non-contiguous input.

### 3. `AudioEngine::processAudioBuffer` — Unsafe Scratch Buffer Resize in Audio Callback
**File:** `src/audio/AudioEngine.cpp:175-177`
```cpp
if (scratchBuffer_.size() < totalSamples) scratchBuffer_.resize(totalSamples);
```
**Problem:** `resize()` can allocate in the audio callback path. This is an audio anti-pattern — allocation in real-time paths causes priority inversion and glitches. The buffer should be pre-allocated to worst-case size.
**Fix:** Pre-allocate `scratchBuffer_` in `init()` to max expected size (e.g., 48000 * 2 * max_buffer_ms / 1000).

### 4. `Application::~Application` — Destruction Order Bug
**File:** `src/core/Application.cpp:44-48`
```cpp
qmlEngine_.reset();
visualizerWindow_.reset();
videoRecorder_.reset();
audioEngine_.reset();
qapp_.reset();
```
**Problem:** Member destruction happens in reverse declaration order. The explicit `.reset()` calls may conflict with implicit destruction. If `qmlEngine_` holds references to `audioEngine_`, the explicit reset order is correct, but the implicit destructor will also fire — double-free or use-after-free is possible if any shared ownership exists. More critically, `sunoController_`, `presetManager_`, `lyricsSync_` are NOT explicitly reset, so they destroy in declaration order which may be before `qmlEngine_` is fully torn down.
**Fix:** Either use explicit reset for ALL members in correct order, or rely entirely on declaration order (reorder declarations to match desired destruction).

### 5. `Playlist::loadM3U` — Path Traversal Vulnerability
**File:** `src/audio/Playlist.cpp:203-207`
```cpp
if (!filePath.is_absolute()) {
    filePath = path.parent_path() / filePath;
}
```
**Problem:** No sanitization. A malicious M3U file with `../../etc/passwd` would resolve to an arbitrary path. While `fs::exists()` prevents loading non-existent files, the path traversal itself is unvalidated.
**Fix:** Canonicalize the path and verify it's under an allowed directory.

### 6. `SunoPersistentAuth` / `SystemBrowserAuth` — Referenced but Not Shown
**Files:** `src/ui/SunoPersistentAuth.cpp`, `src/ui/SystemBrowserAuth.cpp`
**Problem:** These handle authentication tokens. Without seeing the code, the file names suggest persistent storage of auth credentials. If tokens are stored in plaintext (common in hobby projects), this is a credential exposure risk.
**Action:** Audit these files for plaintext token storage.

---

## HIGH / P1 — Performance, Architecture, Modernization

### 7. `AudioQueue` — Triple Queue Redundancy
**File:** `src/audio/AudioQueue.hpp`
**Problem:** Three separate SPSC queues (`vizQueue_`, `recQueue_`, `anaQueue_`) with identical `pushInternal` logic. `pushAll()` pushes the same data to all three. This triples memory usage and cache pressure.
**Fix:** Use a single queue with multiple consumer cursors (SPMC pattern), or a single queue with a fan-out proxy. Alternatively, use `moodycamel::ConcurrentQueue` which natively supports multi-consumer.

### 8. `AudioAnalyzer::detectBeat` — Naive Beat Detection
**File:** `src/audio/AudioAnalyzer.cpp:89-105`
**Problem:** Energy-ratio beat detection with a fixed threshold (1.5) and fixed history window (60 frames). This is a 1990s approach. Modern beat detection uses:
- Spectral flux onset detection
- Autocorrelation for tempo estimation
- Multi-band energy analysis
- Adaptive thresholding (not fixed 1.5)
**Fix:** Integrate `librosa`-style onset detection or at minimum use spectral flux. Consider `aubio` library for production-grade beat detection.

### 9. `AudioAnalyzer::analyze` — No Windowing Function
**File:** `src/audio/AudioAnalyzer.cpp:55-63`
**Problem:** Raw PCM is fed directly into FFT without a windowing function (Hann, Hamming, Blackman, etc.). This causes spectral leakage — frequency bins smear energy from adjacent frequencies, degrading visualizer accuracy.
**Fix:** Apply a Hann window before FFT:
```cpp
for (usize i = 0; i < FFT_SIZE; ++i) {
    contiguous[i] *= 0.5f * (1.0f - std::cos(2.0f * M_PI * i / (FFT_SIZE - 1)));
}
```

### 10. `AudioEngine` — No Gapless Playback Implementation
**File:** `src/audio/AudioEngine.cpp:82-94`
**Problem:** `nextPlayer_` is preloaded for gapless playback, but `swapPlayers()` disconnects and reconnects signals, which introduces a gap. True gapless requires crossfade or shared audio output buffer.
**Fix:** Use `QMediaPlayer`'s native playlist support or implement crossfade with two `QAudioOutput` instances mixed into a single device.

### 11. `Application::init` — Monolithic 150+ Line Function
**File:** `src/core/Application.cpp:120-260`
**Problem:** `init()` handles: logging, config loading, CLI overrides, Qt surface setup, QApplication creation, style setup, audio engine init, video recorder init, rating manager init, preset manager init, visualizer window creation, lyrics sync init, Suno controller init, QML engine creation, QML loading, signal connections. This is a god function.
**Fix:** Extract into `initLogging()`, `initConfig()`, `applyCliOverrides()`, `initQt()`, `initAudio()`, `initVisualizer()`, `initQml()`, `connectSignals()`.

### 12. CLI Parsing — X-Macro Pattern is Fragile and Unmaintainable
**Files:** `src/core/CliArgs.inc`, `src/core/Application.cpp:60-150`
**Problem:** The X-macro pattern requires 5 separate `#define` + `#include` + `#undef` blocks. Each block must define ALL 5 macro types even if only one is used. This is error-prone and makes debugging impossible (no line numbers in macro expansions).
**Fix:** Use C++23 `std::expected` with a declarative argument descriptor table and a single generic parser:
```cpp
struct CliFlag { std::string_view name; CliArgType type; void* target; };
constexpr std::array<CliFlag, N> flags = { ... };
// Single parse loop with std::visit on type
```

### 13. `Playlist` — No Thread Safety
**File:** `src/audio/Playlist.hpp`, `src/audio/Playlist.cpp`
**Problem:** `Playlist` is accessed from the main thread (UI) and potentially from the audio engine thread (via signals). No mutex, no lock-free structure. `items_` vector is mutated during `addFile()`, `removeAt()`, `move()` while `currentItem()` may be called from another thread.
**Fix:** Add a `std::shared_mutex` for reader-writer access, or use a lock-free design with immutable snapshots.

### 14. `AudioSpectrum` — Passed by Value in Signal
**File:** `src/audio/AudioEngine.hpp:78`
```cpp
void spectrumUpdated(const AudioSpectrum& spectrum);
```
**Problem:** `AudioSpectrum` contains `std::array<f32, 1024>` (4KB). Passing by const reference across a signal/slot boundary with queued connections causes a copy into the event queue. This is fine for direct connections but expensive for queued connections across threads.
**Fix:** Use `std::shared_ptr<const AudioSpectrum>` or move semantics for cross-thread signals.

### 15. `MediaMetadata::extractAlbumArt` — Only Supports MP3 and FLAC
**File:** `src/audio/analysis/MediaMetadata.cpp:110-145`
**Problem:** Album art extraction only handles `.mp3` (ID3v2/APIC) and `.flac`. OGG Vorbis, Opus, WMA, AAC/M4A (iTunes atoms) are all unsupported despite TagLib supporting them.
**Fix:** Use TagLib's generic `FileRef` API or add handlers for OGG (XiphComment), M4A (MP4::CoverArt), etc.

### 16. `CircularBuffer::getSpans()` — Defined but Never Used
**File:** `src/audio/AudioAnalyzer.hpp:37-46`
**Problem:** `getSpans()` is implemented for zero-copy FFT but `performFFT()` does a manual O(N) copy loop instead. Dead/unused code.
**Fix:** Use `getSpans()` in `performFFT()` or remove it.

---

## MEDIUM / P2 — Code Quality, Maintainability, Modernization

### 17. No `[[nodiscard]]` on `Result<T>` Returns
**Files:** Multiple
**Problem:** Functions returning `Result<T>` (e.g., `init()`, `parseArgs()`, `saveM3U()`, `loadM3U()`) don't use `[[nodiscard]]`. Callers can silently ignore errors.
**Fix:** Add `[[nodiscard]]` to all `Result<T>`-returning functions.

### 18. `vc::Signal` — Custom Signal Implementation
**File:** `src/util/Signal.hpp` (referenced but not shown)
**Problem:** Custom signal/slot when Qt already provides `Q_OBJECT` signals. This creates two parallel event systems. If `Signal` is synchronous (direct invocation), it's a re-entrancy hazard. If it's thread-safe, it's redundant with Qt.
**Fix:** Audit `Signal.hpp`. If it's simple synchronous callbacks, replace with `std::function` vectors or Qt signals. If it's for non-QObject types, document why.

### 19. `Result<void>` — Custom Result Type
**File:** `src/util/Result.hpp` (referenced but not shown)
**Problem:** C++23 has `std::expected<T, E>` which is the standard equivalent. A custom `Result<T>` adds maintenance burden and diverges from standard patterns.
**Fix:** Replace `Result<T>` with `std::expected<T, ErrorInfo>` where `ErrorInfo` holds `.message`.

### 20. `Duration` Type — Unclear Definition
**Files:** Multiple
**Problem:** `Duration` is used throughout but its definition is not shown. If it's `std::chrono::milliseconds`, the code `item.metadata.duration.count() / 1000` in M3U saving is correct but fragile. If it's a custom type, it may lack chrono interoperability.
**Fix:** Ensure `Duration` is `std::chrono::milliseconds` or a strong typedef with proper conversions.

### 21. Magic Numbers Throughout
**Files:** Multiple
```cpp
spectrum.beatIntensity > 1.1f  // AudioAnalyzer.cpp:70
smoothingFactor_{0.3f}         // AudioAnalyzer.hpp:88
beatThreshold_{1.5f}           // AudioAnalyzer.hpp:85
energyHistory_.resize(60, 0.0f) // AudioAnalyzer.cpp:6
std::chrono::milliseconds(5)   // AudioEngine.cpp:158
```
**Fix:** Extract to named constants or config values.

### 22. `Application::printHelp` — 80+ Lines of Hardcoded Console Output
**File:** `src/core/Application.cpp:280-360`
**Problem:** Help text is hardcoded in C++ source. Adding/removing a flag requires editing `CliArgs.inc`, the help function, and potentially the help topic system.
**Fix:** Generate help text programmatically from the `CliArgs.inc` table (the X-macro pattern already supports this — just add a `#define` that prints).

### 23. `AudioEngine::analyzerWorker` — Busy-Wait with Fixed Sleep
**File:** `src/audio/AudioEngine.cpp:153-162`
```cpp
std::this_thread::sleep_for(std::chrono::milliseconds(5));
```
**Problem:** Fixed 5ms sleep when queue is empty wastes CPU and adds latency. Should use condition variable or semaphore.
**Fix:** Add a semaphore that `pushAll()` signals, and `analyzerWorker()` waits on.

### 24. `Playlist::addFile` — Synchronous Metadata Reading
**File:** `src/audio/Playlist.cpp:12-18`
**Problem:** `MetadataReader::read(path)` is called synchronously. For large files (FLAC, WAV), this blocks the UI thread. Adding 100 files would freeze the UI for seconds.
**Fix:** Read metadata asynchronously via `QtConcurrent::run` or a background thread with progress reporting.

### 25. `AudioEngine::loadLastPlaylist` — No Error Recovery
**File:** `src/audio/AudioEngine.cpp:137-140`
**Problem:** If the M3U file is corrupted or contains invalid paths, `loadM3U` logs warnings but continues. However, there's no user feedback about partial load failures.
**Fix:** Return a count of loaded vs. failed items and emit a signal for UI notification.

### 26. No `constexpr` Usage for Compile-Time Constants
**Files:** `AudioAnalyzer.hpp`, `AudioQueue.hpp`
**Problem:** `FFT_SIZE`, `SPECTRUM_SIZE`, `AUDIO_FRAME_SAMPLES`, `DEFAULT_QUEUE_CAPACITY` are `constexpr` (good), but many other constants (thresholds, buffer sizes) are runtime-initialized member variables that could be `constexpr`.
**Fix:** Audit all constants and make compile-time where possible.

### 27. `MediaMetadata::formatLine` — Inefficient String Replacement
**File:** `src/audio/analysis/MediaMetadata.cpp:30-45`
**Problem:** Multiple `while` loops with `find` + `replace` for each placeholder. Each replacement scans the entire string. For N placeholders and M occurrences, this is O(N*M*len).
**Fix:** Single-pass scan with a replacement map:
```cpp
std::string result;
result.reserve(format.size() * 2);
// scan for {key} and replace in one pass
```

### 28. `AudioFrame` — `alignas(64)` May Be Insufficient
**File:** `src/audio/AudioQueue.hpp:22`
```cpp
struct alignas(64) AudioFrame {
    float samples[AUDIO_FRAME_SAMPLES * 2]; // 8 * 2 * 4 = 64 bytes
    u32 sampleCount;  // 4 bytes
    u32 channels;     // 4 bytes
    u32 sampleRate;   // 4 bytes
    // Total: 76 bytes, padded to 128 (next multiple of 64)
};
```
**Problem:** The struct is 76 bytes but aligned to 64, so it's padded to 128 bytes. This wastes 52 bytes per frame. With 36000 frames per queue and 3 queues, that's ~5.4MB wasted.
**Fix:** Reduce `AUDIO_FRAME_SAMPLES` to 7 (56 + 12 = 68, padded to 128 — same waste) or restructure to fit in exactly 64 bytes (8 samples * 2 channels * 4 bytes = 64, move metadata outside).

### 29. `Application` — Singleton Pattern via Raw Pointer
**File:** `src/core/Application.hpp:105`
```cpp
static Application* instance_;
#define APP vc::Application::instance()
```
**Problem:** Raw pointer singleton with no thread safety on access. The `#define APP` pollutes the global namespace.
**Fix:** Use `std::atomic<Application*>` or Meyer's singleton. Replace `#define APP` with an inline function.

### 30. `AudioEngine::processAudioBuffer` — No Sample Format Validation
**File:** `src/audio/AudioEngine.cpp:175-190`
**Problem:** Only handles `Float` and `Int16` formats. `QAudioFormat` also supports `Int32`, `UInt8`, `NSampleFormats`. Unknown formats silently produce garbage.
**Fix:** Add `default:` case that logs an error and returns.

---

## LOW / P3 — Style, Documentation, Minor Issues

### 31. Inconsistent Namespace Usage
**Files:** Multiple
**Problem:** Some files use `namespace vc { ... }`, others qualify with `vc::`. The `main.cpp` doesn't use a namespace at all (correct for entry point). Internal anonymous namespaces are used in some files but not others.

### 32. `#pragma once` — Non-Standard but Universal
**Files:** All headers
**Problem:** `#pragma once` is not in the C++ standard. While universally supported, it can fail with symlinks or mounted filesystems.
**Action:** Acceptable for this project, but note the risk.

### 33. Missing `#include` Guards in `.inc` Files
**File:** `src/core/CliArgs.inc`
**Problem:** The `.inc` file is designed for multiple inclusion with different macro definitions, which is correct for X-macros. But there's no comment explaining this pattern for future maintainers.

### 34. `AudioAnalyzer::pcmData()` — Unnecessary Copy Loop
**File:** `src/audio/AudioAnalyzer.hpp:65-72`
```cpp
std::vector<vc::f32> result;
result.reserve(pcmBuffer_.size());
for (usize i = 0; i < pcmBuffer_.size(); ++i) {
    result.push_back(pcmBuffer_[i]);
}
```
**Problem:** Manual loop instead of using iterators or `getSpans()` + `std::copy`. The comment says "returns copy for API compatibility" but the copy is inefficient.
**Fix:** Use `getSpans()` and `std::copy` from spans.

### 35. `#include <projectM-4/projectM.h>` in `AudioEngine.hpp`
**File:** `src/audio/AudioEngine.hpp:2`
**Problem:** `AudioEngine.hpp` includes `projectM-4/projectM.h` but never uses any projectM types in the header. This is an unnecessary dependency injection that slows compilation.
**Fix:** Remove the include from the header; add it only to `.cpp` files that need it.

### 36. `AudioQueue` — No Move Semantics for `AudioFrame`
**File:** `src/audio/AudioQueue.hpp:22-33`
**Problem:** `AudioFrame` contains a C-array and trivial types. It's trivially copyable, so move = copy. This is fine, but the `try_enqueue`/`try_dequeue` from moodycamel already handle this correctly. No issue, just noting.

### 37. `CliUtils::findClosestMatch` — Cut Off
**File:** `src/core/CliUtils.cpp` (end of file)
**Problem:** The file appears to be truncated — `findClosestMatch` function signature is cut off. This means the codebase is incomplete as provided.
**Action:** Flag for complete file review.

### 38. README — Humor Over Documentation
**File:** `README.md`
**Problem:** While entertaining, the README lacks:
- Actual build instructions for non-Arch systems (Ubuntu, Fedora, macOS, Windows)
- CI/CD badge links to actual build logs
- Architecture diagram
- License compatibility notes for dependencies (projectM is LGPL, PFFFT is BSD, etc.)
- Minimum compiler versions (GCC 13? Clang 17?)
**Fix:** Add a "Supported Platforms" section and dependency license matrix.

### 39. `docs/suno_api/README.md` — Reverse-Engineered API Documentation
**File:** `docs/suno_api/README.md`
**Problem:** Documents reverse-engineered Suno API endpoints. This is legally gray (ToS violation at minimum) and the endpoints will break without notice. The `430` status code is non-standard.
**Fix:** Abstract the Suno client behind an interface so the implementation can be swapped. Add circuit-breaker pattern for API failures.

### 40. Missing `.clang-tidy`, `.clang-format`, `CMakeLists.txt`
**Problem:** No static analysis configuration, no formatting rules, no build system file shown. For a C++23 project, this means:
- No enforced coding standard
- No automated bug detection (clang-tidy checks: modernize-*, bugprone-*, performance-*)
- No reproducible builds
**Fix:** Add `.clang-tidy` with C++23 checks, `.clang-format` with project style, and ensure `CMakeLists.txt` uses `CMAKE_CXX_STANDARD 23`.

---

## ARCHITECTURAL RECOMMENDATIONS

### A. Dependency Injection Over Singletons
`Application::instance()`, `RatingManager::instance()`, `CONFIG` (macro?) are all global singletons. This makes testing impossible and creates hidden coupling. Use constructor injection for all dependencies.

### B. Separate Audio Processing from UI
`AudioEngine` inherits `QObject` and uses Qt signals. The audio processing (FFT, beat detection) should be a pure C++ library with no Qt dependency, testable in isolation. Only the bridge layer should use Qt.

### C. Consider `std::execution` (C++26) or `std::jthread` Patterns
The `analyzerWorker` is a raw `std::jthread` with manual sleep. Use `std::stop_token` (already available via `jthread`) with a condition variable for proper cancellation.

### D. QML Architecture
The QML files are referenced but not shown. The bridge pattern (`AudioBridge`, `LyricsBridge`, etc.) suggests many small bridge classes. Consider a single `BackendBridge` with namespaced properties to reduce boilerplate.

### E. Build System
`build.sh` is referenced but not shown. Ensure CMake is the primary build system with proper:
- `FetchContent` for dependencies
- `compile_commands.json` generation
- Sanitizer builds (ASAN, TSAN, UBSAN)
- Unity builds for faster compilation

---

PRIORITY MATRIX

Priority	Count	Categories
P0 Critical	6	Thread safety, data races, path traversal, destruction order, credential storage
P1 High	10	Performance (FFT, beat detection, metadata), architecture (monolith, triple queue), missing windowing
P2 Medium	14	Modern C++23 adoption, error handling, magic numbers, async operations
P3 Low	10	Style, documentation, minor inefficiencies

Total actionable items: 40



RECOMMENDED IMMEDIATE ACTIONS (Top 5)

1.Fix performFFT static locals → member variables or thread_local
2.Add FFT windowing → Hann window before PFFFT call (1-line fix, massive quality improvement)
3.Pre-allocate scratch buffer → Move resize to init()
4.Add [[nodiscard]] to all Result<T> returns → Prevent silent error swallowing
5.Replace Result<T> with std::expected → C++23 standard, reduces custom code