# AGENTS.md

Operating rules for this repository. The ranked backlog is [`TODO.md`](TODO.md); the docs hub
is [`docs/README.md`](docs/README.md).

## Build & test

```bash
./build.sh --fast --tests                  # build tests; the app is usually unnecessary
./build.sh --fast --tests --safe           # build + run the headless-safe entries
./build.sh --budget                        # what this box can afford; builds nothing
./build.sh --tsan --tests && ./build.sh --tsan --safe    # before merging recorder/session work
./build.sh --asan --tests && ./build.sh --asan --safe
```

Profiles `--fast` `--debug` `--release` `--tsan` `--asan` `--ubsan`, each with **its own build
directory** (`build/`, `build-fast/`, `build-tsan/`, …) — never point one profile's flags at
another's. Tuning: `-j/--jobs N`, `--ctest-jobs N`, `--nice N` / `--no-nice`, `--no-ccache`,
`--qt PATH`. Overrides: `CHADVIS_CPU_PERCENT`, `CHADVIS_MEM_PERCENT`, `CHADVIS_PER_JOB_MIB`,
`CHADVIS_CTEST_JOBS`, `CHADVIS_NICE`, `CHADVIS_CCACHE_BIN`.

* `ctest --test-dir build` finds zero tests and still exits 0. Always name the profile
  directory, and read the test count rather than the exit code.
* The binary is a `MACOSX_BUNDLE`: `build/chadvis-projectm-qt.app/Contents/MacOS/chadvis-projectm-qt`
  on macOS, `build/chadvis-projectm-qt` elsewhere.
* **A green run is not evidence for GL, burn-in, or audio.** Those skip headless and ctest
  counts a skip as success. Only a real runtime observation counts there.

### The box is shared — do not saturate it

`build.sh` spends **75% of the machine**, and the fraction is measured, not assumed:

| step | peak RSS | wall | CPU |
| --- | --- | --- | --- |
| compile, largest TU, `-O0` / `-O3` / TSan | 306 / 301 / 338 MiB | 30 / 18 / 25 s | ~17 s |
| link `unit_tests` (36 objs + Qt + projectM) | 160 MiB | 1.3 s | 1 s |
| `unit_tests`, full run | 114 MiB | **91 s** | **14 s** |

Compiles are CPU-bound; **the tests are not** — the suite is idle ~84% of its life. So
`--jobs` is **75% of logical cores** (3 on this 4-logical / 2-physical box), floored at the
physical count and capped by 75% of currently-available RAM; ctest runs at the same width.
Everything runs `nice 10` so the foreground wins the scheduler. Run `./build.sh --budget`
before assuming a build is misbehaving. Every run rewrites the gitignored `build.log` at the
repo root with its full output, so a failure is still readable after the terminal scrolls.

### Packages: port first, brew second, vcpkg for C/C++

This macOS is too old for Homebrew to support fully — use **MacPorts** (`port`, `/opt/local`)
for anything missing, and `vcpkg` (on `PATH`) for C/C++ deps neither provides. `brew` still has
older packages, so check it second, not first. `sudo port install` needs sudo: **ask first.**

Verify with `command -v` before trusting a tool — `ccache`, `sccache` and `cmake` have moved
between the two prefixes on this machine. **A build directory remembers the absolute paths of the
tools that configured it**, so migrating a package manager silently breaks every `build*` dir
with a misleading `FAILED: [code=127] … No such file or directory` that reads like a broken
install and is not one. `build.sh` detects this and reconfigures.

## CI reality

The workflows are honest about what they can and cannot prove, and the difference matters
when reading a badge:

* `build.yml` is the gate: Ubuntu × {gcc, clang} + macOS, 11 headless entries out of 14 registered.
* `windows.yml` is a **gate** as of 2026-10-20: it builds Release, runs the same 11
  headless entries, and uploads a `windeployqt` bundle with the vcpkg DLLs beside it.
  It was `continue-on-error` until it went green, and a non-blocking job rots unnoticed;
  if it starts failing, fix or revert it rather than re-arming `continue-on-error`.
* `asan.yml`, `ubsan.yml`, `tsan.yml` report and count; they do not gate. A lane that
  produced no log is a broken lane, not a clean one.
* The three runtime-environment entries (`integration_tests`, `integration_gl_tests`,
  `test_OffscreenRenderSpike`) skip headless and ctest counts a skip as success. A green
  run is not evidence for GL, burn-in, or audio.
* **A workflow file that parses is not a workflow file GitHub accepts.** GitHub rejects a
  mapping with a repeated key outright; `yaml.safe_load` keeps the last one and reports
  nothing, so every local check stays green while the workflow goes dark. The symptom is a
  run with **zero jobs** and the summary "This run likely failed because of a workflow file
  issue" — not a compile or test failure, and `gh run rerun` refuses it. Run
  `./scripts/check-workflows.py` after touching `.github/workflows/`; it catches duplicate
  keys, tab indentation, and a missing `jobs:`.

## Do not

* **Never commit credentials.** No raw captures, cookies, tokens, account or clip identifiers,
  pasted private text, or complete media URLs. User secrets go through `CredentialStore`
  (macOS/Windows/Linux keychain); the plaintext file backend stays gated.
* **Never log a bearer, cookie, `Authorization` header, or a full URL with a query string.**
  Log `url.path()`, not `url.toString()`.
