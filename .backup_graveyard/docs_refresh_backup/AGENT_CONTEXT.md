# Agent Context: chadvis-projectm-qt

## 🧠 Project Identity
**What is this?** A modern C++20/Qt6 visualizer for projectM v4 (Milkdrop).
**Target Audience:** Arch Linux users, "Chads", AI music creators.
**Key Vibe:** High performance, clean architecture, "Arch BTW" humor.

## 🏗 Core Architecture
- **Language:** C++20 (smart pointers, no raw `new`, `Result<T>` error handling).
- **Framework:** Qt6 (Widgets for UI, QWindow for rendering).
- **Build:** CMake + Ninja + CPM (dependency management).
- **Rendering:** Custom `VisualizerWindow` managing its own OpenGL context (not `QOpenGLWidget`).
- **Audio:** FFmpeg for decoding, QtMultimedia for playback (currently).
- **Threading:**
    - Main Thread: UI & Input.
    - Render Thread: `VisualizerWindow` (via `requestUpdate`).
    - Recorder Thread: `std::jthread` for FFmpeg encoding.

## 🚧 Current Focus
We are currently optimizing the repository for **agentic workflows**. This means:
1.  **Refactoring**: Splitting monolithic files (`VideoRecorder.cpp`, etc.) into smaller components.
2.  **Documentation**: Ensuring every header has clear, LLM-readable comments.
3.  **Testing**: Implementing a real test suite (currently placeholders).
4.  **Cleanup**: Removing noise from `README.md` and `.backup_graveyard/`.

## ⚠️ Critical Constraints
- **DO NOT COMPILE**: Ask the user to run `./build.sh`.
- **NO EXCEPTIONS**: Use `vc::Result<T>` for error handling.
- **QT SIGNALS**: Use modern `connect(&Sender::sig, &Receiver::slot)` syntax.
- **SMART POINTERS**: `std::unique_ptr` for ownership, `std::shared_ptr` only when necessary.

## 🗺 Navigation
- **Plan**: `.agent/IMPROVEMENT_PLAN.md`
- **Status**: `.agent/PROJECT_STATUS.md`
- **Analysis**: `.agent/CODEBASE_ANALYSIS.md`
- **Commands**: `.agent/COMMANDS.md` (To be created)
