# Agent Task Brief: Build System & Repository Audit (`chadvis-projectm-qt`)

## 1. Ground Rules & Repo Conventions (Verify Locally First)
Before modifying any files, read the local repository files directly—especially `AGENTS.md`, `CMakeLists.txt`, `cmake/*.cmake`, `tests/CMakeLists.txt`, `tests/integration/CMakeLists.txt`, `build.sh`, `.github/workflows/*`, `.clang-format`, `.clang-tidy`, `.qmllint.ini`, and `TODO.md`.

Key constraints already documented in the repository:
- **Indentation / Formatting (`.editorconfig`)**:
  - `CMakeLists.txt` and `*.cmake` use **4-space indentation** (`indent_size = 4`), not 2-space. Match the existing tree; do not reformat unrelated lines.
  - C++ files use 4-space indentation governed by `.clang-format`; YAML/JSON use 2-space indentation.
- **C++ / Error-Handling Policy (`README.md`)**:
  - C++23 target (`cmake/Compiler.cmake:7-8`); primary dev target is Arch Linux, with full macOS support.
  - No exceptions—error handling uses `vc::Result<T>`.
- **Scope Discipline**:
  - Confirm every finding against the actual file contents before editing.
  - Do not silently change runtime behavior or enable gated Suno API routes (`docs/suno_api/`).

---

## 2. Priority CMake & Build System Audit Targets

### A. Root `CTest` Registration (`CMakeLists.txt` vs `tests/CMakeLists.txt`)
- **Observed Issue (`README.md`)**: Test suites are only registered under `build/tests`, so running `ctest --test-dir build` discovers **0 tests and still exits 0**; users and scripts must remember `ctest --test-dir build/tests --output-on-failure`.
- **Inspect**: `CMakeLists.txt`, `tests/CMakeLists.txt`, `tests/integration/CMakeLists.txt`, and CI workflows under `.github/`.
- **Recommended Improvement**:
  - Call `include(CTest)` (which provides the standard `BUILD_TESTING` option, default `ON`, and calls `enable_testing()` at the top-level binary dir) in the root `CMakeLists.txt`.
  - Gate `add_subdirectory(tests)` on `if(BUILD_TESTING)` so standard `ctest --test-dir build --output-on-failure` works out of the box and downstream packagers can disable tests with `-DBUILD_TESTING=OFF`.
  - Update `README.md` / `docs/dev/` / CI if needed after verifying both `ctest --test-dir build` and `ctest --test-dir build/tests` (note: updating `README.md` only if the behavior changes).

### B. Offline Build Support & Unconditional CPM Fetches (`cmake/Dependencies.cmake`)
- **Observed Issue (`docs/user/INSTALL.md`)**:
  - Tier 3 dependencies (`pffft` at `cmake/Dependencies.cmake:76` and `moodycamel/readerwriterqueue` at `cmake/Dependencies.cmake:86`) call `CPMAddPackage` **unconditionally** with no system-package probe.
  - `CPM` is bootstrapped unconditionally on first configure, so **every fresh clone requires network access on every platform**, even when Tier 2 dependencies (`spdlog`, `fmt`, `toml++`) are already installed on the system.
- **Inspect**: `cmake/Dependencies.cmake`, `cmake/CPM.cmake` (or CPM download snippet), `cmake/FindProjectM4.cmake`.
- **Recommended Improvements**:
  - Check whether `pffft` and `readerwriterqueue` can first try `find_package` / `pkg-config` (or `CPM_USE_LOCAL_PACKAGES` / `FETCHCONTENT_FULLY_DISCONNECTED`) before falling back to `CPMAddPackage`, allowing distro packaging (e.g., Arch PKGBUILD / Nix / offline CI) to build offline.
  - Verify all `CPMAddPackage` calls pin immutable versions/tags/commit SHAs and `URL_HASH` / `SHA256` where applicable.
  - Audit `CPM` bootstrap behavior when all dependencies are satisfied locally so offline configures can succeed when `CPM` is cached or bypassed.

### C. Qt6 Component Discovery & CMake Minimum Version Alignment (`cmake/Dependencies.cmake`, `CMakeLists.txt`, `tests/`)
- **Observed Issue (`docs/user/INSTALL.md`)**:
  - Root `cmake/Dependencies.cmake:12-13` calls `find_package(Qt6 ...)` requiring `Core Gui Multimedia Network Quick Qml QuickControls2 Sql`.
  - Meanwhile, `tests/CMakeLists.txt:2` separately asks for `Qt6::Test`, and `tests/integration/CMakeLists.txt:18` separately asks for `Qt6::OpenGL`.
  - `CMakeLists.txt:1` states `CMake 3.20+`, while `INSTALL.md` notes a **Qt 6.8 floor** for distro packages.