* **Never send a cookie, bearer, or automatic remote image request to a host outside the
  captured allowlist** (`src/suno/CapturedHosts.cpp`). This is an unofficial suno.com client
  with no supported API: new hosts need a capture first, and a lead is never evidence.
  `docs/suno_api/` is the record — if a doc and `src/` disagree, `src/` wins.
* **Never `rm`.** Move it to `.backup_graveyard/` with a date-time suffix (gitignored).
* Anything unusually destructive gets user approval first.

## Working style

* Ship verified work. Done means the tests ran, not that it compiles. Prefer decisive commits
  over gold-plating, and keep going while tasks remain or improvements are evident.
* Commit often with a real message — git history is the memory for the next session. Update the
  docs describing a change in the same commit. One fact, one owning document.
* `TODO.md` explains its own grammar. `./scripts/task.sh` offers claim/heartbeat/release leases
  and an audit; use them when work is genuinely shared, and skip them when it isn't.
* C++23, enforced at configure time. `std::expected` (`.and_then()` / `.or_else()`) or
  `vc::Result<T>` for expected failures; `catch` only for genuinely exceptional paths.
* Add tests where they let logic be verified — threads, raw memory, FFmpeg return codes — and
  run the sanitizers before merging recorder or session work.
* Keep the "Chad / Arch, BTW" register in prose. Never a substitute for fact.

## Agent skills

`.opencode/skills/` holds agent skills, auto-discovered by OpenCode. Thirteen are the
official Qt set from [TheQtCompanyRnD/agent-skills](https://github.com/TheQtCompanyRnD/agent-skills)
(refresh from upstream rather than hand-editing vendored copies):

* Review — `qt-cpp-review`, `qt-qml-review`: deterministic lint plus deep-analysis passes
  for Qt C++ and QML. Run before merging, alongside the sanitizer profiles.
* Authoring — `qt-qml`, `qt-canvas2d`, `qt-ui-design`, `qt-cmake-project`: correct the
  systematic LLM biases around bindings, scoping, qmake-isms, and legacy `qt5_*` macros.
* Docs & tests — `qt-cpp-docs`, `qt-qml-docs`, `qt-qml-test`, `qt-qml-test-run`: generate
  reference docs and Qt Quick Test cases; `-run` builds and executes them through CMake.
* Profiling — `qt-qml-profiler`: `qmlprofiler` trace analysis for 2D QML hotspots.
* Figma — `qt-figma-token-extraction`, `qt-figma-component-generation`: design tokens to
  QML singletons, Figma components to Qt Quick Controls 2. Need Figma API access.

The rest (`code-reviewer`, `test-engineer`, `debugging-and-error-recovery`, …) are
project-authored workflow skills. When both apply, prefer the Qt skill over the generic one.

**Layout rule: every skill is a directory with a `SKILL.md` at
`.opencode/skills/<name>/SKILL.md`, and `<name>` must equal the frontmatter `name`.** A loose
`.md` at the skills root is not discovered, and a directory name with a trailing space is
not just cosmetic — it makes `git checkout` fail on Windows with `invalid path`, which kills
the entire Windows job at checkout while `continue-on-error` reports the run as green. Both
mistakes have happened here.

* Repo verification — `chadvis-verify`: the build-profile rule, the ctest zero-tests trap,
  the `SAFE_TESTS` drift check, the sanitizer lanes, and "green is not GL evidence". Run it
  before claiming any task complete.
* C++ standards — `cpp-coding-standards` (C++ Core Guidelines): the rule set behind the
  C++23 requirement, complementary to `modern-cpp`'s security emphasis.
* C++ specialist — `cpp-pro`: a general modern-C++ systems prompt; no license file, treat as
  unlicensed third-party content, do not redistribute beyond this repo.
* CI/CD — `ci-cd-and-automation`: pipeline setup and quality-gate guidance.

* C++ systems engineering — `c-cpp-profi` ([MuhDur/c-cpp-profi](https://github.com/MuhDur/c-cpp-profi)):
  evidence-grade workflow with a gate ladder (inventory → compile → static → sanitizers →
  tests → fuzz → perf → portability), scripts plus a report checker. Script paths were
  rewritten from `skill/c-cpp-profi/` to `.opencode/skills/c-cpp-profi/` when vendoring.
  No license file upstream — treat as unlicensed third-party content, do not redistribute
  beyond this repo.
* Modern C++ idioms — `modern-cpp` ([trailofbits/skills](https://github.com/trailofbits/skills),
  CC-BY-SA-4.0): C++20/23/26 idioms with a security emphasis — hardening flags, sanitizer
  setup, safe idioms by vulnerability class. The skill form of the C++23 rule above.
* Debugging discipline — `systematic-debugging` ([obra/superpowers](https://github.com/obra/superpowers),
  MIT): the root-cause-before-fix iron law. (The repo's own `debugging-and-error-recovery`
  is currently a stub.)
* Finishing discipline — `verification-before-completion` (superpowers, MIT): no completion
  claim without fresh verification evidence — the skill form of "Ship verified work".
* Design — `brainstorming` (superpowers, MIT): question-driven design refinement before
  building anything non-trivial.

`.opencode/opencode.jsonc` also registers the hosted **Qt Documentation MCP**
(`https://qt-docs-mcp.qt.io/mcp`, no auth) for live Qt 6.8 LTS / 6.11 API lookup — prefer
it over training-data recall for Qt API questions. OpenCode auto-detects `clangd` for
C/C++ LSP diagnostics when it is installed, which gives live compiler feedback mid-task.
