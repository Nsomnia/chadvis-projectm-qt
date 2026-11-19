# AGENTS.md — ChadVis (`chadvis-projectm-qt`)

C++20 / Qt 6 (floor 6.8, local 6.12) desktop audio visualizer (projectM v4) and unofficial Suno client.

## 1. Prime Directives & Token Hygiene
- **Never dump `TODO.md` in full:** Use `./scripts/task.sh next`, `rg`, or targeted line reads. Never rewrite `TODO.md` wholesale—edit only the target task block.
- **No speculative APIs:** Never invent Suno routes, JSON keys, or response fields. Uncaptured shapes stay raw (`QJsonObject`/`QVariantMap`); missing fields degrade cleanly.
- **Fail closed:** Unverified routes, missing capability gates, and uncaptured hosts stay disabled/refused.
- **Verify before `[x]`:** Every `[x]` task requires a `VERIFY` or `CHANGE` line in `STATUS/<ID>.log` and passing `./scripts/task.sh audit`.

## 2. Build, Test & Verification Commands
- **Fast incremental test build:** `./build.sh --fast --tests` (builds `unit_tests`; never build the full GUI app unless testing app-level wiring).
- **Run unit tests:** `ctest --test-dir build-fast/tests --output-on-failure -j"$(sysctl -n hw.ncpu)"` (never pass `--test-dir build`).
- **Safe subset:** `./build.sh --fast --tests --safe` (canonical list in `scripts/safe-tests.txt`).
- **QML lint:** `./scripts/lint-qml.sh` (must not exceed the 1374-warning baseline).
- **Integration tests:** Excluded from `--tests`; rebuild explicitly when touching `tests/integration/` (`scripts/check-integration-fresh.py` enforces freshness).
- **Sanitizers (required for recorder/threading/session changes):** `./build.sh --tsan --tests && ./build.sh --tsan --safe` (and `--asan`).
- **Headless limits:** Green CI is **not** proof for OpenGL rendering, FFmpeg subtitle burn-in, or live audio/HW encode. Flag those for human/real-window verification.

## 3. Architecture & Language Rules

### C++20 & Threading
- **Cross-thread Qt calls:** Use parameterless `QMetaObject::invokeMethod(this, &Class::slot, Qt::QueuedConnection)` backed by atomics or mutex-guarded queues. Never pass reference-capturing lambdas across threads (`scripts/check-thread-handoffs.py`).
- **Audio callback (`AudioEngine`):** Zero allocations, zero locks, zero resizing on the realtime audio thread.
- **No `Q_ASSERT` in tests:** Release CI compiles `Q_ASSERT` out (`QT_NO_DEBUG`). Use `QVERIFY`/`QCOMPARE` or `qFatal` (`scripts/check-test-asserts.py`).
- **HTTP mocks:** Use `tests/support/FakeNetworkReply.hpp` (`fake_net::FakeReply`). Never create local reply fakes or make live DNS calls in unit tests.
- **Singletons:** Do not add new globals. `ThemeCatalog` (C++) must never be named `Theme` (shadows `Theme.qml`).

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
- **Log scrubbing:** Never log bearer tokens, cookies, raw Clerk `long_message` strings, clip IDs, or local user paths (`DownloadQueue` / `SunoDownloader` must stay ID-free).
- **HAR sanitization:** Never commit raw `.har` files or plaintext tokens. Strip values/cookies/IDs at ingest; promote verified routes to `docs/suno_api/ENDPOINT-INVENTORY.md` as `[T1]`.

## 5. Git, Docs & Multi-Agent Coordination
- **Claims:** Claim tasks via `./scripts/task.sh claim <ID>` (`STATUS/<ID>.claim`) before editing overlapping write-sets.
- **Keep `TODO.md` lean:** Move `[x]` and `[-]` tasks to `docs/dev/TODO-archive.md` and user-facing changes to `CHANGELOG.md` when closing a task.
