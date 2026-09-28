# 🏗️ Installing ChadVis: The "Arch BTW" Protocol

If you're reading this, you've likely already mastered the art of the CLI. But just in case you're having a "Linus Tech Tips" moment where you almost delete your DE, follow these steps.

The build wrapper declares support for **macOS and Linux** (`build.sh:2`) and runs unmodified on both. Linux instructions come first because Arch is the stated primary development target (the **Code style** rule in [`AGENTS.md`](../../AGENTS.md)). macOS is not an afterthought bolted on later — it is fully supported, `CHADVIS_QT_PATH` exists specifically for it, and a Mac is where the most recent verified build of this tree lives.

---

## 🛠️ Prerequisites

The hard requirements are **CMake 3.20+** (`CMakeLists.txt:1`), **Ninja**, a **C++23** compiler (`cmake/Compiler.cmake:7-8`), and **Qt6**. Ninja is not optional: `build.sh` hardcodes `-G Ninja` when it configures (`build.sh:138`), so the configure step fails outright without it.

### The Arch Command (The One True Way)

```bash
sudo pacman -S cmake ninja qt6-base qt6-multimedia qt6-svg spdlog fmt taglib \
    tomlplusplus glm ffmpeg libprojectM
```

This is the exact list the project's own dependency checker prints when something is missing (`scripts/check_deps.sh:88`), and it agrees with `cmake/Dependencies.cmake`. Run `./scripts/check_deps.sh` before blaming the compiler — it reports what it can actually find. The checker is a *probe*, though, not the specification: it also tests packages the build does not require (`qt6-svg`, `Qt6Widgets`, `Qt6OpenGLWidgets`) and it cannot test the CPM-only tier at all. When the two disagree, `cmake/Dependencies.cmake` is the authority.

`glew` is **not** on that list and is no longer part of the build. It was removed on 2026-08-26; there are zero references left in `cmake/`, `CMakeLists.txt`, or `src/`. Installing it is harmless but pointless, and the fact that it used to be required is a historical artifact.

The macOS line further down installs the same dependencies in Homebrew's packaging: `qt` is one formula where Arch splits it into `qt6-base`/`qt6-multimedia`, there is no `libprojectM` (Homebrew cannot supply v4 — see [projectM v4 on macOS](#projectm-v4-on-macos)), and the line adds `pkg-config`, which the build only ever uses as an optional probe. Nothing else differs.

### Notes on the list

The dependencies fall into three tiers, and confusing them is what produces "it says the library is installed but configure still fails".

**Tier 1 — required from the system, no fallback.** A missing package here is a hard configure error:

