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
- [x] Run `ctest --test-dir build/tests --output-on-failure` plus the standalone auth executable; all 8 registered suites pass.
- [x] Run QML lint on the current QML module; it completes with existing layout/unqualified-access warnings and the corrected SunoBridge properties.
- [x] Close the asynchronous credential-readiness regression — coalesced keychain restores now notify every caller, and Library/Explore defer authentication decisions until the shared read completes.
- [x] Fence authenticated work by request epoch — sign-out/replacement/auth loss clears queues, retries, and waiters; aborts tracked replies; rebases restore waiters; and cancels direct uploads.
- [x] Close the PFFFT/analyzer race — immutable shared setup, aligned per-call scratch, synchronized state, deterministic setup-failure silence, and normal plus TSan concurrency tests are in place.
- [x] Close the audio callback allocation risk — preallocate the PCM scratch during `AudioEngine::init()` and drop oversized buffers instead of resizing on the callback path.
- [x] Close the unit-test/user-state leak — `AudioEngine` accepts an injected session path, and lyrics pipeline tests persist only into their temporary directory.
- [x] Close the native visualizer no-op — `VisualizerBridge.toggleActive()` now toggles the native visualizer window visibility.
- [x] Close the compiler-visible persistence error gap — FBO resize, playlist/config/theme/database/preset persistence now logs `Result` failures instead of discarding them.
- [x] Close cached QML singleton lifetime risk — parented/unparented bridge singletons clear their static instance pointer on destruction, with a destruction regression.
- [x] Close the shutdown-order risk — `Application` explicitly resets QML, controller/lyrics/preset, recorder, audio, and Qt application lifetimes in dependency order.
- [x] Verify the current shell reaches a rendered window — the integration target loads the real QML module under Qt's offscreen platform, requires a visible/exposed `QQuickWindow` root, and verifies a non-null grabbed frame.
- [ ] Run an authorized-account Library/Create/Listen/Explore/Notifications/Video/Settings smoke.

## Suno client work remaining
- [x] **Credential readiness** — the dedicated restore worker coalesces reads without dropping callers; Library and Explore wait for definitive readiness instead of rejecting an in-flight keychain read as signed out.
- [x] **Settings credential ownership** — `SunoClient` is now the sole durable credential writer; Settings edits are debounced off the GUI thread, cache from the client rather than querying the keychain, and clear live plus persisted credentials together.
- [~] **Authentication** — exact `last_active_session_id` matching, captured `touch`/`client` envelope shapes, credential-prefix normalization, and fail-closed host policy are implemented; automatic `/tokens` fallback, route preference, and sign-out evidence remain open.
- [x] **Remote artwork policy** — clip image fields are sanitized at parse and bridge boundaries; QML receives only HTTPS URLs on the exact captured `cdn1.suno.ai`/`cdn2.suno.ai` origins, with no userinfo, fragment, nondefault port, or local/relative scheme.
- [x] **Fail-closed requests** — active Orpheus/Modal, WAV conversion, constructed-media, user-reachable lead routes, automatic unverified `/tokens` fallback, noncanonical session selection, undocumented Browser-Token synthesis, declaration-only fetch declarations/signals, and automatic mutation replay are removed or disabled; authorized runtime validation remains.
- [~] **Library** — cursor pagination, local filtering, DB merge, and error states are wired; authorized-account pagination and captured-host playback still need runtime verification. Range resume is intentionally disabled.
- [~] **Account/models** — runtime model catalog and numeric account/model fields are wired; account/billing contract fixtures and limit behavior still need capture-backed tests.
- [ ] **Generation** — bind captcha decision and runtime model/catalog/limit data; add fake-request contract tests and durable queued/processing/failed UI state. The current UI intentionally refuses generation without a supported CAPTCHA token flow.
- [~] **Media/download** — playback requires HTTPS on the exact captured `audiopipe.suno.ai` origin, ignores legacy `audio_url` and image hosts, rejects userinfo/fragments/nondefault ports/forbidden sentinels, and uses manual redirects. Unpromoted Range resume is disabled and retries restart from byte zero.
- [x] **Audio upload** — the exact captured initialize → returned direct multipart URL → finish sequence is implemented for `.m4a`; the direct leg now permits only HTTPS on the exact captured `suno-uploads.s3.amazonaws.com` origin with default/443, no userinfo/fragment, and manual redirects. `initialize-clip`, processing, generation linkage, limits/errors, and full validation remain gated.
- [x] **Explore and notifications** — read-only Explore, notification badge/list, and explicit mark-all-read surfaces are wired; following-feed pagination remains unverified.
- [x] **Lyrics/karaoke** — Library Download & Play hands an authoritative clip ID into the active sync pipeline; captured aligned payloads load into Listen/Video without pause/play, stale responses are ignored, lazy bridge wiring is covered, and SRT/LRC export plus search/context/upcoming queries are implemented.
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