- **Inspect**: `CMakeLists.txt`, `cmake/Dependencies.cmake`, `tests/CMakeLists.txt`, `tests/integration/CMakeLists.txt`.
- **Recommended Improvements**:
  - Consolidate or cleanly structure `Qt6` discovery: e.g., check `Qt6` version >= minimum supported floor (e.g., `6.8` if intended) explicitly in `find_package(Qt6 ...)` rather than relying on implicit failure, and handle `Test` / `OpenGL` consistently (such as `OPTIONAL_COMPONENTS` at root or cleanly scoped under `if(BUILD_TESTING)`).
  - Verify whether `qt_standard_project_setup()` and QML module helpers (`qt_add_qml_module`, policy `QTP0001` / `QTP0004`, etc.) require bumping `cmake_minimum_required` or setting explicit policies, and ensure `CMAKE_AUTOMOC` / `AUTORCC` / QML tooling behave cleanly without warnings on both Arch Linux and macOS.

### D. `build.sh` Ergonomics & `CMakePresets.json` (`build.sh`, `docs/user/INSTALL.md`)
- **Observed Issues (`docs/user/INSTALL.md`)**:
  - `build.sh:138` hardcodes `-G Ninja` (configure fails outright if `ninja` is absent).
  - `build.sh` does **not** forward extra `-D` CMake flags (users wanting `-DCHADVIS_AUTO_INSTALL_DEPS=ON` or custom options have to abandon `build.sh` and run raw `cmake -S . -B build ...`).
  - `build.sh:87-98` probes a hardcoded list of Qt prefixes (`/opt/homebrew/opt/qt`, `/usr/local/opt/qt`, `/usr/lib/qt6`) and passes `CHADVIS_QT_PATH` via `CMAKE_PREFIX_PATH` (`build.sh:139-141`).
- **Inspect**: `build.sh` end-to-end.
- **Recommended Improvements**:
  - Support forwarding extra CMake configure flags in `build.sh` (e.g., `-- -D...` or unrecognized `-D*` passthrough) and allow `CMAKE_GENERATOR` override (defaulting to `Ninja` if available, with a clear early error or fallback if not).
  - Check if `CMAKE_EXPORT_COMPILE_COMMANDS=ON` is enabled by default for `.clangd` / `.clang-tidy` tooling.
  - Evaluate adding a `CMakePresets.json` (`dev-debug`, `dev-release`, `ci`) so IDE integrations (CLion, VS Code, Qt Creator) and CI get reproducible configure/build/test presets without duplicating `build.sh` logic.

### E. Configure-Time Auto-Install Hygiene (`CHADVIS_AUTO_INSTALL_DEPS`)
- **Observed Behavior (`docs/user/INSTALL.md`)**:
  - `-DCHADVIS_AUTO_INSTALL_DEPS=ON` invokes host package managers (`apt`, `dnf`, `pacman`, `port`, `brew`, `vcpkg`, `winget`, `choco`) with non-interactive `sudo` during `cmake` configure, then re-runs probes.
- **Inspect**: The auto-install `.cmake` module under `cmake/` and how it interacts with `cmake/Dependencies.cmake`.
- **Recommended Improvements**:
  - Audit command construction for proper list/argument quoting (no shell-injection or whitespace issues), clear logging, and safe handling of `CHADVIS_AUTO_INSTALL_DRY_RUN=ON`.
  - Verify that re-probing clears stale `CMakeCache` `NOTFOUND` variables (`unset(... CACHE)`) before calling `find_package` / `find_path` / `find_library` again; otherwise CMake caches the initial miss and fails even after the package installs.
  - Check Windows support claims vs reality (`INSTALL.md` mentions `vcpkg`/`winget`/`choco` and `build/chadvis-projectm-qt`, while `build.sh:2` only supports Linux and macOS).

### F. `projectM v4` & `TagLib` / `FFmpeg` Detection & Target Hygiene (`cmake/Dependencies.cmake`, `cmake/FindProjectM4.cmake`)
- **Observed Details (`README.md`, `docs/user/INSTALL.md`, `docs/user/USAGE.md`)**:
  - `cmake/FindProjectM4.cmake:34` disables `pkg-config` on `APPLE` by design and falls back to CPM static build of `v4.1.6` (`:59-68`).
  - `cmake/Dependencies.cmake:112-115` probes TagLib via `pkg-config` first, then manual `find_path`/`find_library`.
  - `cmake/Dependencies.cmake:123` links `libavcodec`, `libavformat`, `libavutil`, `libswscale`, `libswresample`.
  - `USAGE.md` notes `--diagnostics` reports whether optional `libavfilter` and `libavutil-AES` features were compiled in.