*   **Qt6**: `find_package` requires `Core Gui Multimedia Network Quick Qml QuickControls2 Sql` (`cmake/Dependencies.cmake:12-13`). `qt6-base` covers all of them. The *test* lane asks for two more — `Qt6::Test` (`tests/CMakeLists.txt:2`) and `Qt6::OpenGL` (`tests/integration/CMakeLists.txt:18`) — and neither platform list names a package for them, so both are expected from the same base Qt6 install; the checker probes the same family at `scripts/check_deps.sh:35`. If configure stops on either of those two, the problem is the Qt6 install, not a missing line in the list below. The `qt6-svg` entry in the pacman line is carried over from the checker but is in no required component list; install it if something asks, ignore it otherwise.
*   **OpenGL**: `find_package(OpenGL REQUIRED)` (`cmake/Dependencies.cmake:20`) — the visualizer renderers use it directly. It is a hard requirement and no package is named for it on either platform, because it comes from the platform's OpenGL stack rather than from anything ChadVis vendors. The checker verifies it only by looking for `libGL.so` (`scripts/check_deps.sh:79`). If configure stops at `OpenGL`, that is the missing piece and neither dependency line will fix it.
*   **TagLib**: audio metadata, read by `src/audio/analysis/MediaMetadata.cpp` and `src/suno/SunoDownloader.cpp`. `pkg-config` first, then a manual `find_path`/`find_library` fallback; a total miss is a `FATAL_ERROR` (`cmake/Dependencies.cmake:112-115`).
*   **GLM**: math library, `find_package(glm REQUIRED)` (`cmake/Dependencies.cmake:120`).
*   **FFmpeg**: `libavcodec`, `libavformat`, `libavutil`, `libswscale`, `libswresample` (`cmake/Dependencies.cmake:123`), searched per-component with a `FATAL_ERROR` naming the component that was not found. That is the whole list — there is no hardware-acceleration component, because hardware encoding is resolved at runtime from whichever FFmpeg you installed, not selected at configure time.
*   **projectM v4**: specifically v4. v3 is legacy tier. Detection tries a config package, then `pkg-config projectM-4`, then falls back to a CPM source build (see [projectM v4 on macOS](#projectm-v4-on-macos)).

**Tier 2 — system first, CPM fallback.** Installing these is an optimisation, not a requirement: `find_package(... CONFIG QUIET)` succeeds and the CPM branch is skipped (`cmake/Dependencies.cmake:38`, `:51`, `:64`).

*   **spdlog / fmt / toml++**: header-first libraries. If a system package is found it is used, with a pinned CPM source build as the fallback. The first configure therefore needs network access either way, because CPM is bootstrapped regardless.

**Tier 3 — CPM only, no system-package option.** There is nothing to install; `CPMAddPackage` is called unconditionally (`cmake/Dependencies.cmake:76` for `pffft`, `:86` for `moodycamel/readerwriterqueue`). A fresh clone needs network access on every platform.

**Optional tooling:**

*   **pkg-config**: `find_package(PkgConfig QUIET)` — genuinely optional ("everything below degrades to manual search", `cmake/Dependencies.cmake:16`). That is why the Homebrew line carries it and the pacman line does not. Installing it on Linux just makes detection quieter, not more correct.

### The "I'm on something else" List

Any distribution that can provide the libraries above works — Fedora, openSUSE, Debian, and friends. The pacman line is just the one we happen to test against.

---

## 🏗️ The Build Process

We use a portable Bash build wrapper that keeps normal invocations incremental. It is the same script on both platforms.

```bash
git clone https://github.com/Nsomnia/chadvis-projectm-qt.git
cd chadvis-projectm-qt
./build.sh
```

Use `./build.sh --rebuild` when a clean rebuild is required, or `./build.sh --debug` / `./build.sh --release` to select a configuration. Run `./build.sh --help` for the compact option list. A bare `./build.sh` with no flags reuses whatever CMake settings are already cached in `build/`.

`--rebuild` archives the existing `build/` directory into `.backup_graveyard/` under a timestamped name instead of deleting it. Nothing is thrown away. We are not animals.

### 🍎 macOS / Homebrew

```bash
brew install cmake ninja qt ffmpeg spdlog fmt taglib tomlplusplus glm pkg-config
```

Then build exactly as above. The wrapper already probes the standard Homebrew prefixes in this order (`build.sh:87-98`):

1. `/opt/homebrew/opt/qt` — Apple Silicon
2. `/usr/local/opt/qt` — Intel
3. `/usr/lib/qt6` — Linux

If your Qt lives somewhere else, point the wrapper at it:

```bash
export CHADVIS_QT_PATH=/path/to/your/qt
./build.sh
```

`CHADVIS_QT_PATH` is passed straight through as CMake's `CMAKE_PREFIX_PATH` (`build.sh:139-141`). It is used for every subsequent configure, so exporting it once in your shell profile is fine.

If configure cannot find Qt, check the configure output first — it prints the Qt prefix it used, or `autodetect` if it found none.

#### projectM v4 on macOS

**Homebrew cannot give you projectM v4.** The `projectm` formula in homebrew-core is pinned to **3.1.12**, which is v3 — precisely the legacy tier this project does not target. There is no v4 formula.

On macOS, the `pkg-config` detection path is also disabled by design (`cmake/FindProjectM4.cmake:34` excludes `APPLE`). So you have two real options:

1. **Install projectM v4 yourself** so that it exports `projectM4Config.cmake` with the `libprojectM::projectM` and `libprojectM::playlist` targets, then point CMake at that prefix.
2. **Let CPM build it.** If no v4 package config is found, the build fetches and compiles projectM **v4.1.6** from source as static libraries (`cmake/FindProjectM4.cmake:59-68`). This is the default path and it just works, but it needs network access and a working toolchain.

Either way, expect the first configure to be slower. Remember the [tier 3 packages](#notes-on-the-list) — there is no system package to reach for, so a fresh clone requires network access on every platform, macOS included.

**Linus (The Senior Dev):** "If you get a CMake error about `projectM-4`, it means you didn't install the v4 dev headers. Check your `/usr/include/projectM-4/` or stop complaining."

**Richard Stallman:** "Wait! Before you build, have you considered if the compiler you are using is truly free? GCC is the only path to salvation. Do not let the LLVM sirens lure you with their 'faster compile times'."

---

## 🚀 Post-Installation

The binary lands at **`build/chadvis-projectm-qt`** — the top of the build directory, not under a `src/` subdirectory. The wrapper prints the exact path when it finishes (`build.sh:176`).

```bash
./build/chadvis-projectm-qt
```

There are no install rules you need for a local run, but they exist if you want them: `cmake --install build` installs the binary to `bin/`, the config template to `share/chadvis-projectm-qt/config/`, and on Linux a `.desktop` file and icon (`cmake/Install.cmake`). The installed config template is what a first launch on Linux probes to seed `config.toml`; see the [configuration reference](CONFIG.md#-file-location) for where that file actually lands on your platform.

**Pro-tip:** Add it to your path or create a `.desktop` file if you want to launch it from your application menu like a regular person. But real Chads launch from the terminal to see those sweet, sweet logs.

> "Build times are just a coffee break for developers. Unless you're on a Threadripper, then it's just a blink." — *LTT (probably)*
