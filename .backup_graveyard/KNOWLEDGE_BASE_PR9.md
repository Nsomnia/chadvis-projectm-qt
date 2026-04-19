# 🧠 Agent Knowledge Base: ChadVis Technical Context

Welcome, fellow LLM. This file is your shortcut to understanding the codebase without having to parse the "Chad" humor in the main docs.

## 🏗️ Core Architecture
- **Language**: C++20. Use `std::jthread`, `std::unique_ptr`, `std::span`.
- **UI**: Qt6 Widgets. Note: `VisualizerWindow` is a `QWindow`, not a `QWidget`.
- **Patterns**: Singleton-Engine-Controller.
- **Macros**: `APP` for `vc::Application::instance()`, `CONFIG` for `vc::Config::instance()`.
- **Error Handling**: `vc::Result<T>`. Do not use exceptions.

## 📁 Key Directories
- `src/core/`: Application lifecycle, config, logging.
- `src/audio/`: Playback, analysis (KissFFT), playlists.
- `src/visualizer/`: projectM v4 bridge, OpenGL window.
- `src/recorder/`: FFmpeg encoding, PBO capture.
- `src/suno/`: API client, SQLite database.
- `src/overlay/`: Text rendering on top of OpenGL.

## ⚙️ Critical Constraints
- **OpenGL Context**: Must be current before `gl*` calls.
- **Threading**: Encode and Decode are in separate threads. UI/Render is on Main.
- **Smart Pointers**: No raw `new`. No `delete`. Use `make_unique`.
- **Signals**: Use modern Qt `connect` syntax.

## 🛠️ Build & Test
- Build: `./build.sh build` (Zsh required).
- Build Artifacts: `build/`.
- Tests: `tests/` (unit, integration, manual).

## 📝 Recent Changes (Apr 2024)
- Documentation refactored to modular structure in `docs/`.
- Root `README.md` and `CHANGELOG.md` updated.
- Existing docs moved to `.backup_graveyard/docs_refresh_backup/`.

## 🤖 Scratchpad
- Check `TODO.md` for active tasks.
- Maintain the persona in human-facing docs, but keep it clinical here.
