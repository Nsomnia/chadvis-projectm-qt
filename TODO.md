# TODO — ChadVis (Suno client first, projectM second)

> State marks: `[ ]` todo · `[~]` in progress · `[!]` blocked · `[?]` needs a human/capture · `[x]` done, awaiting verification
> Roadmap: `docs/PIVOT_PLAN.md` · API authority: `docs/suno_api/ENDPOINT-INVENTORY.md` · detailed backlog: `AGENTS.md`
> Native Google sign-in remains disabled; captured `tokens` fallback is implemented, while route preference and other `[VERIFY]` contracts remain unresolved.

## Current sprint — Suno client shell and capture alignment

### Documentation and evidence
- [x] **2026-09-24 capture/docs audit** — reviewed 432 Burp items whose timestamps are 2026-09-24 even though the external directory is labeled `sept-09-2026`.
- [x] **Live authority consolidation** — retained the API index, canonical inventory, OAuth gate, and raw provenance as the only live Suno API documents.
- [x] **Directly observed promotions** — session `tokens` top-level JWT, notification read, following feed, explore, audio initialize/finish, direct multipart storage upload, captured feed filters, numeric account/model fields, and forbidden-media sentinel.
- [x] **Provenance safety** — external raw XML remains uncommitted/outside the repository; SHA-256 hashes and handling rules are recorded in `docs/suno_api/raw/README.md`.
- [x] **Stale-claim cleanup** — removed old touch-vs-tokens exclusivity, fabricated CDN fallback, remote-search assertions, historical topic authority, native-login completion claims, and duplicate API prose from roadmap/integration docs.

### Fresh verification required
- [x] Reconfigure and build the current merged tree; the desktop app and focused Suno test targets link successfully.
- [x] Run `ctest --test-dir build/tests --output-on-failure` plus the standalone auth executable; all 6 registered suites pass.
- [x] Run QML lint on the current QML module; it completes with existing layout/unqualified-access warnings and the corrected SunoBridge properties.
- [x] Close the asynchronous credential-readiness regression — coalesced keychain restores now notify every caller, and Library/Explore defer authentication decisions until the shared read completes.
- [~] Verify the current shell reaches a rendered window — the exact current binary reaches `QML window created successfully` and `Initialization complete`; an internal exposed/frame-rendered assertion is still required because macOS denied the external Accessibility check.
- [ ] Run an authorized-account Library/Create/Listen/Explore/Notifications/Video/Settings smoke.

## Suno client work remaining
- [x] **Credential readiness** — the dedicated restore worker coalesces reads without dropping callers; Library and Explore wait for definitive readiness instead of rejecting an in-flight keychain read as signed out.
- [~] **Authentication** — same-host `tokens` fallback, captured `touch`/`client` shapes, credential-prefix normalization, and fail-closed host policy are implemented; route preference, Settings credential ownership, and sign-out evidence remain open.
- [~] **Fail-closed requests** — active Orpheus/Modal, WAV conversion, constructed-media, and user-reachable lead routes are disabled; declaration-only fetch methods and authorized runtime validation remain to be retired or completed.
- [~] **Library** — cursor pagination, local filtering, DB merge, and error states are wired; authorized-account pagination and range playback still need runtime verification.
- [~] **Account/models** — runtime model catalog and numeric account/model fields are wired; account/billing contract fixtures and limit behavior still need capture-backed tests.
- [ ] **Generation** — bind captcha decision and runtime model/catalog/limit data; add fake-request contract tests and durable queued/processing/failed UI state. The current UI intentionally refuses generation without a supported CAPTCHA token flow.
- [~] **Media/download** — captured media selection, forbidden-sentinel rejection, database persistence, and playback handoff are wired; range resume/pause/resume remains to verify.
- [x] **Audio upload** — the exact captured initialize → returned direct multipart URL → finish sequence is implemented for `.m4a`; `initialize-clip`, processing, generation linkage, limits/errors, and full validation remain gated.
- [x] **Explore and notifications** — read-only Explore, notification badge/list, and explicit mark-all-read surfaces are wired; following-feed pagination remains unverified.
- [ ] **Lyrics/karaoke** — load aligned lyrics into the active sync pipeline and finish versioned edits, LRC/SRT export, search, context, and upcoming-line behavior.
- [ ] **Header drift** — capture route-specific Authorization/Browser-Token/Device-Id requirements before changing shared header policy.

## Capture-gated / blocked
- [?] **Native Google callback** — every observed callback/final redirect is Suno-owned HTTPS. A human capture must prove loopback or custom-scheme acceptance, state/nonce/PKCE/transaction behavior, session persistence/refresh, and sign-out before live sign-in can be enabled.
- [ ] **Clerk route selection** — `POST .../tokens` and `POST .../touch` are both directly observed; requiredness and preference order are not established.
- [ ] **Server feed search** — no reviewed request contains `searchText`; use local filtering.
- [ ] **Following-feed pagination** — first-page request/response is observed, but no next-page token/cursor was captured.
- [~] **Upload lifecycle** — initialize/direct-upload/finish is implemented for the captured `.m4a` flow; status, clip initialization, generation linkage, limits, and full validation are not captured.
- [ ] **Orpheus/B-Side/VIP/hidden features** — remain disabled unless a new direct capture and product decision promote them.
- [ ] Fresh captures needed for playlist mutation, error/rate-limit envelopes, direct generation drift, and other `[LEAD]` routes.

## Roadmap order
1. Fresh build/test/runtime verification and lead-route fail-closed hardening.
2. Library/account/media correctness.
3. Captcha-backed generation and the bounded audio-upload lifecycle.
4. End-to-end aligned-lyrics karaoke.
5. Real recording review, deterministic export, scene/keyframes, and batch automation.

## Verification bar
A task is not complete from a stale binary, declaration, scan string, or historical commit. Verify the current source with a fresh configure/build, relevant tests, focused lint/format checks, and a real runtime path; update `CHANGELOG_CURRENT.md` with observed results.
