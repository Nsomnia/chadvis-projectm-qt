# Changelog

Current release: **1.1.0** — 2026-09-26

This is the canonical changelog for ChadVis. It follows [Keep a Changelog](https://keepachangelog.com/en/1.0.0/) and [Semantic Versioning](https://semver.org/spec/v2.0.0.html). Pre-pivot, oversized history is retained as [docs/CHANGELOG_LEGACY.md](docs/CHANGELOG_LEGACY.md). That file predates version normalization in its `2.1.0`/`2.0.0`/`1.1.0`/`1.0.0` sections, but it also opens with a post-normalization `[Unreleased]` block that carries most of the work now published here as 1.1.0 — so the legacy file overlaps this one rather than being purely historical.

## [Unreleased]

### Fixed

- `chadvis-projectm-qt --version` reported a hardcoded `1.0.0` while the build was `1.1.0`. The banner is now assembled by `vc::Cli::versionBanner()` from the `CHADVIS_VERSION` definition that `version.txt` feeds, and `tests/unit/core/test_Version.cpp` pins the banner text and the real executable's `--version` output to `version.txt`, so a reintroduced literal fails the suite.
- Two out-of-bounds reads in the lyrics path, both reachable from QML. `LyricsData::getTimeRange` clamped only the low end of `startIdx` and let a caller-supplied `size_t` near `SIZE_MAX` wrap `endIdx - 1` into a catastrophic subscript; both indices are now saturated against real bounds. `LyricsSync::getContextLines` checked only the lower bound on the cached `currentPos_.lineIndex`, so loading a shorter song left a stale index that a subsequent query read past the end. A shared `constexpr checkedIndex()` helper replaces the ad-hoc `int`/`size_t` mixing at the QML bridge boundary that allowed both to survive.
- `TestSunoEndpoints` and `TestPresetScanner` were compiled into `unit_tests` but never executed — the fail-closed host-allowlist guard had never actually run. Both are now registered in `test_main.cpp`; the static initializer that called `std::abort()` to make the endpoints test noticeable is removed.

## [1.1.0] - 2026-09-26

This release finalizes the `v1.1.0-BLEEDING_EDGE` line and is the first version whose source, executable, and release metadata are sourced from the same `version.txt` value.

### Added

- Suno-first desktop shell with Library, Create, Listen, Video, and standalone paged Settings surfaces.
- Capture-backed Explore, Notifications, Library pagination/filtering, account/billing surfaces, and the captured three-leg `.m4a` upload transport.
- End-to-end captured aligned-lyrics karaoke: authoritative playback handoff, active-clip gating, late-load synchronization, Listen/Video highlighting, SRT/LRC export, local search, and context/upcoming queries.
- Native visualizer window visibility toggle.
- Offscreen QML integration smoke that requires a visible/exposed root and a non-null rendered frame.
- Focused epoch, media/origin, header, persistence, and audio-concurrency regression coverage.

### Security

- Credential/request epochs fence queued work, retries, auth waiters, and tracked replies across sign-out, replacement, and auth loss; direct uploads cancel on credential invalidation.
- Settings credential ownership is centralized in `SunoClient`; manual edits are debounced off the GUI thread and clearing keeps memory and durable storage coherent.
- Remote artwork, MP3 media, and direct upload URLs are restricted to exact capture-proven HTTPS origins with manual redirects.
- Clerk session selection requires an exact `last_active_session_id` match; undocumented Browser-Token synthesis and automatic `/tokens` fallback are disabled.
- Mutation POSTs are not automatically replayed after 401.

### Fixed

- QML root load failures now fail initialization instead of being logged as success.
- PFFFT scratch is aligned, per-call, immutable-setup backed, and protected from reset/analyze races; deterministic silence is returned on setup failure.
- Audio callback scratch is preallocated; oversized buffers are dropped without allocation.
- FBO, playlist, preset, database, theme, and config persistence failures now produce actionable logs.
- Cached QML bridge statics and application shutdown lifetimes are cleared in dependency order.
- Unit tests no longer write temporary playlists into the real user session file.

### Changed

- `version.txt` is the single source of truth for the CMake project, application version, and CPack version; invalid values fail configuration.
- Added the MIT `LICENSE` file referenced by the README/PKGBUILD and aligned the Arch package to `v1.1.0`.
- PFFFT is pinned to the tested revision; declaration-only Suno fetch declarations and stale legacy API surfaces are removed.
- Library search is explicitly local; the captured feed filter is not extended with unobserved `searchText` behavior.
- Unpromoted Range resume and redirect following remain disabled until capture-backed.

### Verification

- 8/8 CTest targets pass, including epoch, upload, Explore, Notifications, Clerk, AudioAnalyzer, and offscreen QML integration tests.
- QML lint completes on the current entry module.
- The rebuilt desktop executable reaches `QML window created successfully` and `Initialization complete` in a real startup smoke.
- AudioAnalyzer concurrency tests pass under ThreadSanitizer.

## [Unreleased]

### Documentation

- **Documentation hub consolidation** *(2026-09-26)* — Added a single `docs/README.md` hub and folded the `docs/INDEX.md` and `docs/integration/INDEX.md` sub-hubs into it, so the table of contents is stated once instead of three times.
- **Suno API inventory split** *(2026-09-26)* — Split the endpoint inventory so the `[T1]`/`[LEAD]`/`[VERIFY]` contract material stands alone; non-contractual observations (scan output, bundle strings, feature-flag and model-version prose) were demoted to a separate file instead of sitting beside promoted contracts.
- **Raw capture files archived** *(2026-09-26)* — Moved the large raw network-capture artifacts out of the documentation tree; provenance and handling rules remain in the raw README, and the unredacted XML still never enters the repository.
- **Config reference corrected** *(2026-09-26)* — Replaced camelCase keys in the configuration reference that do not exist in `config/default.toml` (for example `bufferSize`, `sampleRate`, `beatSensitivity`, `outputDirectory`, `videoCodec`) with the real snake_case keys.
- **Test-invocation directory corrected** *(2026-09-26)* — `ctest --test-dir build` discovers zero tests and still exits zero; the registered suites live in `build/tests`. Documentation that reported the empty invocation as a passing run now names the real directory.
- **Build output path corrected** *(2026-09-26)* — Fixed the documented binary path to the actual artifact at `build/chadvis-projectm-qt`.
- **Credential instructions corrected** *(2026-09-26)* — Usage docs told users to paste a session token; the Account panel requires the complete `Cookie` request header (`__client…` cookies) and warns that a lone session JWT is not enough. Docs now match the panel and carry that warning.
- **Stale claims corrected** *(2026-09-26)* — Removed the pre-pivot `C++20` references and the "final form of the visualizer" framing, and replaced the "don't ask us to support Windows" line with the actual state: macOS is the only platform run end to end, Linux is the primary development target, and Windows exists only as an unverified NSIS/CPack target.
- **Changelog structure corrected** *(2026-09-26)* — This file's description of `docs/CHANGELOG_LEGACY.md` now states that its post-normalization `[Unreleased]` block duplicates 1.1.0-era work; the legacy file's duplicate `### Fixed` sections and orphaned `### Superseded research notes` heading were folded into one structure.
- **Backlog consolidation** *(2026-09-26)* — `AGENTS.md` and `TODO.md` and `docs/PIVOT_PLAN.md` tracked the same work items under three taxonomies with three status vocabularies. `AGENTS.md` is now operating rules only; every tracked item, including the corrected stale ones, lives in the root `TODO.md`.
- **Endpoint implementation status** *(2026-09-26)* — The route catalog gained an `Implemented` column (`wired` / `declared-unused` / `not-in-code`) so a captured route is no longer mistaken for a shipped one. Of 78 declared route constants, 12 are called from `src/`; the two Clerk routes are built inline rather than declared.

### Security

- **Committed PII removed from the tree** *(2026-09-26)* — `docs/suno_api/raw/endpoints_sniffed.list` was committed and carried third-party DSN keys in userinfo position, a real clip UUID with copyrighted lyrics, and a user upload artifact. It is removed; raw captures no longer live in the repository. The provenance document now states the retention policy the tree previously violated.
- **Account identifiers removed from the tree** *(2026-09-26)* — A committed agent-capture note carried a real email address, display name, handle, and subscription tier. Removed. The tracked tree is now clean of personal data.
- **Backup graveyard pruned** *(2026-09-26)* — 2,298 files / 546 MB purged. Zero files were tracked and the graveyard was gitignored, so nothing source-relevant was lost; git history preserves the originals.
- **Outstanding: history scrubbing** — 13 pushed commits across `main`, `development`, `legacy`, `experiments/juce-refactor`, and three tags still carry account identifiers, a display name, and a third-party token. Every token found is expired, but the account identifiers are permanent and are treated as publicly disclosed. Tracked in `TODO.md`.

### Planned

- Capture-backed generation/CAPTCHA submission and durable queued/processing/failed UI state.
- Native Google sign-in callback evidence and any route-specific header refresh.
- Authorized-account runtime verification for Suno pagination, billing, and media playback.
- Versioned lyric edits, word-level karaoke highlighting, and future release cleanup/refactoring work.

## [Legacy history]

Pre-pivot release notes are preserved in [docs/CHANGELOG_LEGACY.md](docs/CHANGELOG_LEGACY.md). Its `2.1.0`, `2.0.0`, `1.1.0`, and `1.0.0` entries are historical and are not used to derive the current normalized version; its `[Unreleased]` block is the oversized original of the 1.1.0 notes above.
