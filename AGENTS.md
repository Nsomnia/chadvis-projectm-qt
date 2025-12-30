# Agent Guidelines: chadvis-projectm-qt

This document provides essential information for agentic coding agents operating in this repository.

## 🛠 Build & Development

The project uses CMake (3.20+) and Ninja. A `build.sh` script is provided for common tasks.

**Note:** If you encounter errors regarding `-mno-direct-extern-access`, this is a known issue with certain system configurations and Qt. The `build.sh` and `CMakeLists.txt` contain fixes to filter this flag out.

- **Build (Debug):** `./build.sh build`
- **Build (Release):** `./build.sh release` (optimized)
- **Clean:** `./build.sh clean`
- **Run:** `./build.sh run [args]`
- **Check Dependencies:** `./build.sh check-deps`

## 🧪 Testing

The project uses `QtTest` for unit and integration testing.

- **Run all tests:** `./build.sh test`
- **Run single test suite:** `./build/tests/unit/unit_tests [TestSuite]`
- **Run specific test case:** `./build/tests/unit/unit_tests [TestSuite] [TestCase]`
- **List available tests:** `./build/tests/unit/unit_tests -functions`

## 🎨 Code Style & Conventions

### General Principles
- **Language:** C++20. Use modern features (`std::span`, `std::string_view`, designated initializers, `auto`).
- **Safety:** No exceptions. Use `vc::Result<T>` for error handling.
- **Ownership:** Prefer `std::unique_ptr` for exclusive ownership. Use `std::shared_ptr` only when truly shared.
- **Qt:** Use Qt6. Adhere to the Signal/Slot pattern for inter-component communication.

### Naming Conventions
- **Namespace:** `vc` (Visualizer Component)
- **Classes/Structs:** `PascalCase` (e.g., `AudioEngine`, `MediaMetadata`)
- **Methods/Functions:** `camelCase` (e.g., `init()`, `parseArgs()`)
- **Variables:** `camelCase` (e.g., `inputFiles`)
- **Member Variables:** `camelCase_` (trailing underscore, e.g., `state_`, `player_`)
- **Constants/Macros:** `UPPER_CASE` (e.g., `MAX_BUFFER_SIZE`, `LOG_INFO`)
- **Files:** `PascalCase.cpp/hpp` (matching class names where applicable)

### Type Aliases (Use `vc::` types from `src/util/Types.hpp`)
- Integers: `u8`, `u16`, `u32`, `u64`, `i8`, `i16`, `i32`, `i64`
- Floating Point: `f32` (float), `f64` (double)
- Size/Index: `usize` (`std::size_t`), `isize` (`std::ptrdiff_t`)
- Time: `Duration` (`std::chrono::milliseconds`), `TimePoint`

### Error Handling
Always use `vc::Result<T>` (from `src/util/Result.hpp`).
```cpp
Result<void> init() {
    if (failed) return Result<void>::err("Detailed error message");
    return Result<void>::ok();
}

// Early return pattern using macros
auto result = doSomething();
TRY(result); // Returns error if result failed
```

### Logging
Use the `LOG_*` macros defined in `src/core/Logger.hpp`.
- `LOG_TRACE(...)`, `LOG_DEBUG(...)`, `LOG_INFO(...)`, `LOG_WARN(...)`, `LOG_ERROR(...)`

### Includes Order
1. Module header (e.g., `Application.hpp` in `Application.cpp`)
2. Internal project headers (absolute paths from `src/`, e.g., `util/Result.hpp`)
3. Qt headers (e.g., `<QMediaPlayer>`)
4. Third-party headers (e.g., `<spdlog/spdlog.h>`, `<projectM-4/projectM.h>`)
5. Standard Library headers (e.g., `<memory>`, `<vector>`)

## 🏗 Architecture

- **Core:** `Application` (Singleton entry), `Config` (TOML-based singleton via `CONFIG` macro), `Logger`.
- **Audio:** `AudioEngine` manages playback via Qt Multimedia and FFmpeg. Uses `AudioAnalyzer` for spectrum data.
- **Visualizer:** `ProjectMBridge` wraps projectM v4. `VisualizerWindow` (QOpenGLWidget) handles rendering.
- **UI:** Qt-based `MainWindow` with dockable widgets (`PlaylistView`, `PresetBrowser`, etc.).
- **Overlay:** `OverlayEngine` renders dynamic text elements on top of the visualizer.
- **Recorder:** `VideoRecorder` handles FFmpeg-based encoding in a dedicated thread.
- **Signals:** Uses both Qt signals/slots and a lightweight `vc::Signal<T>` template for non-QObject classes.

## 📝 Best Practices

### Performance
- Avoid allocations in the audio/render hot paths. Use `scratchBuffer_` or pre-allocated vectors.
- Use `std::string_view` for read-only string parameters to avoid unnecessary copies.
- Prefer `std::span` for passing contiguous data (audio buffers, image data).

### Header Hygiene
- Use `#pragma once` in all headers.
- Keep headers lean; use forward declarations (e.g., `class QMediaPlayer;`) in headers and move includes to `.cpp` files.
- Never use `using namespace` in headers.

### Error Handling
- Use `[[nodiscard]]` for functions returning `Result`.
- Provide descriptive error messages in `Result::err()`.
- Use the `TRY()` macro for chaining fallible operations.

### Qt Integration
- Use `std::unique_ptr` for QObjects not managed by parent-child ownership.
- Prefer modern `connect(sender, &Sender::signal, receiver, &Receiver::slot)` syntax.
- If a class doesn't need Qt features, avoid `QObject` to keep it lightweight.

### Humor & Style
- Maintain the "Arch Linux" flavor in internal messages and logs ("I use Arch btw"), where appropriate, but heavily in any documents intended for the end user.
- Keep comments high-value and direct. No "fluff".
- If it's a "potato-safe" operation, mention it.
