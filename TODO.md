# TODO — ChadVis Qt (Suno Frontend First)

> State marks: `[ ]` todo · `[~]` doing · `[!]` blocked · `[?]` needs you · `[x]` done-awaiting-verification · `[-]` cancelled
> Source of truth for sequencing: `docs/PIVOT_PLAN.md` (phases P0–P7) and `AGENTS.md` backlogs. Rust spec corpus at `~/Documents/suno-media-station-glm5.2/docs` is authoritative; this file tracks the Qt port.

## Current Phase: P2 — Remote Library First (UI pivot post-P1)

### In Progress
- [~] P2 docs harness refresh — AGENTS.md/Pivot Plan/CHANGELOG aligned to Suno-frontend-first; this TODO.md created
- [~] Verify Library full-sync pagination in live app with >20 tracks (auto-page loop + viewport-fill guard)

### Up Next (P2 remainder)
- [ ] Remote library browse/search/filter/sort polish — debounced search is live (350 ms), add sort + tag filter chips
- [ ] Download manager hardening — retryable queue already in `SunoDownloader`/`DownloadQueue` (max 3 concurrent, backoff cap 3); wire HTTP-range resume verification + parity columns
- [ ] Playback parity — single player over remote-preview URL vs local file; buffer-ahead next track
- [ ] Playlist management — listing `GET /api/playlist/me` is T1 captured, mutations still LEAD

### Completed (awaiting verification)
- [x] **Window restore** — `onViewChanged` → `onActiveViewChanged` in `main.qml` (2026-08-27, 55e8dd9); `qmllint` clean
- [x] **Library pagination full-sync** — `SunoLibraryManager` auto-pages feed/v3 cursor loop (1100 ms respect 1 Hz limiter) + `SunoBridge` keeps `loading` true while `hasMore` (2026-08-27, f669457); `LibraryView` viewport-fill guard added
- [x] **Spinner robustness** — uniform failure → `clearLoading` wiring + 15 s watchdog; auth-gated empty-state with Settings navigation
- [x] **P1 Suno Core Correctness** — auth module (Clerk touch, keychain, Device-Id), single queued client, ClipParser, feed/v3, session/billing (a578e1a, 8a2b898, 86ae631)
- [x] **P0 Cross-platform foundation** — macOS 14 / Qt 6.11 / projectM 4.1.6 DPR fix (fd357ff, ca2f33e, e26841f)

### Blocked / Needs You
- [ ] Fresh capture for `GET /api/me/v2/personas` / `POST /api/persona/create/` to promote LEAD → T1 (needed for generation surface persona presets)

### Verified Complete (locked)
- [x] P0 foundation — verified 2026-08-26 (build launches, retina viewport, deepwork state at `.slim/deepwork/suno-frontend-pivot.md`)
