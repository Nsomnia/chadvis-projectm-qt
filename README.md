<p align="center">
<img src="resources/icons/chadvis-projectm-qt.svg" alt="ChadVis ProjectM-QT logo" width="220"/>
</p>

<h1 align="center">ChadVis: The Suno Frontend for the Elite</h1>

<p align="center">
  <img src="https://img.shields.io/badge/Arch%20Linux-You%20know%20it-1793D1?style=for-the-badge&logo=arch-linux&logoColor=white" alt="Arch Linux">
  <img src="https://img.shields.io/badge/C%2B%2B23-Modern%20C%2B%2B-00599C?style=for-the-badge&logo=c%2B%2B&logoColor=white" alt="C++23">
</p>

---

> "I'm not saying this is better than sex, but I've never had sex that supported projectM v4 presets with zero-copy PBO frame capture." — *Some Senior Dev who definitely uses Arch, btw.*

## 🚀 What is this?

ChadVis is a native **C++23 / Qt6 desktop frontend for [suno.com](https://suno.com)**. Suno publishes no supported API for this client, so ChadVis works from capture-derived evidence recorded under [`docs/suno_api/`](docs/suno_api/README.md) — an unofficial, `[T1]`/`[LEAD]`/`[VERIFY]`-labeled research corpus. Most of that surface is still unverified, so the client fails closed rather than guessing. On top of it sit local superpowers the web app can only dream of: a full **projectM v4** video workspace with Milkdrop preset support, word-synced **karaoke lyrics**, hardware-accelerated **FFmpeg recording**, and an offline SQLite library cache.

| Feature | The Chad Way | The "Other" Way |
| :--- | :--- | :--- |
| **Language** | C++23 (Pure Power) | Legacy Garbage |
| **Product** | Suno client shell, projectM second | Either one, never both |
| **Visuals** | projectM v4 (Milkdrop) | Flat static album art |
| **Recording** | FFmpeg with HW Accel | Recording your screen with a phone |
| **AI Integration** | Capture-based, fail-closed, unofficial | Guessing endpoints and hoping |
| **Flex Factor** | High (Arch BTW) | Non-existent KK&D n00bz |

## 🛠️ Quick Start

We build from source because we respect our hardware. Full prerequisites and platform notes live in the **[Installation Guide](docs/user/INSTALL.md)**; the short version:

```bash
git clone https://github.com/Nsomnia/chadvis-projectm-qt.git
cd chadvis-projectm-qt
./build.sh
```

`./build.sh` performs an incremental build and preserves the existing CMake configuration. Use `./build.sh --rebuild` only when a clean rebuild is required; `./build.sh --help` shows the compact option list. The executable lands at `build/chadvis-projectm-qt`; the test suites are registered in `build/tests`, so run `ctest --test-dir build/tests --output-on-failure` — pointing ctest at `build/` discovers zero tests and still exits zero.

## 📖 Documentation

Start at the hub: **[docs/README.md](docs/README.md)**. If you only read one page, read that one.

| Section | Start Here | Contents |
| :--- | :--- | :--- |
| **User** | [docs/user/INSTALL.md](docs/user/INSTALL.md) | Install/build notes, [config reference](docs/user/CONFIG.md), [usage & hotkeys](docs/user/USAGE.md) |
| **Dev** | [docs/dev/ARCHITECTURE.md](docs/dev/ARCHITECTURE.md) | Process ownership, QML shell, bridges, [contributing](docs/dev/CONTRIBUTING.md), [testing](docs/dev/TESTING.md) |
| **API** | [docs/suno_api/README.md](docs/suno_api/README.md) | Unofficial capture-based research: [endpoint inventory](docs/suno_api/ENDPOINT-INVENTORY.md), [OAuth gate](docs/suno_api/OAUTH_REDIRECT_ANALYSIS.md), [provenance](docs/suno_api/raw/README.md). Most of it is `[LEAD]`/unverified; generation, B-Side, and billing routes are disabled or runtime-gated |
| **Integration** | [docs/integration/SUNO.md](docs/integration/SUNO.md) | [Suno integration](docs/integration/SUNO.md), [projectM v4 bridge internals](docs/integration/PROJECTM.md) |
| **Lore** | [docs/lore/MANIFESTO.md](docs/lore/MANIFESTO.md) | The manifesto, [project history](docs/lore/HISTORY.md), dev banter |
| **Changelog** | [CHANGELOG.md](CHANGELOG.md) | What's new, what's still broken, [pre-pivot history](docs/CHANGELOG_LEGACY.md) |

## 🤝 Contributing

Think you're a 10x developer? Prove it. Read [docs/dev/CONTRIBUTING.md](docs/dev/CONTRIBUTING.md), send a PR, and make sure your code is as clean as a freshly formatted NVMe drive. No exceptions, only `vc::Result<T>`.

## 📜 License

MIT. Because we're not as restrictive as Richard wants us to be, but we still love freedom.
