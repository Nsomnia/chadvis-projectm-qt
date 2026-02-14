# AGENTS.md - chadvis-projectm-qt

This file provides essential information for agentic coding agents working on chadvis-projectm-qt, a Qt6-based projectM v4 visualizer written in C++20.

## Build, Test, and Lint Commands

### Building
- **Full build**: `./build.sh build` (uses Ninja, Release mode by default)
- **Debug build**: `./build.sh -d build`
- **Clean build**: `./build.sh -c build`
- **Run after build**: `./build.sh run`
- **Direct executable**: `./build/chadvis-projectm-qt`

### Testing
- **Run all tests**: `./build.sh test` or `ctest --test-dir build --output-on-failure`
- **Run unit tests only**: `./build/tests/unit/unit_tests`
- **Build specific test target**: `ninja -C build unit_tests`

### Dependency Check
- **Check system dependencies**: `./build.sh check-deps`
- **Auto-install deps (Arch)**: `./build.sh -y build`

### No Linting/Formatting Tools Configured
- This project has no `.clang-format` file. Follow the code style guidelines below manually.
- Build warnings are enforced via CMake: `-Wall -Wextra -Wpedantic`

## Code Style Guidelines

### Language & Standards
- **C++20** with modern features (concepts, designated initializers, etc.)
- **No exceptions**: Use `vc::Result<T>` for error handling (see `util/Result.hpp`)
- **No raw `new`/`delete`**: Use `std::unique_ptr` or `std::shared_ptr`

### Naming Conventions
- **Classes/Structs**: `PascalCase` (e.g., `AudioEngine`, `ConfigData`)
- **Functions/Methods**: `camelCase` (e.g., `init()`, `loadConfig()`)
- **Private members**: trailing underscore (e.g., `audioEngine_`, `config_`)
- **Types**: Use aliases from `Types.hpp`: `i32`, `u32`, `f32`, `usize`, `Vec2`, `Color`
- **Namespaces**: `vc` (short for "Visualizer Core")
- **Macros**: `SCREAMING_SNAKE_CASE` (e.g., `LOG_INFO`, `TRY`, `APP`)
- **Files**: `PascalCase.hpp` / `PascalCase.cpp`

### File Organization
- **Headers**: Use `#pragma once` (not include guards)
- **Includes**: System headers first, then third-party, then project headers
- **Forward declarations**: Use in headers to minimize includes
- **File size**: Keep files under 500 lines; split if larger

### Error Handling
```cpp
// Use Result<T> for all error-prone operations
Result<AudioConfig> parseAudio(const toml::table& tbl);

// Use TRY macro for early returns
#define TRY(expr) do { auto _result = (expr); if (!_result) return ...; } while (0)

// Check with if (result) or result.isOk()
if (auto result = doSomething(); result) {
    auto value = result.value();
}
```

### Logging
```cpp
// Use macros (not Logger::get() directly)
LOG_INFO("Starting audio engine");
LOG_ERROR("Failed to initialize: {}", errorMsg);
LOG_DEBUG("Buffer size: {}", bufferSize);  // Debug builds only
```

### Qt Conventions
- **Qt classes**: Use `Q_OBJECT` macro, inherit from `QWidget`/`QObject`
- **Signals/Slots**: Use modern Qt5+ syntax: `connect(obj, &Class::signal, this, &Class::slot)`
- **MOC**: CMake handles `AUTOMOC ON` automatically
- **Smart pointers**: Use `std::unique_ptr<QApplication>` over `QScopedPointer`

### Import Order Example
```cpp
#pragma once

// 1. System headers
#include <memory>
#include <string>

// 2. Third-party headers
#include <QtCore/QObject>
#include <spdlog/spdlog.h>

// 3. Project headers
#include "util/Result.hpp"
#include "util/Types.hpp"
```

## Architecture Rules

1. **Singleton Pattern**: `vc::Application` owns all engines; access via `APP` macro
2. **Engine Ownership**: All engines owned by Application; use raw pointers for non-owning access
3. **OpenGL Safety**: Always ensure context is current before `gl*` calls
4. **Threading**: Main thread = UI, separate threads for audio/recording/network
5. **Modularity**: Keep files focused; controllers bridge UI and engines

## Key Patterns

- **Result Type**: `vc::Result<T>` for error handling without exceptions
- **Type Aliases**: Use `i32`, `u32`, `f32`, `Vec2` from `util/Types.hpp`
- **Namespace Alias**: `namespace fs = std::filesystem;`
- **Configuration**: All config in `ConfigData.hpp`, parsing in `ConfigParsers.cpp`

## Testing

- **Framework**: Qt Test (`Qt6::Test`)
- **Test structure**: `tests/unit/<module>/test_<Component>.cpp`
- **Test classes**: Inherit from `QObject`, use `Q_OBJECT` macro, private slots for tests
- **Run single test**: Not directly supported; use `QTest::qExec()` with filter args

## Dependencies

- **Build**: CMake >= 3.20, Ninja, GCC/Clang with C++20 support
- **Qt6**: Core, Gui, Widgets, Multimedia, OpenGLWidgets, Svg, Network, Sql
- **Libraries**: spdlog, fmt, taglib, toml++, GLEW, GLM, FFmpeg, ProjectM-4
- **Arch install**: `sudo pacman -S cmake qt6-base qt6-multimedia qt6-svg spdlog fmt taglib tomlplusplus glew glm ffmpeg libprojectM sqlite`

## Important Notes

- **Never commit secrets**: No API keys, cookies, or credentials in source
- **CHANGELOG.md**: Update for significant changes
- **Arch BTW**: This is an Arch Linux-optimized project
- **No force push**: Never force push to main
- **Ask before compiling**: Build system is heavy; always ask user to run builds
- **Commit and push frequently**: using git or gh command.
