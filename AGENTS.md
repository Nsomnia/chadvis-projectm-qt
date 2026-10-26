# AGENTS.md

Operating rules for this repository. This file is loaded into **every** session, so it carries
rules and pointers, never the detail — the doc named on each line owns that detail. Backlog:
[`TODO.md`](TODO.md). Docs hub: [`docs/README.md`](docs/README.md).

## Build & test

```bash
./build.sh --fast --tests                  # build tests; the app is usually unnecessary
./build.sh --fast --tests --safe           # build + run the headless-safe entries
./build.sh --budget                        # what this box can afford; builds nothing
./build.sh --tsan --tests && ./build.sh --tsan --safe    # before merging recorder/session work
./build.sh --asan --tests && ./build.sh --asan --safe
```

* Profiles `--fast` `--debug` `--release` `--tsan` `--asan` `--ubsan` each keep their own build
  directory (`build-fast/`, `build-tsan/`, …). Never point one profile's flags at another's.
  Tuning flags and environment overrides: `./build.sh --help`.
* `ctest --test-dir build` finds zero tests and still exits 0. Name the profile directory and
  read the test count, not the exit code.
* The binary is a `MACOSX_BUNDLE`: `build*/chadvis-projectm-qt.app/Contents/MacOS/chadvis-projectm-qt`
  on macOS, `build*/chadvis-projectm-qt` elsewhere.
* **A green run is not evidence for GL, burn-in, or audio.** Those entries skip headless and
  ctest counts a skip as success; only a real runtime observation counts there.
* `build.sh` spends 75% of the box on purpose (`--jobs` = 75% of logical cores, `nice 10`). Run
  `./build.sh --budget` before calling a build misbehaving; every run rewrites `build.log`.

## CI reality

* Gates: `build.yml` (Ubuntu gcc + clang, macOS) and `windows.yml` (Release, the same headless
  entries, a `windeployqt` artifact). The sanitizer lanes report and count; they do not gate, and
  a lane that produced no log is broken, not clean. A failing gate gets fixed or reverted, never
  re-armed with `continue-on-error`.
* `integration_tests`, `integration_gl_tests`, `test_OffscreenRenderSpike` skip headless — see
  the green-is-not-evidence rule above.
* **A workflow file that parses is not one GitHub accepts.** A repeated YAML key is rejected
  outright while `yaml.safe_load` reports nothing; the symptom is a run with zero jobs, not a
  test failure. Run `./scripts/check-workflows.py` after touching `.github/workflows/`.

## Shipping

`./scripts/release.sh` (`--dry-run`, `--tag vX.Y.Z`) is the only correct way — it refuses a dirty
tree, a HEAD that is not `origin/main`, an empty `[Unreleased]` section, a red gate, or a second
run, then dispatches `release.yml` pinned to that commit. Full rule:
[`docs/dev/RELEASING.md`](docs/dev/RELEASING.md).

## Do not

* **Never commit credentials.** No raw captures, cookies, tokens, account or clip identifiers,
  private text, or complete media URLs in anything that leaves this machine — commit, push,
  `docs/`, `TODO.md`, `STATUS/`, commit message, release asset, issue. User secrets go through
  `CredentialStore`; the plaintext file backend stays gated.
* **A secret may be printed in the session transcript** — it is local and ephemeral. That
  permission stops at the repository and at any remote. The app's runtime log is durable and gets
  attached to issues, so it stays free of credentials: log `url.path()`, not `url.toString()`.
* **Never send a cookie, bearer, or automatic remote image request off the captured allowlist**
  (`src/suno/CapturedHosts.cpp`). This is an unofficial suno.com client: a new host needs a
  capture first, and a lead is never a host entry. `docs/suno_api/` is the record — if a doc and
  `src/` disagree, `src/` wins.
* **A route may ship on `[LEAD]` evidence only if it verifies itself at runtime and fails
  closed.** The bar is a verified answer — an authenticated unwrap, a container sniff, a status
  the server itself decides — not a captured guess. A form with neither a capture nor a runtime
  check stays refused, and what may never ship is a path whose failure mode is a wrong file.
  Capture procedure: [`docs/suno_api/capture/README.md`](docs/suno_api/capture/README.md).
* **Never `rm` source.** Move it to `.backup_graveyard/` with a date-time suffix (gitignored).
  **Generated build directories are exempt:** `build*/` are discarded, never parked — the user
  copies a binary they want to keep into `~/Documents` by hand, and a parked multi-GB tree is
  disk the next build needs. No script backs up a build.
* Anything unusually destructive gets user approval first.

## Working style

* Done means the tests ran, not that it compiles. Prefer decisive commits over gold-plating.
* Commit often with a real message — git history is the memory for the next session — and update
  the owning doc in the same commit. One fact, one owning document.
* C++23, enforced at configure time. `std::expected` (`.and_then()` / `.or_else()`) or
  `vc::Result<T>` for expected failures; `catch` only for genuinely exceptional paths.
* Test what only a test can verify — threads, raw memory, FFmpeg return codes — and run the
  sanitizers before merging recorder or session work.
* `TODO.md` explains its own grammar. `./scripts/task.sh` offers claim/heartbeat/release leases
  and an audit; use them when work is genuinely shared, skip them when it is not.
* Keep the "Chad / Arch, BTW" register in prose. Never a substitute for fact.

## Environment

* Qt API questions: use the Qt Documentation MCP before trusting training recall. OpenCode
  auto-detects `clangd`, which gives live C/C++ diagnostics mid-task.
* Packages: MacPorts (`port`, `/opt/local`) first, Homebrew second, vcpkg for C/C++ neither
  provides; `sudo port install` needs sudo — ask first. Check `command -v` before trusting a
  tool: a build directory remembers the absolute paths it configured with, so a moved
  `ccache`/`cmake` yields a misleading `[code=127]` that `build.sh` repairs by reconfiguring.
  Policy: [`docs/dev/DEPENDENCIES.md`](docs/dev/DEPENDENCIES.md).
* Suno recon: recent captures live in `~/Documents/suno-recon-active-investigation/`. When a
  schema is still unknown, ask before reaching for a proxy capture.

## Agent skills

Every skill is a directory with `SKILL.md` at `.opencode/skills/<name>/SKILL.md`, and `<name>`
must equal the frontmatter `name`. A loose `.md` at the skills root is not discovered, and a
trailing space in the directory name makes `git checkout` fail on Windows — which kills the
Windows job while `continue-on-error` still reports green. Both have happened here. Prefer a Qt
skill over a generic one when both apply. Inventory, provenance and per-harness paths:
[`docs/dev/SKILLS.md`](docs/dev/SKILLS.md).
