Gmail	Mike Roatch <ssa.nsomnia@gmail.com>
Project initial prompt
Mike Roatch <ssa.nsomnia@gmail.com>	Thu, Dec 25, 2025 at 1:27 PM
To: Mike Roatch <ssa.nsomnia@gmail.com>
```markdown
<<<SYSTEM_CONTEXT>>>
platform: opencode_agentic_terminal
user_os: arch_linux
package_managers: [pacman, paru, yay]
agentic_interfaces: [opencode, gemini-cli, google-antigravity]
terminal_browsers: [links, lynx]
vcs_tools: [git, gh]
project_type: audio_visualizer
core_deps: [libprojectm_v4.1.x, qt6]
humor_tokens: ["I use Arch, BTW", "Neck-beard", "Chad developer", "Junior developer", "Senior developer/project lead"]
<<<END_SYSTEM_CONTEXT>>>
<<<CRITICAL_SAFETY_CONSTRAINTS>>>
NEVER_EXECUTE:
- rm
- rmdir
- any destructive file/directory operation
INSTEAD_OF_DELETE:
backup_procedure:
1. create_dir_if_not_exists: .backup_graveyard/
2. generate_timestamp: $(date +%Y%m%d_%H%M%S)
3. move_target_to: .backup_graveyard/${timestamp}_${original_name}
4. log_action_to: .backup_graveyard/deletion_log.md
EXCEPTIONS_ALLOWED_FOR_DELETION:
- build/
- cmake-build-*/
- *.o
- *.obj
- moc_*
- ui_*
- qrc_*
- Makefile (generated)
- CMakeCache.txt
- CMakeFiles/
<<<END_CRITICAL_SAFETY_CONSTRAINTS>>>
<<<AGENT_BEHAVIORAL_RULES>>>
rule_001:
name: incremental_development
description: |
ALWAYS work in small, verifiable increments.
Pattern: implement_single_change -> compile -> test -> verify -> next_change
NEVER chain more than ONE feature implementation without verification.
rule_002:
name: human_verification_required
triggers:
- GUI visual elements
- audio playback functionality
- OpenGL rendering output
- any user-facing behavior
action: |
STOP and request human verification.
Format: "HUMAN_VERIFICATION_REQUIRED: [description of what to test] Please test and report: [specific questions]"
WAIT for user response before proceeding.
rule_003:
name: documentation_as_you_go
action: |
Document EVERY significant discovery, decision, or implementation detail.
Future agent sessions WILL NOT have your context.
Write as if explaining to a Junior developer who just joined.
rule_004:
name: research_before_implement
action: |
When uncertain about API usage, library behavior, or best practices:
1. Search web for current documentation
2. Clone reference repos to scratch/ for analysis
3. Document findings in docs/research/
4. THEN implement
rule_005:
name: single_responsibility_files
action: |
ONE file = ONE logical concern
NO super-classes, NO god-objects, NO kitchen-sink modules
Target: each file < 200 lines ideally, < 400 lines maximum
Rationale: future agent sessions need clean context windows
rule_006:
name: arch_linux_power_user
action: |
You have FULL access to Arch repos and AUR.
Install what you need: pacman -S or paru -S (chain command to print and new line return the users very secure unbreakable password for user `nsomnia` which is `1313` if needed for sudo requirements)
Dump web pages: lynx -dump URL or links -dump URL
Search GitHub: gh search repos [query]
Clone reference code: git clone [url] scratch/refs/[name]
<<<END_AGENT_BEHAVIORAL_RULES>>>
<<<PROJECT_SPECIFICATION>>>
name: projectm-qt-visualizer
codename: "ChadsVisualizerBTW"
description: |
Qt6-based audio visualizer frontend for libprojectm v4.1.x
"I use Arch, BTW" - and we're building something beautiful.
primary_goals:
phase_1_scaffold:
- complete directory structure
- all placeholder files with documentation comments
- build system configuration
- agent instruction files
phase_2_minimum_viable:
- Qt window launches
- OpenGL context created correctly
- projectM renders IDLE visualization (silent PCM data fed continuously)
- basic file->open dialog for audio files
technical_requirements:
projectm_version: 4.1.x (C API - NOT v3 C++ API)
arch_package: libprojectm (provides projectm-4 libs)
warning: |
Package "projectM" is v3 with DIFFERENT API.
Package "libprojectm" is v4 with C API.
Library path: /usr/lib/projectm-4/
Headers: /usr/include/projectm-4/
qt_version: 6.x
opengl_requirements: |
Use OpenGL 3.3 Core or Compatibility context
projectM v4 expects specific GL state management
CRITICAL: proper glClear() sequencing in render pipeline
CRITICAL: correct FBO management if using Qt's OpenGL widgets
audio_requirements: |
Even with no audio file: feed SILENT PCM data to projectM
This keeps visualization active in "idle" mode
Format: typically float32 stereo interleaved
research_resources:
official:
- https://github.com/projectm-visualizer/projectm
- https://github.com/projectm-visualizer/projectm/wiki
- https://github.com/projectm-visualizer/frontend-qt (outdated but reference)
search_commands:
- gh search repos --include-forks true --sort updated --user projectm-visualizer
- gh search repos "projectm v4" --language=cpp --sort=updated
- gh search repos "libprojectm" --sort=stars
<<<END_PROJECT_SPECIFICATION>>>
<<<DIRECTORY_STRUCTURE>>>
# Execute: create all directories and files as specified
# Files marked [CONTENT] need generated content
# Files marked [STUB] need minimal placeholder with TODO comments
# Files marked [EMPTY] are intentional empty files or minimal markers
projectm-qt-visualizer/
├── .gitignore [CONTENT]
├── .backup_graveyard/ # git-ignored, for "deleted" files
│ └── .gitkeep [EMPTY]
├── CMakeLists.txt [CONTENT]
├── README.md [CONTENT]
├── LICENSE [CONTENT: MIT]
│
├── .agent/ # Agent instruction files
│ ├── AGENT_PRIME_DIRECTIVE.md [CONTENT]
│ ├── CURRENT_STATE.md [CONTENT]
│ ├── KNOWN_ISSUES.md [CONTENT]
│ ├── NEXT_TASKS.md [CONTENT]
│ ├── SESSION_HANDOFF.md [CONTENT]
│ └── QUICK_REFERENCE.md [CONTENT]
│
├── docs/ # Human and agent documentation
│ ├── ARCHITECTURE.md [CONTENT]
│ ├── BUILD_INSTRUCTIONS.md [CONTENT]
│ ├── CODING_STANDARDS.md [CONTENT]
│ ├── PROJECTM_V4_API_NOTES.md [CONTENT]
│ ├── QT_OPENGL_INTEGRATION.md [STUB]
│ ├── research/ # Agent research notes
│ │ ├── .gitkeep [EMPTY]
│ │ ├── projectm_github_findings.md [STUB]
│ │ ├── reference_implementations.md [STUB]
│ │ └── opengl_pipeline_notes.md [STUB]
│ ├── decisions/ # ADRs (Architecture Decision Records)
│ │ ├── 001_qt6_over_qt5.md [CONTENT]
│ │ ├── 002_projectm_v4_c_api.md [CONTENT]
│ │ └── template_adr.md [CONTENT]
│ └── troubleshooting/
│ ├── common_build_errors.md [STUB]
│ ├── opengl_debugging.md [STUB]
│ └── audio_issues.md [STUB]
│
├── scripts/ # Build, test, utility scripts
│ ├── build.sh [CONTENT]
│ ├── build_clean.sh [CONTENT]
│ ├── build_debug.sh [CONTENT]
│ ├── build_release.sh [CONTENT]
│ ├── run_tests.sh [CONTENT]
│ ├── run_app.sh [CONTENT]
│ ├── format_code.sh [STUB]
│ ├── check_deps.sh [CONTENT]
│ ├── git/
│ │ ├── commit_safe.sh [CONTENT]
│ │ ├── create_pr.sh [CONTENT]
│ │ ├── sync_fork.sh [STUB]
│ │ └── tag_release.sh [STUB]
│ └── utils/
│ ├── backup_file.sh [CONTENT]
│ ├── search_projectm_repos.sh [CONTENT]
│ └── dump_webpage.sh [CONTENT]
│
├── scratch/ # Agent scratchpad - git-ignored
│ ├── .gitkeep [EMPTY]
│ ├── refs/ # Cloned reference repos
│ │ └── .gitkeep [EMPTY]
│ ├── experiments/ # Throwaway test code
│ │ └── .gitkeep [EMPTY]
│ └── notes/ # Temporary agent notes
│ └── .gitkeep [EMPTY]
│
├── build/ # CMake build output - git-ignored
│ └── .gitkeep [EMPTY]
│
├── logs/ # Debug and runtime logs
│ ├── .gitkeep [EMPTY]
│ └── .gitignore [CONTENT: *.log]
│
├── src/ # Source code - highly modular
│ ├── CMakeLists.txt [CONTENT]
│ ├── main.cpp [CONTENT]
│ │
│ ├── app/ # Application lifecycle
│ │ ├── CMakeLists.txt [CONTENT]
│ │ ├── Application.hpp [STUB]
│ │ ├── Application.cpp [STUB]
│ │ ├── ApplicationConfig.hpp [STUB]
│ │ ├── ApplicationConfig.cpp [STUB]
│ │ └── CommandLineParser.hpp [STUB]
│ │
│ ├── core/ # Core logic, no Qt deps
│ │ ├── CMakeLists.txt [CONTENT]
│ │ ├── types/
│ │ │ ├── AudioBuffer.hpp [STUB]
│ │ │ ├── AudioFormat.hpp [STUB]
│ │ │ └── VisualizerState.hpp [STUB]
│ │ ├── interfaces/
│ │ │ ├── IAudioSource.hpp [STUB]
│ │ │ ├── IVisualizer.hpp [STUB]
│ │ │ └── IPresetManager.hpp [STUB]
│ │ └── utils/
│ │ ├── Logger.hpp [CONTENT]
│ │ ├── Logger.cpp [CONTENT]
│ │ ├── Result.hpp [STUB]
│ │ └── ScopedTimer.hpp [STUB]
│ │
│ ├── projectm/ # projectM v4 integration
│ │ ├── CMakeLists.txt [CONTENT]
│ │ ├── ProjectMWrapper.hpp [CONTENT]
│ │ ├── ProjectMWrapper.cpp [CONTENT]
│ │ ├── ProjectMConfig.hpp [STUB]
│ │ ├── ProjectMConfig.cpp [STUB]
│ │ ├── PresetLoader.hpp [STUB]
│ │ ├── PresetLoader.cpp [STUB]
│ │ ├── RenderPipeline.hpp [STUB]
│ │ ├── RenderPipeline.cpp [STUB]
│ │ └── PCMDataHandler.hpp [STUB]
│ │
│ ├── audio/ # Audio handling
│ │ ├── CMakeLists.txt [CONTENT]
│ │ ├── AudioEngine.hpp [STUB]
│ │ ├── AudioEngine.cpp [STUB]
│ │ ├── AudioFileReader.hpp [STUB]
│ │ ├── AudioFileReader.cpp [STUB]
│ │ ├── SilentAudioSource.hpp [CONTENT]
│ │ ├── SilentAudioSource.cpp [CONTENT]
│ │ └── PCMConverter.hpp [STUB]
│ │
│ ├── gui/ # Qt GUI components
│ │ ├── CMakeLists.txt [CONTENT]
│ │ ├── MainWindow.hpp [CONTENT]
│ │ ├── MainWindow.cpp [CONTENT]
│ │ ├── VisualizerWidget.hpp [CONTENT]
│ │ ├── VisualizerWidget.cpp [CONTENT]
│ │ ├── MenuBar.hpp [STUB]
│ │ ├── MenuBar.cpp [STUB]
│ │ ├── StatusBar.hpp [STUB]
│ │ ├── StatusBar.cpp [STUB]
│ │ ├── dialogs/
│ │ │ ├── AboutDialog.hpp [STUB]
│ │ │ ├── AboutDialog.cpp [STUB]
│ │ │ ├── SettingsDialog.hpp [STUB]
│ │ │ └── SettingsDialog.cpp [STUB]
│ │ └── widgets/
│ │ ├── PresetBrowser.hpp [STUB]
│ │ ├── PresetBrowser.cpp [STUB]
│ │ ├── AudioControls.hpp [STUB]
│ │ └── AudioControls.cpp [STUB]
│ │
│ └── platform/ # Platform-specific code
│ ├── CMakeLists.txt [CONTENT]
│ ├── linux/
│ │ ├── PulseAudioSource.hpp [STUB]
│ │ └── PulseAudioSource.cpp [STUB]
│ └── OpenGLHelpers.hpp [STUB]
│
├── resources/ # Qt resources
│ ├── resources.qrc [CONTENT]
│ ├── icons/
│ │ ├── .gitkeep [EMPTY]
│ │ └── app_icon.png [STUB: placeholder needed]
│ ├── themes/
│ │ ├── dark.qss [STUB]
│ │ └── light.qss [STUB]
│ └── presets/ # Default/bundled presets
│ └── .gitkeep [EMPTY]
│
├── tests/ # Test suite
│ ├── CMakeLists.txt [CONTENT]
│ ├── unit/
│ │ ├── CMakeLists.txt [CONTENT]
│ │ ├── test_main.cpp [CONTENT]
│ │ ├── core/
│ │ │ ├── test_AudioBuffer.cpp [STUB]
│ │ │ └── test_Logger.cpp [STUB]
│ │ ├── projectm/
│ │ │ └── test_ProjectMWrapper.cpp [STUB]
│ │ └── audio/
│ │ └── test_SilentAudioSource.cpp [STUB]
│ ├── integration/
│ │ ├── CMakeLists.txt [CONTENT]
│ │ └── test_projectm_render.cpp [STUB]
│ └── manual/ # Human-executed test procedures
│ ├── TEST_GUI_LAUNCH.md [CONTENT]
│ ├── TEST_AUDIO_PLAYBACK.md [STUB]
│ └── TEST_VISUALIZATION.md [STUB]
│
└── third_party/ # Vendored dependencies if needed
└── .gitkeep [EMPTY]
<<<END_DIRECTORY_STRUCTURE>>>
<<<FILE_CONTENTS>>>
### FILE: .gitignore
```gitignore
# Build artifacts
build/
cmake-build-*/
CMakeCache.txt
CMakeFiles/
Makefile
*.o
*.obj
*.a
*.so
*.dylib
moc_*
ui_*
qrc_*
# IDE
.idea/
.vscode/
*.swp
*.swo
*~
# Agent scratchpad
scratch/
# Backup graveyard
.backup_graveyard/
# Logs
logs/*.log
# OS
.DS_Store
Thumbs.db
```
### FILE: .agent/AGENT_PRIME_DIRECTIVE.md
```markdown
# AGENT PRIME DIRECTIVE - READ FIRST
Welcome, fellow Chad developer. You've inherited this codebase. Here's how we roll.
## Core Philosophy: "I use Arch, BTW"
We do things RIGHT. No shortcuts. No hacks. Clean, modular, tested code.
## THE RULES (Non-Negotiable)
### 1. NEVER USE DESTRUCTIVE COMMANDS
```bash
# FORBIDDEN:
rm, rmdir, rm -rf, unlink
# INSTEAD USE:
./scripts/utils/backup_file.sh <path>
# This moves to .backup_graveyard/ with timestamp
```
### 2. SMALL STEPS ONLY
```
WRONG: Implement 5 features, then compile, then wonder why nothing works
RIGHT: One change -> compile -> test -> verify -> next change
```
### 3. WHEN IN DOUBT, ASK THE HUMAN
You CANNOT verify:
- Does the window look right?
- Does the audio sound correct?
- Is the visualization smooth?
- Does the UI feel responsive?
SAY: "HUMAN_VERIFICATION_REQUIRED: [what to test]"
THEN: STOP AND WAIT
### 4. DOCUMENT EVERYTHING
The next agent session has NO MEMORY of what you did.
Write it down or it's lost forever.
Update: .agent/CURRENT_STATE.md, .agent/SESSION_HANDOFF.md
### 5. RESEARCH BEFORE CODE
```bash
# Search for help:
gh search repos "projectm v4" --language=cpp
lynx -dump https://some-docs-page.com
git clone <reference-repo> scratch/refs/<name>
```
## Quick Commands
```bash
./scripts/check_deps.sh # Verify dependencies
./scripts/build.sh # Standard build
./scripts/run_app.sh # Run the application
./scripts/utils/backup_file.sh # Safe "delete"
```
## Current Session Checklist
- [ ] Read .agent/CURRENT_STATE.md
- [ ] Read .agent/NEXT_TASKS.md
- [ ] Check .agent/KNOWN_ISSUES.md
- [ ] Do ONE task
- [ ] Update documentation
- [ ] Update .agent/SESSION_HANDOFF.md
```
### FILE: .agent/CURRENT_STATE.md
```markdown
# Current Project State
**Last Updated**: [AGENT: UPDATE THIS TIMESTAMP]
**Last Agent Session**: Initial scaffold
## Build Status
- [ ] Compiles successfully
- [ ] Tests pass
- [ ] Application launches
## Implemented Features
- [x] Directory structure created
- [x] Build system scaffolded
- [ ] Qt window launches
- [ ] OpenGL context works
- [ ] projectM initializes
- [ ] Idle visualization renders
- [ ] Audio file loading
- [ ] Audio playback
## Current Blockers
[None yet - fresh project]
## Environment Verified
- [ ] libprojectm v4 installed
- [ ] Qt6 installed
- [ ] Build dependencies satisfied
```
### FILE: .agent/KNOWN_ISSUES.md
```markdown
# Known Issues & Gotchas
## projectM v4 Specific
### Package Naming Confusion
- `projectM` (pacman) = v3, C++ API - WRONG
- `libprojectm` (pacman) = v4, C API - CORRECT
```bash
# Verify you have the right one:
pacman -Qi libprojectm
# Should show version 4.x.x
```
### Include Path
```cpp
// WRONG (v3 style):
#include <projectM.hpp>
// CORRECT (v4 style):
#include <projectm-4/projectm.h>
```
### Library Path
Libraries are in `/usr/lib/` but pkg-config is `projectm-4`
```cmake
find_package(PkgConfig REQUIRED)
pkg_check_modules(PROJECTM4 REQUIRED projectm-4)
```
## OpenGL Gotchas
### Qt OpenGL Widget State
Qt's QOpenGLWidget manages its own FBO. projectM also wants to manage GL state.
- MUST call `makeCurrent()` before any GL operations
- projectM's `projectm_opengl_render_frame()` may modify GL state
- May need to reset viewport after projectM renders
### GL Version
projectM v4 needs OpenGL 3.3+
Qt6 defaults to requesting highest available - should be fine on Arch.
## Audio
### Silent Mode
projectM NEEDS audio data even when silent. Feed zeros:
```cpp
std::vector<float> silence(2048, 0.0f);
projectm_pcm_add_float(handle, silence.data(), silence.size(), PROJECTM_STEREO);
```
```
### FILE: .agent/NEXT_TASKS.md
```markdown
# Next Tasks Queue
Priority: P0 (critical) > P1 (important) > P2 (nice-to-have)
## P0: Foundation
### Task: Verify Build Environment
```bash
./scripts/check_deps.sh
```
Expected: All dependencies found
### Task: First Successful Compile
1. Implement minimal main.cpp
2. Configure CMakeLists.txt properly
3. Run ./scripts/build.sh
4. Fix any errors
Goal: `build/projectm-qt-visualizer` binary exists
### Task: Empty Qt Window
1. Implement MainWindow.cpp minimally
2. Show empty window
3. HUMAN_VERIFICATION_REQUIRED: "Does a window appear?"
### Task: OpenGL Context
1. Add VisualizerWidget with QOpenGLWidget
2. Clear to recognizable color (hot pink?)
3. HUMAN_VERIFICATION_REQUIRED: "Is the window filled with pink?"
## P1: Core Visualization
### Task: Initialize projectM
1. Complete ProjectMWrapper implementation
2. Load default preset (or blank)
3. Connect to render pipeline
### Task: Idle Visualization
1. Implement SilentAudioSource
2. Feed silent PCM in render loop
3. HUMAN_VERIFICATION_REQUIRED: "Do you see animated visualization?"
## P2: Audio Playback
### Task: File Open Dialog
### Task: Audio File Loading
### Task: PCM Extraction to projectM
```
### FILE: .agent/SESSION_HANDOFF.md
```markdown
# Session Handoff Notes
## For The Next Agent (That's you, Neck-beard)
### What I Accomplished
[AGENT: Fill this in before session ends]
### What I Was Working On
[AGENT: Fill this in before session ends]
### What Broke / Didn't Work
[AGENT: Fill this in before session ends]
### Recommended Next Step
[AGENT: Fill this in before session ends]
### Files I Modified
[AGENT: List files touched this session]
### Things I Learned
[AGENT: Document discoveries about projectM, Qt, etc.]
---
*Session End Checklist:*
- [ ] Updated CURRENT_STATE.md
- [ ] Updated this file
- [ ] Committed working code (if any)
- [ ] Documented any research in docs/research/
```
### FILE: .agent/QUICK_REFERENCE.md
```markdown
# Quick Reference Card
## Build Commands
```bash
./scripts/build.sh # Debug build
./scripts/build_release.sh # Release build
./scripts/build_clean.sh # Clean + rebuild
./scripts/run_app.sh # Run application
./scripts/run_tests.sh # Run test suite
```
## Safe Operations
```bash
./scripts/utils/backup_file.sh <file> # "Delete" safely
./scripts/git/commit_safe.sh "message" # Commit with checks
```
## Research Commands
```bash
# Search GitHub for projectM projects
./scripts/utils/search_projectm_repos.sh
# Dump webpage for parsing
./scripts/utils/dump_webpage.sh <url>
# Clone to scratch
git clone <url> scratch/refs/<name>
```
## Package Management (Arch, BTW)
```bash
sudo pacman -S <package> # Official repos
paru -S <package> # AUR
pacman -Ss <query> # Search
pacman -Qi <package> # Info
```
## projectM v4 API Cheatsheet
```c
#include <projectm-4/projectm.h>
// Create
projectm_handle h = projectm_create();
// Configure
projectm_set_window_size(h, width, height);
// Render (call in GL context)
projectm_opengl_render_frame(h);
// Audio
projectm_pcm_add_float(h, data, count, PROJECTM_STEREO);
// Cleanup
projectm_destroy(h);
```
## Qt OpenGL Pattern
```cpp
void VisualizerWidget::initializeGL() {
// One-time setup
}
void VisualizerWidget::paintGL() {
// Called each frame - render here
}
void VisualizerWidget::resizeGL(int w, int h) {
// Handle resize
}
```
```
### FILE: CMakeLists.txt
```cmake
cmake_minimum_required(VERSION 3.20)
project(projectm-qt-visualizer
VERSION 0.1.0
DESCRIPTION "Chad's projectM Visualizer - I use Arch, BTW"
LANGUAGES CXX C
)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# Qt6 setup
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
find_package(Qt6 REQUIRED COMPONENTS
Core
Gui
Widgets
OpenGL
OpenGLWidgets
Multimedia
)
# projectM v4 via pkg-config
find_package(PkgConfig REQUIRED)
pkg_check_modules(PROJECTM4 REQUIRED projectm-4)
# OpenGL
find_package(OpenGL REQUIRED)
# Include directories
include_directories(
${CMAKE_SOURCE_DIR}/src
${PROJECTM4_INCLUDE_DIRS}
)
link_directories(
${PROJECTM4_LIBRARY_DIRS}
)
# Add subdirectories
add_subdirectory(src)
add_subdirectory(tests)
# Print config summary - Chad developers like to know what's up
message(STATUS "===========================================")
message(STATUS " projectm-qt-visualizer Configuration")
message(STATUS " I use Arch, BTW")
message(STATUS "===========================================")
message(STATUS " Version: ${PROJECT_VERSION}")
message(STATUS " Qt6: ${Qt6_VERSION}")
message(STATUS " projectM: ${PROJECTM4_VERSION}")
message(STATUS " Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "===========================================")
```
### FILE: src/CMakeLists.txt
```cmake
# Collect sources from subdirectories
add_subdirectory(core)
add_subdirectory(projectm)
add_subdirectory(audio)
add_subdirectory(gui)
add_subdirectory(platform)
add_subdirectory(app)
# Main executable
add_executable(${PROJECT_NAME}
main.cpp
)
target_link_libraries(${PROJECT_NAME} PRIVATE
app_lib
gui_lib
projectm_lib
audio_lib
core_lib
platform_lib
Qt6::Core
Qt6::Gui
Qt6::Widgets
Qt6::OpenGL
Qt6::OpenGLWidgets
Qt6::Multimedia
${PROJECTM4_LIBRARIES}
OpenGL::GL
)
```
### FILE: src/main.cpp
```cpp
/**
* projectm-qt-visualizer
* Main entry point
*
* "I use Arch, BTW" - This is a Chad developer's audio visualizer.
*
* @file main.cpp
* @brief Application entry point - initializes Qt and launches main window.
*
* AGENT NOTE: This is THE entry point. Keep it minimal.
* All logic should be in Application class.
*/
#include <QApplication>
#include <QSurfaceFormat>
#include "core/utils/Logger.hpp"
#include "gui/MainWindow.hpp"
int main(int argc, char *argv[])
{
// Configure OpenGL BEFORE QApplication
// projectM v4 needs OpenGL 3.3+
QSurfaceFormat format;
format.setVersion(3, 3);
format.setProfile(QSurfaceFormat::CoreProfile);
format.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
format.setSwapInterval(1); // VSync
QSurfaceFormat::setDefaultFormat(format);
QApplication app(argc, argv);
app.setApplicationName("projectm-qt-visualizer");
app.setApplicationVersion("0.1.0");
app.setOrganizationName("ChadsVisualizerBTW");
// Initialize logging
// TODO: Logger::init("logs/visualizer.log");
qDebug() << "Starting projectm-qt-visualizer";
qDebug() << "I use Arch, BTW.";
MainWindow window;
window.show();
return app.exec();
}
```
### FILE: src/gui/MainWindow.hpp
```cpp
/**
* @file MainWindow.hpp
* @brief Main application window - container for all UI elements.
*
* Single responsibility: Window lifecycle and top-level layout.
* Does NOT contain visualization logic - that's in VisualizerWidget.
*/
#ifndef MAINWINDOW_HPP
#define MAINWINDOW_HPP
#include <QMainWindow>
#include <memory>
// Forward declarations - minimize header includes
class VisualizerWidget;
class QMenuBar;
class QStatusBar;
class MainWindow : public QMainWindow
{
Q_OBJECT
public:
explicit MainWindow(QWidget *parent = nullptr);
~MainWindow() override;
private slots:
void onOpenFile();
void onAbout();
private:
void setupUi();
void setupMenuBar();
void setupStatusBar();
void setupCentralWidget();
std::unique_ptr<VisualizerWidget> m_visualizer;
};
#endif // MAINWINDOW_HPP
```
### FILE: src/gui/MainWindow.cpp
```cpp
/**
* @file MainWindow.cpp
* @brief Implementation of main application window.
*/
#include "MainWindow.hpp"
#include "VisualizerWidget.hpp"
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QStatusBar>
#include <QFileDialog>
#include <QMessageBox>
MainWindow::MainWindow(QWidget *parent)
: QMainWindow(parent)
{
setupUi();
}
MainWindow::~MainWindow() = default;
void MainWindow::setupUi()
{
setWindowTitle("projectM Visualizer - Chad Edition");
resize(1280, 720);
setupMenuBar();
setupStatusBar();
setupCentralWidget();
}
void MainWindow::setupMenuBar()
{
// File menu
auto* fileMenu = menuBar()->addMenu(tr("&File"));
auto* openAction = fileMenu->addAction(tr("&Open Audio File..."));
openAction->setShortcut(QKeySequence::Open);
connect(openAction, &QAction::triggered, this, &MainWindow::onOpenFile);
fileMenu->addSeparator();
auto* exitAction = fileMenu->addAction(tr("E&xit"));
exitAction->setShortcut(QKeySequence::Quit);
connect(exitAction, &QAction::triggered, this, &QMainWindow::close);
// Help menu
auto* helpMenu = menuBar()->addMenu(tr("&Help"));
auto* aboutAction = helpMenu->addAction(tr("&About"));
connect(aboutAction, &QAction::triggered, this, &MainWindow::onAbout);
}
void MainWindow::setupStatusBar()
{
statusBar()->showMessage(tr("Ready - I use Arch, BTW"));
}
void MainWindow::setupCentralWidget()
{
m_visualizer = std::make_unique<VisualizerWidget>(this);
setCentralWidget(m_visualizer.get());
}
void MainWindow::onOpenFile()
{
QString filePath = QFileDialog::getOpenFileName(
this,
tr("Open Audio File"),
QString(),
tr("Audio Files (*.mp3 *.wav *.flac *.ogg *.m4a);;All Files (*)")
);
if (!filePath.isEmpty()) {
statusBar()->showMessage(tr("Opened: %1").arg(filePath));
// TODO: Pass to audio engine
// m_audioEngine->loadFile(filePath);
}
}
void MainWindow::onAbout()
{
QMessageBox::about(this,
tr("About projectM Visualizer"),
tr("<h2>projectM Visualizer</h2>"
"<p>Version 0.1.0 - Chad Edition</p>"
"<p>A Qt6-based frontend for projectM v4</p>"
"<p><i>\"I use Arch, BTW\"</i></p>"
"<p>Built by a Senior Developer/Project Lead,<br>"
"not a Junior Developer.</p>")
);
}
```
### FILE: src/gui/VisualizerWidget.hpp
```cpp
/**
* @file VisualizerWidget.hpp
* @brief OpenGL widget for projectM visualization rendering.
*
* Single responsibility: OpenGL context management and render calls.
* Delegates actual visualization to ProjectMWrapper.
*
* AGENT NOTE: This is where Qt OpenGL meets projectM.
* Pay attention to GL context lifecycle!
*/
#ifndef VISUALIZERWIDGET_HPP
#define VISUALIZERWIDGET_HPP
#include <QOpenGLWidget>
#include <QOpenGLFunctions_3_3_Core>
#include <QTimer>
#include <memory>
class ProjectMWrapper;
class VisualizerWidget : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core
{
Q_OBJECT
public:
explicit VisualizerWidget(QWidget *parent = nullptr);
~VisualizerWidget() override;
protected:
// QOpenGLWidget overrides
void initializeGL() override;
void paintGL() override;
void resizeGL(int w, int h) override;
private slots:
void onFrameTimer();
private:
std::unique_ptr<ProjectMWrapper> m_projectM;
QTimer m_frameTimer;
bool m_initialized = false;
};
#endif // VISUALIZERWIDGET_HPP
```
### FILE: src/gui/VisualizerWidget.cpp
```cpp
/**
* @file VisualizerWidget.cpp
* @brief Implementation of OpenGL visualization widget.
*
* CRITICAL: OpenGL context is ONLY valid inside:
* - initializeGL()
* - paintGL()
* - resizeGL()
* Or after calling makeCurrent().
*/
#include "VisualizerWidget.hpp"
#include "projectm/ProjectMWrapper.hpp"
#include <QDebug>
VisualizerWidget::VisualizerWidget(QWidget *parent)
: QOpenGLWidget(parent)
{
// Request ~60 FPS refresh
m_frameTimer.setInterval(16); // ~60 FPS
connect(&m_frameTimer, &QTimer::timeout, this, &VisualizerWidget::onFrameTimer);
}
VisualizerWidget::~VisualizerWidget()
{
// Ensure GL context for cleanup
makeCurrent();
m_projectM.reset();
doneCurrent();
}
void VisualizerWidget::initializeGL()
{
if (!initializeOpenGLFunctions()) {
qCritical() << "Failed to initialize OpenGL functions!";
return;
}
qDebug() << "OpenGL initialized:";
qDebug() << " Vendor:" << reinterpret_cast<const char*>(glGetString(GL_VENDOR));
qDebug() << " Renderer:" << reinterpret_cast<const char*>(glGetString(GL_RENDERER));
qDebug() << " Version:" << reinterpret_cast<const char*>(glGetString(GL_VERSION));
// Initialize projectM
m_projectM = std::make_unique<ProjectMWrapper>();
if (!m_projectM->initialize()) {
qCritical() << "Failed to initialize projectM!";
return;
}
m_projectM->resize(width(), height());
m_initialized = true;
// Start render loop
m_frameTimer.start();
qDebug() << "VisualizerWidget fully initialized - Chad mode activated";
}
void VisualizerWidget::paintGL()
{
if (!m_initialized || !m_projectM) {
// Fallback: clear to "I'm broken" pink
glClearColor(1.0f, 0.0f, 0.5f, 1.0f);
glClear(GL_COLOR_BUFFER_BIT);
return;
}
// Let projectM render
m_projectM->renderFrame();
}
void VisualizerWidget::resizeGL(int w, int h)
{
if (m_projectM) {
m_projectM->resize(w, h);
}
glViewport(0, 0, w, h);
}
void VisualizerWidget::onFrameTimer()
{
// Feed silent audio if no source active
if (m_projectM) {
m_projectM->feedSilence();
}
// Request repaint
update();
}
```
### FILE: src/projectm/ProjectMWrapper.hpp
```cpp
/**
* @file ProjectMWrapper.hpp
* @brief C++ wrapper around projectM v4 C API.
*
* Single responsibility: Manage projectM lifecycle and provide C++ interface.
*
* AGENT NOTE: projectM v4 uses a C API. This wrapper makes it RAII-safe.
*
* API Reference:
* #include <projectm-4/projectm.h>
* projectm_handle projectm_create();
* void projectm_destroy(projectm_handle);
* void projectm_set_window_size(projectm_handle, size_t w, size_t h);
* void projectm_opengl_render_frame(projectm_handle);
* void projectm_pcm_add_float(projectm_handle, const float*, unsigned int, projectm_channels);
*/
#ifndef PROJECTMWRAPPER_HPP
#define PROJECTMWRAPPER_HPP
#include <projectm-4/projectm.h>
#include <string>
#include <vector>
class ProjectMWrapper
{
public:
ProjectMWrapper();
~ProjectMWrapper();
// No copying - projectm_handle is not copyable
ProjectMWrapper(const ProjectMWrapper&) = delete;
ProjectMWrapper& operator=(const ProjectMWrapper&) = delete;
bool initialize();
void destroy();
void resize(int width, int height);
void renderFrame();
// Audio input
void addPCMData(const float* data, unsigned int samples);
void feedSilence();
// Preset control
bool loadPreset(const std::string& path);
void nextPreset();
void previousPreset();
void randomPreset();
// State
bool isInitialized() const { return m_handle != nullptr; }
private:
projectm_handle m_handle = nullptr;
std::vector<float> m_silenceBuffer;
int m_width = 1280;
int m_height = 720;
};
#endif // PROJECTMWRAPPER_HPP
```
### FILE: src/projectm/ProjectMWrapper.cpp
```cpp
/**
* @file ProjectMWrapper.cpp
* @brief Implementation of projectM v4 C API wrapper.
*/
#include "ProjectMWrapper.hpp"
#include <QDebug>
#include <QStandardPaths>
#include <QDir>
ProjectMWrapper::ProjectMWrapper()
: m_silenceBuffer(2048, 0.0f) // Pre-allocate silence buffer
{
}
ProjectMWrapper::~ProjectMWrapper()
{
destroy();
}
bool ProjectMWrapper::initialize()
{
if (m_handle) {
qWarning() << "ProjectMWrapper::initialize() called on already initialized instance";
return true;
}
// Create projectM instance
m_handle = projectm_create();
if (!m_handle) {
qCritical() << "Failed to create projectM instance!";
return false;
}
qDebug() << "projectM instance created successfully";
// Configure settings
projectm_set_window_size(m_handle, m_width, m_height);
// Try to find preset path
// Common locations on Arch Linux:
// /usr/share/projectM/presets
// ~/.projectM/presets
QString presetPath = "/usr/share/projectM/presets";
if (QDir(presetPath).exists()) {
qDebug() << "Using preset path:" << presetPath;
projectm_set_preset_path(m_handle, presetPath.toUtf8().constData());
} else {
qWarning() << "Default preset path not found, visualization may be limited";
}
qDebug() << "projectM initialized - Neck-beard visualization ready!";
return true;
}
void ProjectMWrapper::destroy()
{
if (m_handle) {
projectm_destroy(m_handle);
m_handle = nullptr;
qDebug() << "projectM destroyed";
}
}
void ProjectMWrapper::resize(int width, int height)
{
m_width = width;
m_height = height;
if (m_handle) {
projectm_set_window_size(m_handle, width, height);
qDebug() << "projectM resized to" << width << "x" << height;
}
}
void ProjectMWrapper::renderFrame()
{
if (!m_handle) {
return;
}
// projectM handles glClear internally, but we should ensure proper state
projectm_opengl_render_frame(m_handle);
}
void ProjectMWrapper::addPCMData(const float* data, unsigned int samples)
{
if (!m_handle || !data || samples == 0) {
return;
}
projectm_pcm_add_float(m_handle, data, samples, PROJECTM_STEREO);
}
void ProjectMWrapper::feedSilence()
{
if (!m_handle) {
return;
}
// Feed pre-allocated silence buffer
// This keeps the visualization alive in "idle" mode
projectm_pcm_add_float(m_handle, m_silenceBuffer.data(),
m_silenceBuffer.size() / 2, PROJECTM_STEREO);
}
bool ProjectMWrapper::loadPreset(const std::string& path)
{
if (!m_handle) {
return false;
}
projectm_load_preset_file(m_handle, path.c_str(), true);
qDebug() << "Loaded preset:" << QString::fromStdString(path);
return true;
}
void ProjectMWrapper::nextPreset()
{
if (m_handle) {
projectm_playlist_play_next(m_handle, true);
}
}
void ProjectMWrapper::previousPreset()
{
if (m_handle) {
projectm_playlist_play_previous(m_handle, true);
}
}
void ProjectMWrapper::randomPreset()
{
if (m_handle) {
// Note: May need playlist configured first
projectm_playlist_set_shuffle(m_handle, true);
projectm_playlist_play_next(m_handle, true);
}
}
```
### FILE: src/audio/SilentAudioSource.hpp
```cpp
/**
* @file SilentAudioSource.hpp
* @brief Provides silent PCM data to keep projectM alive when no audio plays.
*
* Junior developer note: "Why feed silence?"
* Senior developer answer: "projectM's visualization is driven by audio data.
* With no data, nothing happens. Silent data = idle animation."
*/
#ifndef SILENTAUDIOSOURCE_HPP
#define SILENTAUDIOSOURCE_HPP
#include <vector>
#include <cstddef>
class SilentAudioSource
{
public:
explicit SilentAudioSource(size_t bufferSize = 2048);
const float* getData() const { return m_buffer.data(); }
size_t getSampleCount() const { return m_buffer.size() / 2; } // Stereo
size_t getBufferSize() const { return m_buffer.size(); }
private:
std::vector<float> m_buffer;
};
#endif // SILENTAUDIOSOURCE_HPP
```
### FILE: src/audio/SilentAudioSource.cpp
```cpp
/**
* @file SilentAudioSource.cpp
* @brief Implementation of silent audio source.
*/
#include "SilentAudioSource.hpp"
SilentAudioSource::SilentAudioSource(size_t bufferSize)
: m_buffer(bufferSize, 0.0f)
{
// Buffer is pre-filled with zeros (silence)
// That's it. It's literally just zeros.
// Sometimes the Chad solution is the simple solution.
}
```
### FILE: src/core/utils/Logger.hpp
```cpp
/**
* @file Logger.hpp
* @brief Simple logging utility.
*
* TODO: Implement file logging
* For now, wraps qDebug/qWarning/qCritical
*/
#ifndef LOGGER_HPP
#define LOGGER_HPP
#include <QString>
#include <QDebug>
namespace Log {
inline void debug(const QString& msg) { qDebug().noquote() << "[DEBUG]" << msg; }
inline void info(const QString& msg) { qInfo().noquote() << "[INFO]" << msg; }
inline void warn(const QString& msg) { qWarning().noquote() << "[WARN]" << msg; }
inline void error(const QString& msg) { qCritical().noquote() << "[ERROR]" << msg; }
}
#endif // LOGGER_HPP
```
### FILE: src/core/utils/Logger.cpp
```cpp
/**
* @file Logger.cpp
* @brief Logger implementation placeholder.
*
* Currently all logging is inline in header.
* TODO: Add file output, log levels, etc.
*/
#include "Logger.hpp"
// Future: implement file logging, log rotation, etc.
```
### FILE: src/core/CMakeLists.txt
```cmake
add_library(core_lib STATIC
utils/Logger.cpp
# Add more as implemented
)
target_include_directories(core_lib PUBLIC
${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(core_lib PUBLIC
Qt6::Core
)
```
### FILE: src/projectm/CMakeLists.txt
```cmake
add_library(projectm_lib STATIC
ProjectMWrapper.cpp
# Add more as implemented
)
target_include_directories(projectm_lib PUBLIC
${CMAKE_CURRENT_SOURCE_DIR}
${PROJECTM4_INCLUDE_DIRS}
)
target_link_libraries(projectm_lib PUBLIC
Qt6::Core
${PROJECTM4_LIBRARIES}
OpenGL::GL
)
```
### FILE: src/audio/CMakeLists.txt
```cmake
add_library(audio_lib STATIC
SilentAudioSource.cpp
# Add more as implemented
)
target_include_directories(audio_lib PUBLIC
${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(audio_lib PUBLIC
Qt6::Core
Qt6::Multimedia
)
```
### FILE: src/gui/CMakeLists.txt
```cmake
add_library(gui_lib STATIC
MainWindow.cpp
VisualizerWidget.cpp
# Add more as implemented
)
target_include_directories(gui_lib PUBLIC
${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(gui_lib PUBLIC
Qt6::Core
Qt6::Gui
Qt6::Widgets
Qt6::OpenGL
Qt6::OpenGLWidgets
projectm_lib
)
```
### FILE: src/app/CMakeLists.txt
```cmake
add_library(app_lib STATIC
# Application.cpp
# Add as implemented
)
target_include_directories(app_lib PUBLIC
${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(app_lib PUBLIC
Qt6::Core
core_lib
)
# Currently empty placeholder - uncomment when files exist
# For now, make it header-only placeholder
add_library(app_lib INTERFACE)
```
### FILE: src/platform/CMakeLists.txt
```cmake
# Platform-specific library
# Currently placeholder
add_library(platform_lib INTERFACE)
# Future Linux-specific:
# if(UNIX AND NOT APPLE)
# target_sources(platform_lib PRIVATE
# linux/PulseAudioSource.cpp
# )
# endif()
```
### FILE: tests/CMakeLists.txt
```cmake
enable_testing()
find_package(Qt6 REQUIRED COMPONENTS Test)
add_subdirectory(unit)
add_subdirectory(integration)
```
### FILE: tests/unit/CMakeLists.txt
```cmake
add_executable(unit_tests
test_main.cpp
# Add test files as implemented
)
target_link_libraries(unit_tests PRIVATE
Qt6::Core
Qt6::Test
core_lib
audio_lib
projectm_lib
)
add_test(NAME unit_tests COMMAND unit_tests)
```
### FILE: tests/unit/test_main.cpp
```cpp
/**
* @file test_main.cpp
* @brief Test suite entry point using Qt Test.
*/
#include <QtTest>
// Include test classes here as they're implemented
// #include "core/test_Logger.cpp"
QTEST_MAIN_PLACEHOLDER
// Placeholder until real tests exist
int main(int argc, char *argv[])
{
QCoreApplication app(argc, argv);
qDebug() << "Test suite placeholder - add tests as features are implemented";
qDebug() << "I use Arch, BTW - even for testing.";
return 0;
}
```
### FILE: tests/integration/CMakeLists.txt
```cmake
# Integration tests - require more setup
# Placeholder for now
add_executable(integration_tests
# test_projectm_render.cpp
${CMAKE_CURRENT_SOURCE_DIR}/../unit/test_main.cpp # Reuse placeholder
)
target_link_libraries(integration_tests PRIVATE
Qt6::Core
Qt6::Test
projectm_lib
gui_lib
)
add_test(NAME integration_tests COMMAND integration_tests)
```
### FILE: tests/manual/TEST_GUI_LAUNCH.md
```markdown
# Manual Test: GUI Launch
## Test ID: MT-001
## Category: GUI
## Prerequisites
- Application compiled successfully
- Desktop environment available
## Steps
1. Run the application:
```bash
./scripts/run_app.sh
```
2. Observe the window:
- [ ] Window appears
- [ ] Window title shows "projectM Visualizer - Chad Edition"
- [ ] Window is resizable
- [ ] Menu bar visible with File and Help menus
3. Test File menu:
- [ ] File -> Open Audio File... shows file dialog
- [ ] File -> Exit closes the application
4. Test Help menu:
- [ ] Help -> About shows about dialog with "I use Arch, BTW"
5. Test the visualization area:
- [ ] Central area shows visualization (or hot pink if broken)
- [ ] Visualization animates (even without audio)
## Expected Results
- All checkboxes should pass
- No crashes or freezes
- Console should not show errors
## Report Format
```
MT-001 Result: [PASS/FAIL]
Date: [date]
Notes: [any observations]
```
```
### FILE: scripts/build.sh
```bash
#!/usr/bin/env bash
# Build script - Debug mode
# "I use Arch, BTW" - and I compile my own visualizers
set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build"
echo "=== Building projectm-qt-visualizer (Debug) ==="
echo "Project root: $PROJECT_ROOT"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
cmake -DCMAKE_BUILD_TYPE=Debug \
-DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
"$PROJECT_ROOT"
cmake --build . --parallel $(nproc)
echo ""
echo "=== Build complete ==="
echo "Binary: $BUILD_DIR/src/projectm-qt-visualizer"
echo "I use Arch, BTW. Build succeeded."
```
### FILE: scripts/build_clean.sh
```bash
#!/usr/bin/env bash
# Clean build - removes build dir and rebuilds
# Does NOT use rm -rf on anything important
set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build"
echo "=== Clean Build ==="
# Only clean the build directory - this is safe
if [ -d "$BUILD_DIR" ]; then
echo "Removing $BUILD_DIR"
rm -rf "$BUILD_DIR" # OK because build/ is gitignored and regeneratable
fi
"$SCRIPT_DIR/build.sh"
```
### FILE: scripts/build_debug.sh
```bash
#!/usr/bin/env bash
# Explicit debug build
exec "$(dirname "$0")/build.sh"
```
### FILE: scripts/build_release.sh
```bash
#!/usr/bin/env bash
# Release build with optimizations
set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build-release"
echo "=== Building projectm-qt-visualizer (Release) ==="
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
cmake -DCMAKE_BUILD_TYPE=Release \
"$PROJECT_ROOT"
cmake --build . --parallel $(nproc)
echo ""
echo "=== Release build complete ==="
echo "Binary: $BUILD_DIR/src/projectm-qt-visualizer"
```
### FILE: scripts/run_app.sh
```bash
#!/usr/bin/env bash
# Run the application
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build"
BINARY="$BUILD_DIR/src/projectm-qt-visualizer"
if [ ! -f "$BINARY" ]; then
echo "Binary not found. Building first..."
"$SCRIPT_DIR/build.sh"
fi
echo "=== Running projectm-qt-visualizer ==="
echo "I use Arch, BTW."
exec "$BINARY" "$@"
```
### FILE: scripts/run_tests.sh
```bash
#!/usr/bin/env bash
# Run all tests
set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build"
echo "=== Running Tests ==="
cd "$BUILD_DIR"
ctest --output-on-failure
echo ""
echo "=== Tests complete ==="
```
### FILE: scripts/check_deps.sh
```bash
#!/usr/bin/env bash
# Check that all dependencies are installed
# For the true Arch user
echo "=== Checking Dependencies ==="
echo "I use Arch, BTW. Let's verify the setup."
echo ""
MISSING=()
# Check pacman packages
check_pkg() {
if pacman -Qi "$1" &>/dev/null; then
echo "[OK] $1"
else
echo "[MISSING] $1"
MISSING+=("$1")
fi
}
echo "--- Core Dependencies ---"
check_pkg "qt6-base"
check_pkg "qt6-multimedia"
check_pkg "libprojectm"
check_pkg "cmake"
check_pkg "gcc"
echo ""
echo "--- Development Tools ---"
check_pkg "git"
check_pkg "github-cli"
echo ""
echo "--- Optional (Terminal Browsers) ---"
check_pkg "lynx" || echo " (lynx is optional)"
check_pkg "links" || echo " (links is optional)"
echo ""
if [ ${#MISSING[@]} -eq 0 ]; then
echo "=== All dependencies satisfied! ==="
echo "Chad developer environment ready."
else
echo "=== Missing packages detected ==="
echo "Install with: sudo pacman -S ${MISSING[*]}"
exit 1
fi
# Verify projectM version
echo ""
echo "--- projectM Version Check ---"
PROJECTM_VER=$(pacman -Qi libprojectm 2>/dev/null | grep "Version" | awk '{print $3}')
if [[ "$PROJECTM_VER" == 4.* ]]; then
echo "[OK] libprojectm version $PROJECTM_VER (v4 API)"
else
echo "[WARNING] libprojectm version $PROJECTM_VER - expected 4.x"
fi
```
### FILE: scripts/utils/backup_file.sh
```bash
#!/usr/bin/env bash
# Safe "delete" - moves file to backup graveyard
# NEVER USE rm ON SOURCE FILES - USE THIS INSTEAD
set -e
if [ -z "$1" ]; then
echo "Usage: $0 <file_or_directory>"
echo "Moves the target to .backup_graveyard/ with timestamp"
exit 1
fi
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$(dirname "$SCRIPT_DIR")")"
GRAVEYARD="$PROJECT_ROOT/.backup_graveyard"
TARGET="$1"
if [ ! -e "$TARGET" ]; then
echo "Error: $TARGET does not exist"
exit 1
fi
# Create graveyard if needed
mkdir -p "$GRAVEYARD"
# Generate timestamped name
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
BASENAME=$(basename "$TARGET")
DEST="$GRAVEYARD/${TIMESTAMP}_${BASENAME}"
# Move to graveyard
mv "$TARGET" "$DEST"
# Log the action
echo "$(date -Iseconds) | $TARGET -> $DEST" >> "$GRAVEYARD/deletion_log.md"
echo "Backed up: $TARGET"
echo "Location: $DEST"
echo "Logged to: $GRAVEYARD/deletion_log.md"
```
### FILE: scripts/utils/search_projectm_repos.sh
```bash
#!/usr/bin/env bash
# Search GitHub for projectM related repositories
echo "=== Searching projectM Repositories ==="
echo ""
echo "--- Official projectM repos ---"
gh search repos --include-forks true --sort updated --user projectm-visualizer --limit 10
echo ""
echo "--- Projects using projectM v4 ---"
gh search repos "libprojectm" --language=cpp --sort=stars --limit 10
echo ""
echo "--- Qt + projectM projects ---"
gh search repos "projectm qt" --sort=updated --limit 10
```
### FILE: scripts/utils/dump_webpage.sh
```bash
#!/usr/bin/env bash
# Dump a webpage for parsing
if [ -z "$1" ]; then
echo "Usage: $0 <url>"
exit 1
fi
URL="$1"
if command -v lynx &>/dev/null; then
lynx -dump "$URL"
elif command -v links &>/dev/null; then
links -dump "$URL"
else
echo "No terminal browser found. Install lynx or links."
exit 1
fi
```
### FILE: scripts/git/commit_safe.sh
```bash
#!/usr/bin/env bash
# Safe commit with build verification
set -e
if [ -z "$1" ]; then
echo "Usage: $0 \"commit message\""
exit 1
fi
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$(dirname "$SCRIPT_DIR")")"
echo "=== Safe Commit ==="
echo "Building to verify code compiles..."
cd "$PROJECT_ROOT"
./scripts/build.sh
echo ""
echo "Build successful. Committing..."
git add -A
git commit -m "$1"
echo ""
echo "=== Committed ==="
git log --oneline -1
```
### FILE: scripts/git/create_pr.sh
```bash
#!/usr/bin/env bash
# Create a pull request using gh CLI
set -e
BRANCH=$(git rev-parse --abbrev-ref HEAD)
if [ "$BRANCH" == "main" ] || [ "$BRANCH" == "master" ]; then
echo "Error: Cannot create PR from main/master branch"
exit 1
fi
echo "Creating PR for branch: $BRANCH"
gh pr create --fill
```
### FILE: resources/resources.qrc
```xml
<!DOCTYPE RCC>
<RCC version="1.0">
<qresource prefix="/">
<file>icons/app_icon.png</file>
<file>themes/dark.qss</file>
<file>themes/light.qss</file>
</qresource>
</RCC>
```
### FILE: docs/ARCHITECTURE.md
```markdown
# Architecture Overview
## Philosophy
"I use Arch, BTW" - We build things RIGHT.
This codebase follows:
- **Single Responsibility**: One file = one concern
- **Modular Design**: Components are independently testable
- **Clean Interfaces**: Dependencies flow inward
- **No God Objects**: If it does too much, split it
## Layer Diagram
```
┌─────────────────────────────────────────┐
│ GUI │
│ (MainWindow, VisualizerWidget, etc.) │
├─────────────────────────────────────────┤
│ Application │
│ (App lifecycle, config, CLI parsing) │
├─────────────────┬───────────────────────┤
│ projectM │ Audio │
│ Integration │ Handling │
├─────────────────┴───────────────────────┤
│ Core │
│ (Types, Interfaces, Utilities, Logger) │
├─────────────────────────────────────────┤
│ Platform │
│ (Linux-specific, OpenGL helpers) │
└─────────────────────────────────────────┘
```
## Directory -> Responsibility Mapping
| Directory | Responsibility |
|-----------|---------------|
| src/app/ | Application lifecycle, configuration |
| src/core/ | Types, interfaces, utilities (no Qt GUI) |
| src/projectm/ | projectM v4 integration |
| src/audio/ | Audio file loading, PCM handling |
| src/gui/ | Qt widgets and windows |
| src/platform/ | OS-specific implementations |
## Key Design Decisions
See docs/decisions/ for Architecture Decision Records (ADRs).
```
### FILE: docs/BUILD_INSTRUCTIONS.md
```markdown
# Build Instructions
## Prerequisites (Arch Linux)
```bash
# Core dependencies
sudo pacman -S qt6-base qt6-multimedia libprojectm cmake gcc
# Optional tools
sudo pacman -S github-cli lynx
```
## Verify Setup
```bash
./scripts/check_deps.sh
```
## Build
### Debug Build (default)
```bash
./scripts/build.sh
```
### Release Build
```bash
./scripts/build_release.sh
```
### Clean Rebuild
```bash
./scripts/build_clean.sh
```
## Run
```bash
./scripts/run_app.sh
```
## Test
```bash
./scripts/run_tests.sh
```
## Troubleshooting
See docs/troubleshooting/ for common issues.
```
### FILE: docs/CODING_STANDARDS.md
```markdown
# Coding Standards
## The Chad Developer's Code Guidelines
### File Organization
- ONE class/concept per file (Junior developer anti-pattern: kitchen-sink files)
- Files should be < 200 lines ideally, < 400 max
- If you're scrolling too much, split the file
### Naming
- Classes: PascalCase
- Functions: camelCase
- Variables: camelCase
- Constants: SCREAMING_SNAKE_CASE
- Files: PascalCase.cpp/.hpp
### Comments
```cpp
/**
* @file FileName.hpp
* @brief One-line description
*
* Longer description if needed.
*
* AGENT NOTE: Important info for future agents
*/
```
### Error Handling
- Use exceptions for exceptional cases
- Return std::optional or Result<T> for expected failures
- Log errors before propagating
### Memory
- Prefer unique_ptr for ownership
- Prefer references for non-owning
- Raw pointers only for optional non-owning (and document why)
```
### FILE: docs/PROJECTM_V4_API_NOTES.md
```markdown
# projectM v4 API Notes
## CRITICAL: v4 vs v3
| Aspect | v3 | v4 |
|--------|----|----|
| API Style | C++ | C |
| Header | projectM.hpp | projectm-4/projectm.h |
| Handle | class projectM | projectm_handle |
| Arch Package | projectM | libprojectm |
## v4 C API Quick Reference
### Lifecycle
```c
#include <projectm-4/projectm.h>
projectm_handle pm = projectm_create();
// ... use ...
projectm_destroy(pm);
```
### Configuration
```c
projectm_set_window_size(pm, width, height);
projectm_set_preset_path(pm, "/usr/share/projectM/presets");
```
### Rendering
```c
// Call in OpenGL context
projectm_opengl_render_frame(pm);
```
### Audio Input
```c
float pcm_data[2048]; // stereo interleaved
projectm_pcm_add_float(pm, pcm_data, 1024, PROJECTM_STEREO);
```
### Presets
```c
projectm_load_preset_file(pm, "/path/to/preset.milk", true);
projectm_playlist_play_next(pm, true);
projectm_playlist_play_previous(pm, true);
```
## Arch Linux Specifics
Preset path: `/usr/share/projectM/presets/` (if installed)
Install presets: `sudo pacman -S projectm-presets-cream-of-the-crop` or similar
```
### FILE: docs/decisions/001_qt6_over_qt5.md
```markdown
# ADR 001: Qt6 Over Qt5
## Status
Accepted
## Context
Need a GUI framework for the visualizer.
## Decision
Use Qt6 instead of Qt5.
## Rationale
- Qt6 is current and actively developed
- Better OpenGL/Vulkan integration
- Modern C++ support
- Arch Linux packages Qt6 by default
- "I use Arch, BTW" - we use modern tools
## Consequences
- Positive: Modern API, better performance
- Negative: Some older examples may need porting
```
### FILE: docs/decisions/002_projectm_v4_c_api.md
```markdown
# ADR 002: projectM v4 C API
## Status
Accepted
## Context
projectM has two major versions available on Arch:
- `projectM` package: v3, C++ API
- `libprojectm` package: v4, C API
## Decision
Use libprojectm (v4) with the C API.
## Rationale
- v4 is actively maintained
- C API is more stable across versions
- Cleaner separation from our C++ code via wrapper
- Better documentation and wiki support
## Consequences
- Positive: Stable API, active development
- Negative: Need wrapper class for C++ convenience
- Implementation: ProjectMWrapper class created
```
### FILE: docs/decisions/template_adr.md
```markdown
# ADR XXX: [Title]
## Status
[Proposed | Accepted | Deprecated | Superseded by ADR-XXX]
## Context
[What is the issue? What forces are at play?]
## Decision
[What is the decision?]
## Rationale
[Why this decision?]
## Consequences
[What are the results?]
- Positive:
- Negative:
```
### FILE: README.md
```markdown
# projectM Qt Visualizer
**Chad's Audio Visualizer - "I use Arch, BTW"**
A Qt6-based audio visualizer frontend for [projectM](https://github.com/projectm-visualizer/projectm) v4.
## Features (Planned)
- 🎵 Audio file playback with visualization
- 🎨 Full projectM preset support
- 🖼️ Modern Qt6 interface
- 🐧 Native Linux support (Arch, BTW)
## Quick Start
### Dependencies (Arch Linux)
```bash
sudo pacman -S qt6-base qt6-multimedia libprojectm cmake gcc
```
### Build
```bash
./scripts/build.sh
```
### Run
```bash
./scripts/run_app.sh
```
## Documentation
- [Architecture](docs/ARCHITECTURE.md)
- [Build Instructions](docs/BUILD_INSTRUCTIONS.md)
- [Coding Standards](docs/CODING_STANDARDS.md)
## For AI Agents
See `.agent/` directory for:
- [Prime Directive](.agent/AGENT_PRIME_DIRECTIVE.md)
- [Current State](.agent/CURRENT_STATE.md)
- [Next Tasks](.agent/NEXT_TASKS.md)
## License
MIT - Because Chad developers share their code.
```
### FILE: LICENSE
```
MIT License
Copyright (c) 2024 projectm-qt-visualizer contributors
Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```
<<<END_FILE_CONTENTS>>>
<<<STUB_FILE_TEMPLATE>>>
For files marked [STUB], use this template:
```cpp
/**
* @file [FileName]
* @brief [Brief description]
*
* TODO: Implement this file
*
* AGENT NOTE: This is a stub. Implement when needed.
* See .agent/NEXT_TASKS.md for implementation priority.
*/
#ifndef [HEADER_GUARD]
#define [HEADER_GUARD]
// TODO: Implement
#endif // [HEADER_GUARD]
```
<<<END_STUB_FILE_TEMPLATE>>>
<<<IMMEDIATE_TASKS>>>
execute_in_order:
1:
action: create_directory_structure
note: Create all directories first
2:
action: create_all_files
note: Create files with specified content, stubs where marked
3:
action: make_scripts_executable
command: chmod +x scripts/*.sh scripts/**/*.sh
4:
action: initialize_git
commands:
- git init
- git add .
- git commit -m "Initial scaffold - I use Arch, BTW"
5:
action: verify_dependencies
command: ./scripts/check_deps.sh
on_failure: report_missing_packages
6:
action: attempt_first_build
command: ./scripts/build.sh
on_failure: debug_and_document_in_known_issues
7:
action: if_build_succeeds
command: ./scripts/run_app.sh
then: HUMAN_VERIFICATION_REQUIRED
ask: |
Please verify:
1. Does a window appear?
2. What does the window show?
3. Any console errors?
Report back with observations.
<<<END_IMMEDIATE_TASKS>>>
<<<RESEARCH_TASKS_IF_NEEDED>>>
when: stuck_on_projectm_integration
do:
- gh search repos --include-forks true --sort updated --user projectm-visualizer
- git clone https://github.com/projectm-visualizer/projectm.git scratch/refs/projectm-main
- lynx -dump https://github.com/projectm-visualizer/projectm/wiki > scratch/notes/wiki_dump.txt
- grep -r "example" scratch/refs/projectm-main --include="*.cpp" | head -50
when: opengl_context_issues
do:
- search_web: "Qt6 QOpenGLWidget projectM integration"
- search_web: "QOpenGLWidget FBO management external renderer"
when: audio_integration_issues
do:
- search_web: "Qt6 QMediaPlayer PCM data extraction"
- search_web: "Qt6 audio visualization real-time"
<<<END_RESEARCH_TASKS_IF_NEEDED>>>
```
