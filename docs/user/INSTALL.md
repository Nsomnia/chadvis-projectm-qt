# 🏗️ Installing ChadVis: The "Arch BTW" Protocol

If you're reading this, you've likely already mastered the art of the CLI. But just in case you're having a "Linus Tech Tips" moment where you almost delete your DE, follow these steps.

---

## 🛠️ Prerequisites

You need a real OS. Arch Linux is preferred, but I guess you can try with Fedora or OpenSUSE if you're feeling adventurous. Ubuntu? Good luck with those ancient PPA versions.

### The Arch Command (The One True Way)
```bash
sudo pacman -S cmake qt6-base qt6-multimedia qt6-svg spdlog fmt taglib \
    tomlplusplus glew glm ffmpeg libprojectM sqlite
```

### The "I'm on something else" List
*   **Qt6**: We use everything from `Core` to `WebEngine`.
*   **projectM v4**: Specifically v4. v3 is legacy tier.
*   **FFmpeg**: For that sweet, sweet decoding and recording.
*   **toml++**: Because JSON is a nightmare for humans to edit.
*   **spdlog**: Fast logging for fast developers.

---

## 🏗️ The Build Process

We use a custom Zsh build script because it's faster and cooler.

```bash
git clone https://github.com/Nsomnia/chadvis-projectm-qt.git
cd chadvis-projectm-qt
./build.sh build
```

**Linus (The Senior Dev):** "If you get a CMake error about `projectM-4`, it means you didn't install the v4 dev headers. Check your `/usr/include/projectM-4/` or stop complaining."

**Richard Stallman:** "Wait! Before you build, have you considered if the compiler you are using is truly free? GCC is the only path to salvation. Do not let the LLVM sirens lure you with their 'faster compile times'."

---

## 🚀 Post-Installation

The binary will be in `build/src/chadvis-projectm-qt`.

**Pro-tip:** Add it to your path or create a `.desktop` file if you want to launch it from your application menu like a regular person. But real Chads launch from the terminal to see those sweet, sweet logs.

---

> "Build times are just a coffee break for developers. Unless you're on a Threadripper, then it's just a blink." — *LTT (probably)*
