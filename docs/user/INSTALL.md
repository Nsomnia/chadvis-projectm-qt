# 🏗️ Installing ChadVis: The "Arch BTW" Protocol

If you're reading this, you've likely already mastered the art of the CLI. But just in case you're having a "Linus Tech Tips" moment where you almost delete your DE, follow these steps.

The build wrapper declares support for **macOS and Linux** (`build.sh:2`) and runs unmodified on both. Linux instructions come first because Arch is the stated primary development target (`AGENTS.md:342`). macOS is not an afterthought bolted on later — it is fully supported, `CHADVIS_QT_PATH` exists specifically for it, and a Mac is where the most recent verified build of this tree lives.

---

## 🛠️ Prerequisites

The hard requirements are **CMake 3.20+** (`CMakeLists.txt:1`), **Ninja**, a **C++23** compiler (`cmake/Compiler.cmake:7-8`), and **Qt6**. Ninja is not optional: `build.sh` hardcodes `-G Ninja` when it configures (`build.sh:138`), so the configure step fails outright without it.

### The Arch Command (The One True Way)

```bash
sudo pacman -S cmake ninja qt6-base qt6-multimedia qt6-svg spdlog fmt taglib \
    tomlplusplus glm ffmpeg libprojectM
```

This is the exact list the project's own dependency checker prints when something is missing (`scripts/check_deps.sh:88`). Run `./scripts/check_deps.sh` before blaming the compiler — it reports what it can actually find.

`glew` is **not** on that list and is no longer part of the build. It was removed on 2026-08-26; there are zero references left in `cmake/`, `CMakeLists.txt`, or `src/`. Installing it is harmless but pointless, and the fact that it used to be required is a historical artifact.

Notes on the list:

*   **Qt6**: `find_package` requires `Core Gui Multimedia Network Quick Qml QuickControls2 Sql` (`cmake/Dependencies.cmake:12-13`). `qt6-base` covers all of them. The `qt6-svg` entry is carried over from the checker but is not in the required component list — install it if something asks, ignore it otherwise.
*   **projectM v4**: specifically v4. v3 is legacy tier. Detection tries a config package, then `pkg-config projectM-4`, then falls back to a CPM source build.
*   **FFmpeg**: `libavcodec`, `libavformat`, `libavutil`, `libswscale`, `libswresample`.
*   **spdlog / fmt / toml++**: header-first libraries. If a system package is found it is used; otherwise CPM fetches and builds a pinned version at configure time, which means the first configure needs network access.
*   **TagLib**: audio metadata. Found via `pkg-config`, with a manual `find_path`/`find_library` fallback.
*   **GLM**: math library, required.

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

Either way, expect the first configure to be slower. `pffft` and `readerwriterqueue` are always built through CPM with no system-package option, so a fresh clone requires network access on every platform, macOS included.

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
