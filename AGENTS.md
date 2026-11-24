# AGENTS.md — ChadVis (`chadvis-projectm-qt`)

C++23 / Qt 6 (floor 6.8, local 6.12) desktop audio visualizer (projectM v4) and unofficial Suno client. C++23 is enforced at configure time (`cmake/Compiler.cmake`).

## 1. Prime Directives & Token Hygiene
- **Never dump `TODO.md` in full:** Use `./scripts/task.sh next`, `rg`, or targeted line reads. Never rewrite `TODO.md` wholesale—edit only the target task block.
- **No speculative APIs:** Never invent Suno routes, JSON keys, or response fields. Uncaptured shapes stay raw (`QJsonObject`/`QVariantMap`); missing fields degrade cleanly.
- **Fail closed:** Unverified routes, missing capability gates, and uncaptured hosts stay disabled/refused.
- **Verify before `[x]`:** Every `[x]` task requires a `VERIFY` or `CHANGE` line in `STATUS/<ID>.log` and passing `./scripts/task.sh audit`.

## 2. Build, Test & Verification Commands
- **Fast incremental test loop:** `./build.sh --tests --test` (builds the `SAFE_TESTS` targets, then runs the headless-safe suites); `./build.sh --run <TestFunction>` for one QTest. Build the full app only for app-level wiring.
- **Run tests:** `ctest --test-dir build-fast/tests --output-on-failure -j"$(sysctl -n hw.ncpu)"`. `CTestTestfile.cmake` lives in `<profile>/tests`; `ctest --test-dir build` discovers zero tests and still exits 0.
- **Profiles:** `--fast` → `build-fast/`, `--debug` → `build-debug/`, `--release` → `build/`, `--tsan`/`--asan`/`--ubsan` → `build-<san>/`. Never mix one profile's flags into another's tree.
- **Safe subset:** `./build.sh --tests --safe` (canonical list in `scripts/safe-tests.txt`). `SAFE_TESTS` in `build.sh` is both the `--tests` build list and the `--safe` run list, so a new headless-safe test needs an `add_test()` entry and a `SAFE_TESTS` entry; `./scripts/check-safe-tests.py` enforces agreement.
- **QML lint:** `./scripts/lint-qml.sh` (needs a configured build; must not exceed the 1374-warning baseline). `python3 scripts/check-theme-contrast.py` needs no build.
- **Integration tests:** Excluded from `--tests`; rebuild with `./build.sh --target integration_tests` when touching `tests/integration/` (`scripts/check-integration-fresh.py` enforces freshness). `integration_gl_tests` is the only automated proof of the GL framebuffer and recording path — a skip is not a pass.
- **Sanitizers (required for recorder/threading/session changes):** `./build.sh --tsan --tests && ./build.sh --tsan --safe` (and `--asan`, `--ubsan`). Findings exit 66 (TSan) or 67 (ASan/UBSan); any other non-zero exit means the lane itself broke. `detect_leaks` is unsupported on macOS.
- **Headless limits:** Green CI is **not** proof for OpenGL rendering, FFmpeg subtitle burn-in, or live audio/HW encode. Flag those for human/real-window verification.

## 3. Architecture & Language Rules

### C++23 & Threading
- **Expected failures:** return `vc::Result<T>` or `std::expected`; reserve exceptions for genuinely exceptional paths. Log through the `Logger` wrapper (`LOG_*`), not ad-hoc console output or new `fmt` uses. Run `clang-format` with the root `.clang-format` before committing.
- **Cross-thread Qt calls:** Use parameterless `QMetaObject::invokeMethod(this, &Class::slot, Qt::QueuedConnection)` backed by atomics or mutex-guarded queues. Never pass reference-capturing lambdas across threads (`scripts/check-thread-handoffs.py`).
- **Audio callback (`AudioEngine`):** Zero allocations, zero locks, zero resizing on the realtime audio thread.
- **No `Q_ASSERT` in tests:** Release CI compiles `Q_ASSERT` out (`QT_NO_DEBUG`). Use `QVERIFY`/`QCOMPARE` or `qFatal` (`scripts/check-test-asserts.py`).
- **HTTP mocks:** Use `tests/support/FakeNetworkReply.hpp` (`fake_net::FakeReply`). Never create local reply fakes or make live DNS calls in unit tests.
- **Singletons:** Do not add new globals. `ThemeCatalog` (C++) must never be named `Theme` (shadows `Theme.qml`).
- **Config:** `config/default.toml`, snake_case keys (unknown keys are silently dropped); CLI flags override the TOML value. Credentials go only through `CredentialStore`, never TOML.

