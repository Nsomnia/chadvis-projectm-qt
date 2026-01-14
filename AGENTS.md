# Agent Guidelines: chadvis-projectm-qt

This document provides essential information for agentic coding agents operating in this repository.

## 🚀 Agent Quickstart

1.  **Read the Plan**: Check `.agent/IMPROVEMENT_PLAN.md` for the current roadmap.
2.  **Check Status**: See `.agent/PROJECT_STATUS.md` for known issues and recent fixes.
3.  **Analyze Context**: Read `.agent/CODEBASE_ANALYSIS.md` to understand the structure.
4.  **Build**: Use `./build.sh build` to compile.
5.  **Test**: Use `./build.sh test` (once tests are implemented).

## 📂 File Structure Overview

- `src/` - Source code (C++20)
    - `audio/` - Audio engine & analysis
    - `core/` - App singleton, config, logging
    - `overlay/` - OpenGL text overlay engine
    - `recorder/` - FFmpeg video recorder
    - `suno/` - Suno AI client
    - `ui/` - Qt widgets & controllers
    - `visualizer/` - ProjectM bridge & window
- `docs/` - Documentation
- `tests/` - Unit & integration tests
- `.agent/` - Agent context & plans

## 🛠 Build & Development

The project uses CMake (3.20+) and Ninja. A `build.sh` script is provided for common tasks.

**CRITICAL: DO NOT COMPILE THE CODE YOURSELF.**
- **AVailable Compile Options:** Due to a starnge opencode and C++ 20 bug it hangs the users system. You may instead either chain manual compilation commands for all changed source code files, or simply inform the user to compile in which case they will either respond with any errors or place them in a file within ``.agent`` for the Agent to analyze.

**Note:** If you encounter errors regarding `-mno-direct-extern-access`, this is a known issue with Qt and can be ignored.

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
Use `vc::Result<T>` (from `src/util/Result.hpp`).
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
- **Visualizer:** `ProjectMBridge` wraps projectM v4. `VisualizerWindow` (QWindow) handles rendering via direct OpenGL context management.
- **UI:** Qt-based `MainWindow` with dockable widgets (`PlaylistView`, `PresetBrowser`, etc.).
- **Overlay:** `OverlayEngine` renders dynamic text elements on top of the visualizer.
- **Recorder:** `VideoRecorder` handles FFmpeg-based encoding in a dedicated `std::jthread`.
- **Signals:** Uses both Qt signals/slots and a lightweight `vc::Signal<T>` template for non-QObject classes.

## 📝 Best Practices

### Performance
- Avoid allocations in the audio/render hot paths. Use `scratchBuffer_` or pre-allocated vectors.
- Use `std::string_view` for read-only string parameters to avoid unnecessary copies.
- Prefer `std::span` for passing contiguous data (audio buffers, image data).
- **Rendering:** `VisualizerWindow` manages its own OpenGL context and resources. Use `makeCurrent` before any GL operations.

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
- **Thread Safety:** Ensure UI updates happen on the main thread (use `QTimer` or signals).

### Humor & Style
- Maintain the "Arch Linux", "Chad developer", "Junior vs Senior devops", and any other relevent humor where appropriate. Heavily in the documentation ("I use Arch btw"), where intended for the end-user.
- Keep comments high-value and direct. No "fluff". The codebase only has to be commented for agent intake as there is not likely to be any human development work direclty in the c++ code.

### Agent friendly file reccomendations
- While not forced, it is generally more token usage efficient and easier for agentic model workflow processes to have smaller files rather than monoliths. Whenever appropriate make new files for new items such as classes, headers, or extended documentation. Follow best industry standards and project aware best practices. If a file becomes large, and especially if this causes edit failures for the agent, then refactor the project structure such that these monolothiic/large files are split up. 
- Make file names very clear as to their purpose. File names do not use very many tokens, and makes expoloring the codebase easier.

### Agentic Compilation Linitations
- Due to a bug in opencode, compiling large numbers of files agentically hangs the users system fatally. For small changes feel free to compile else stop output and inform the user to copile and report pack. The last compile log should be placed in the `.agent/` directory.
