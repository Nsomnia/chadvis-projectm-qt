# Agent Quickstart: chadvis-projectm-qt

Welcome, Chad Agent. This guide will get you up to speed in < 500 tokens.

## 🎯 The Mission
Maintain and evolve a high-performance projectM v4 visualizer built with C++20 and Qt6.

## 🏗 Architecture at a Glance
- **Core**: `vc::Application` (Singleton) owns all engines.
- **Audio**: `AudioEngine` feeds PCM data to projectM.
- **Visualizer**: `VisualizerWindow` (QWindow) handles OpenGL rendering.
- **Recorder**: `VideoRecorder` (Modular) handles FFmpeg encoding in a separate thread.
- **UI**: Qt Widgets + Controllers (`src/ui/controllers`).

## 🛠 Essential Commands
- **Build**: `./build.sh build` (Ask user to run this!)
- **Test**: `./build.sh test`
- **Debug**: `./build/chadvis-projectm-qt --debug`

## ⚠️ The "Chad" Rules
1. **No Exceptions**: Use `vc::Result<T>`.
2. **No Raw New**: Use `std::unique_ptr` or `std::shared_ptr`.
3. **No Direct Compile**: The build system is heavy; always ask the user to compile.
4. **Context is King**: Ensure OpenGL context is current before `gl*` calls.
5. **Stay Modular**: Keep files small. If it's >500 lines, split it.

## 🗺 Where to find things
- **Logic**: `src/`
- **Tasks**: `.agent/IMPROVEMENT_PLAN.md`
- **Status**: `.agent/PROJECT_STATUS.md`
- **Reference**: `docs/ARCHITECTURE.md`

Now go forth and ship some 1337 code. Arch BTW.
