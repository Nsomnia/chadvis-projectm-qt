# Changelog

Current release: **1.1.0** — 2026-09-26

This is the canonical changelog for ChadVis. It follows [Keep a Changelog](https://keepachangelog.com/en/1.0.0/) and [Semantic Versioning](https://semver.org/spec/v2.0.0.html). The pre-pivot, oversized changelog is retained as [docs/CHANGELOG_LEGACY.md](docs/CHANGELOG_LEGACY.md); its `2.x`/`1.x` sections predate the version normalization in `version.txt`.

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

- Capture-backed generation/CAPTCHA submission and durable queued/processing/failed UI state.
- Native Google sign-in callback evidence and any route-specific header refresh.
- Authorized-account runtime verification for Suno pagination, billing, and media playback.
- Versioned lyric edits, word-level karaoke highlighting, and future release cleanup/refactoring work.

## [Legacy history]

Pre-pivot release notes are preserved in [docs/CHANGELOG_LEGACY.md](docs/CHANGELOG_LEGACY.md). The `2.1.0`, `2.0.0`, `1.1.0`, and `1.0.0` entries there are historical and are not used to derive the current normalized version.