- **Inspect**: `cmake/Dependencies.cmake`, `cmake/FindProjectM4.cmake`, `cmake/Install.cmake`, `src/recorder/`.
- **Recommended Improvements**:
  - Ensure all dependencies export/link modern **IMPORTED targets** (avoiding raw include-dir / library-variable pollution at directory scope).
  - Verify `libavfilter` and `libavutil-AES` feature-detection macros/targets in CMake match what `--diagnostics` reports.
  - Check `FFmpeg` hardware acceleration (`src/recorder/EncoderSettings.hpp:70-76`, `VideoRecorderFFmpeg.cpp:553-640`): determine whether an explicit opt-in CMake status summary or optional VAAPI/VideoToolbox/NVENC diagnostic check/test should be added, or whether runtime-only discovery is intentionally kept as-is.
  - Audit `cmake/Install.cmake` for `GNUInstallDirs` usage, `MACOSX_BUNDLE` completeness (e.g., `Info.plist`, icon, Qt deployment/RPATH handling), and ensuring `config/default.toml` installation/lookup works cleanly both uninstalled (from `build/`) and installed (`cmake --install`).

### G. Compiler Warnings, Static Analysis & CI Wiring (`cmake/Compiler.cmake`, `.clang-tidy`, `.qmllint.ini`, `.github/`)
- **Inspect**: `cmake/Compiler.cmake`, `cmake/Sources.cmake` (if present), `.clang-tidy`, `.qmllint.ini`, `.github/workflows/*`, `scripts/`.
- **Recommended Improvements**:
  - Check whether source lists in `CMakeLists.txt` / `cmake/` use explicit file lists vs `FILE(GLOB ... CONFIGURE_DEPENDS)`.
  - Verify warning flags in `cmake/Compiler.cmake` (e.g., `-Wall -Wextra -Wpedantic`, PCH usage if build times warrant it, LTO/IPO option for `Release`, sanitizer options `ASan`/`UBSan` for `Debug` or CI).
  - Verify `.clang-tidy` and `.qmllint.ini` are integrated into CMake targets or CI gates (not sitting unused).
  - Check `docs/user/INSTALL.md` for minor stale documentation (e.g., `qt6-svg` is still in the Arch `pacman -S` snippet even though `INSTALL.md` notes it is unused).

---

## 3. Execution Plan for the Agent
1. **Discovery (Read-Only)**:
   - Read `AGENTS.md`, `CMakeLists.txt`, all files in `cmake/`, `tests/CMakeLists.txt`, `tests/integration/CMakeLists.txt`, `build.sh`, and `.github/workflows/*`.
   - Record exact current behavior, target names, options, and any discrepancies from `docs/user/INSTALL.md` and `README.md`.
2. **Baseline Verification**:
   - Run `./build.sh` (or `cmake -S . -B build -G Ninja && cmake --build build`) and `ctest --test-dir build/tests --output-on-failure`.
   - Confirm whether `ctest --test-dir build` currently discovers 0 tests.
3. **Prioritized Implementation**:
   - **PR/Commit 1 (Low-risk CMake correctness)**: Root `CTest`/`BUILD_TESTING` wiring, cache-invalidation fix if needed in auto-install re-probes, `Qt6` test/OpenGL target consistency, and `CMAKE_EXPORT_COMPILE_COMMANDS`.
   - **PR/Commit 2 (Offline / Packager ergonomics)**: `Find` / system-package fallback improvements for `pffft` and `readerwriterqueue`, `CPM` offline handling, imported target cleanup, and install-rule (`cmake/Install.cmake`) verification.
   - **PR/Commit 3 (Developer UX & CI)**: `build.sh` flag passthrough / generator handling, optional `CMakePresets.json`, sanitizer/LTO CMake options, and syncing `docs/user/INSTALL.md` (`qt6-svg` cleanup, updated `ctest` command if root `CTest` changed).
4. **Final Verification Gate**:
   - Clean configure and build (`./build.sh --rebuild` and raw `cmake` configure).
   - Run `ctest --test-dir build --output-on-failure` (or `build/tests` as appropriate).
   - Run formatting/lint checks matching `.editorconfig`, `.clang-format`, `.clang-tidy`, and `.qmllint.ini`.