### QML & Qt 6.8–6.12 Constraints
- **Qt 6.8 floor:** Do not use Qt 6.9+ QML/C++ APIs without `#if QT_VERSION` guards.
- **`qmlcachegen` SIGSEGV trap (Qt 6.12):** Never access properties/members on a `QVariantMap`-typed `Q_PROPERTY` in QML (e.g. `bridge.mapProp.field` crashes AOT compilation). Format strings or expose typed properties in C++.
- **Register every new QML file** in `cmake/Sources.cmake`.
- **Use `App*` wrappers** (`AppButton`, `AppSlider`, `AppSwitch`, `AppComboBox`, `AppTextField`) instead of raw `QtQuick.Controls`. Note: `AppSlider.stepSize` defaults to `1` (set `stepSize: 0` explicitly for continuous controls).
- **Animations & A11y:** Scale durations by `Theme.motionScale` (respects `reduceMotion`). Every icon-only button and custom tab stop must define `Accessible.name` and keyboard focus rings.
- **No optimistic mutations:** Server state in QML flips only on confirmed `accepted` signals (except playlist reorder with explicit rollback).

## 4. Suno Client, Auth & Capture Discipline
- **API bases:** Primary `https://suno.com/a/api`; legacy alias `https://studio-api-prod.suno.com/api`. OAuth issuer stays on `studio-api-prod.suno.com/api/v2/external/oauth/*` (separate tree—never merge).
- **Credential isolation:** `suno/default` (browser cookie/JWT) and `suno/mcp` (OAuth bearer) are strictly separate. Never cross-wire them.
- **Turnstile / Captcha:** Never solve Cloudflare Turnstile in-process. Hand off to the system browser (`suno.com/create` or Listen-for-Credits submit).
- **Legal / Money / Staff routes:** Never automate TOS acceptance (`POST`), billing/purchases, `/b-side/*`, or `/labs/*`.
- **Log scrubbing:** Never log bearer tokens, cookies, raw Clerk `long_message` strings, clip IDs, or local user paths (`DownloadQueue` / `SunoDownloader` must stay ID-free). Log `url.path()`, not `url.toString()`.
- **HAR sanitization:** Never commit raw `.har` files or plaintext tokens. Strip values/cookies/IDs at ingest; promote verified routes to `docs/suno_api/ENDPOINT-INVENTORY.md` as `[T1]`.

## 5. Git, Docs & Multi-Agent Coordination
- **Claims:** Claim tasks via `./scripts/task.sh claim <ID>` (`STATUS/<ID>.claim`) before editing overlapping write-sets.
- **Keep `TODO.md` lean:** Move `[x]` and `[-]` tasks to `docs/dev/TODO-archive.md` and user-facing changes to `CHANGELOG.md` (`[Unreleased]`) when closing a task.
- **Release:** `./scripts/release.sh` is the only path; never dispatch `release.yml` by hand. No flags creates a pre-release `v<version.txt>-build.<sha>`; `--tag vX.Y.Z` is a real release and needs a clean tree, HEAD on `origin/main`, and a non-empty `[Unreleased]`.
- **CI:** `guards.yml`, `build.yml`, `windows.yml`, `asan.yml`, `tsan.yml`, `release.yml`, `dependabot-automerge.yml`. Run `./scripts/check-workflows.py` after editing workflows; never hide a failing gate with `continue-on-error`.
- **Skills:** `.opencode/skills/<name>/SKILL.md` is canonical; `.claude/skills` and `.workbuddy-ai/skills` are symlinks to it, so edit only the canonical copy; Kilo reads `.kilo/rules/*.md`.
- **Qt/deps:** `build.sh` resolves Qt as `--qt` → `$CHADVIS_QT_PATH` → newest `$HOME/Qt/*/macos` → MacPorts → Homebrew/distro → `.qt-stitch` last. vcpkg is the Windows CI lane only; there is no `vcpkg.json`.
