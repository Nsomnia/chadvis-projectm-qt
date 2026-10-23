# Dependencies

Who supplies what, and why there is no fourth package manager. The build commands live in
[TESTING.md](TESTING.md#build-and-test-commands); the machine-level rules (port first, then
brew, vcpkg for C/C++) are one line in [`AGENTS.md`](../../AGENTS.md).

## The four suppliers, and what each owns

| Supplier | Owns | Why it is the owner |
| :--- | :--- | :--- |
| **macOS Qt kit** (MacPorts `/opt/local`, previously Homebrew) | Qt 6.12, OpenGL | This macOS is older than Homebrew supports well. Qt is a shared, version-matched tree — building it per-project would cost hours and would not match the plugins the app ships against. |
| **vcpkg** (Windows CI only) | TagLib, FFmpeg, GLEW, projectM's own deps | The Windows lane has no system package manager for these, and the triplet must match Qt's `/MD` runtime — see T0298 in [`TODO.md`](../../TODO.md). |
| **pkg-config** (macOS, Linux) | TagLib, FFmpeg (`libavcodec`, `libavformat`, `libavutil`, `libswscale`, `libswresample`) | Both ship `.pc` files on every distro worth targeting, and the probe in `cmake/Dependencies.cmake` already carries the keg-only/`/opt/local` fallback. |
| **CPM.cmake** | `projectM`, and `fmt`/`spdlog`/`toml++` **only when no system copy is found** | Small, header-only or four-file libraries where a system copy is a preference, not a requirement. `glm` and `Qt` are `find_package`-required and are never fetched. Every CPM dependency is pinned to the **commit its tag names**, not to the tag, so a re-cut tag cannot change a fresh build dir. |

`CMakeLists.txt` enforces C++23 at configure time; see [CONTRIBUTING.md](CONTRIBUTING.md).

## Why not Conan

Conan 2 is a good tool and this project is the kind of project it is good at — but it would be a
**third** dependency path beside two that already work, and the gaps it is usually proposed to
close are already tracked:

- **Pinning.** Nine of fifteen dependencies are unpinned (T0090) and the Windows lane has no
  `vcpkg.json` (T0091). Conan fixes both by replacing CPM and vcpkg, not by adding to them: a
  half-migrated tree is worse than an unpinned one, because two graphs then disagree about the
  same library.
- **Qt.** A Conan Qt recipe either builds from source against this old Intel macOS, or needs a
  binary package that does not exist for Qt 6.12 there. Qt stays a system kit.
- **projectM.** The pin lives in `cmake/FindProjectM4.cmake` and is a deliberate policy decision
  (T0164), not a version to be resolved. A Conan recipe would have to mirror the pin exactly to
  gain nothing.
- **Reproducibility, the real benefit,** is a CI property here. The honest cheap fix is to pin
  the CPM tags to commit SHAs and check in a manifest the release lane can read (T0090, T0091,
  T0301 — SBOM), which is hours of work rather than a cross-platform packaging migration.

**Reopen this decision only if** the tree needs a dependency no supplier above can provide on
two of the three platforms, or if CI reproducibility becomes a release blocker that pinning CPM
and a `vcpkg.json` cannot answer. Record the reason in [`TODO.md`](../../TODO.md) first; this
file is the owning document.
