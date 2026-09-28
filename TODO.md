# TODO — ChadVis (Suno client first, projectM second)

> State marks: `[ ]` todo · `[~]` in progress · `[x]` done, awaiting verification ·
> `[?]` needs a capture or a human decision before it can *ship* · `[!]` needs user attention now
> Roadmap: [`docs/PIVOT_PLAN.md`](docs/PIVOT_PLAN.md) · API authority:
> [`docs/suno_api/ENDPOINT-INVENTORY.md`](docs/suno_api/ENDPOINT-INVENTORY.md) ·
> docs hub: [`docs/README.md`](docs/README.md) · operating rules: [`AGENTS.md`](AGENTS.md)
>
> **Nothing in this file is blocked.** A `[?]` mark means a *ship gate*, not a stop: the
> surrounding engineering is still ours to do, and an item marked `[?]` is expected to be
> worked up to the gate and left fail-closed rather than parked. Where a capture would
> unblock something, the item says exactly which capture and what the code does until then.
> The one thing that does not bend is the evidence rule: an unverified route or host is
> never wired, and a `[LEAD]` is never promoted by inference.

This file is the single backlog. Rules live in `AGENTS.md`; do not add work items there.

---

## P0 — Critical: bugs, data loss, security

### Memory & thread safety
- [~] **Lyrics out-of-bounds subscripts** — *retargeted 2026-09-26: the reported class, `LyricsOverlayRenderer`, was deleted in `0530240` and replaced by QML `KaraokeMaster.qml`, so the original item named dead code.* Two unguarded subscripts survive in the current lyrics code, both reachable from QML: `LyricsData::getTimeRange` (`startIdx` clamped only at the low end, and `endIdx - 1` wrapped to `SIZE_MAX` by a near-`SIZE_MAX` `size_t` index) and `LyricsSync::getContextLines` (only the lower bound on the cached `currentPos_.lineIndex` is checked, so a stale index after loading a shorter song reads past the end). Root cause is the ad-hoc `int`/`size_t` mismatch at the QML bridge boundary with no shared bounds helper. Fix adds a `constexpr` checked-index helper and covers empty-`words` and `endTime < startTime` remote-data edge cases.
- [x] **Preset scanning blocks the GUI thread** — *the premise was half-stale: `PresetBridge::rescan()` was already async-ish, but the invalidation hazard was real and startup was fully synchronous.* Fixed in `f121901`. `PresetManager` gained `scanAsync`/`rescanAsync`/`setPublishContext` with **shared generations**: every scan builds a fresh vector and *swaps it in*, so a published generation is never cleared/resized/refilled in place and a `const PresetInfo*` taken before a rescan stays valid. Readers get a `Snapshot` (`shared_ptr<const PresetList>`) or a `PresetView` (query + pinned generation). Worker is a `vc::JThread` parked on a condition variable (`VideoRecorderThread` pattern) — not a moved-`QObject` `QThread`, because `PresetManager` is a *value member* of `pm::Bridge` — with `CredentialStoreWorker`'s `invokeMethod(..., Qt::QueuedConnection)` hand-off reused verbatim. Requests coalesce latest-wins (N rescans cost ≤2 walks); a failed scan retains the previous generation instead of silently emptying a working library. `Application.cpp:391` startup now uses `scanAsync` with a publish context. The synchronous `scan()` is **kept intact** on purpose: `pm::Bridge` reads `empty()`/`selectByIndex(0)` immediately after (Bridge.cpp:98) and never sets a publish context, so it never starts a worker. Also hoisted the per-file `std::regex` out of the scan loop (it compiled a grammar per preset, the dominant cost after the stat storm) with a provably-equivalent `find('-') == npos` early-out. **Pre-existing bug found en route:** `PresetBridge::connectSignals()` was declared, defined, and never called from anywhere — and because `qmlEngine_->load()` runs *inside* `init()`, QML's one-time `model: PresetBridge.filteredPresets()` binding evaluates before an async scan publishes, so the startup list would have stayed **permanently empty**. Wiring now happens in an idempotent `attachManager()` from the ctor. 8 preset tests, 0 skipped, including a pinned-generation case that fires 8 rescans and asserts the old pointers stay valid while a new generation appears at a different address.
- [ ] **Playlist not thread-safe** — accessed from UI and audio threads without synchronization.
- [x] **Dual position update path in `LyricsSync`** — *recon 2026-09-26: `updatePosition` has two independent entry points.* The `QTimer::timeout` lambda (`LyricsSync.cpp:18-22`, GUI thread) and **synchronous direct calls from `loadLyrics`/`seek`/`jumpToLine` (`:58`, `:111`, `:214`)**. `updatePosition` is a read-modify-write over `smoothedTime_` (a `lerp` at `:131`) plus a compare-then-write over `currentPos_` in `detectChanges` (`:161`), so the two paths double-apply smoothing for one audio instant and can drop or duplicate a `lineChanged`/`wordChanged` edge. `LyricsSync` has zero synchronization — no mutex, no atomics, no thread assertion. Two aggravating details: `trackChanged` is connected `Qt::DirectConnection` (`:30-33`) so `clear()` runs a full state reset off the event loop's ordering, while its `stateChanged` sibling correctly uses `Qt::QueuedConnection` (`:25-28`); and `AudioEngine::positionChanged` is consumed by `AudioBridge` but **not** by `LyricsSync`, so lyrics poll at 16 ms while the transport pushes. **Fixed in `f10f87d`:** the `QTimer` is now the sole writer via a one-shot seed drained by a new public `syncNow()`; `seek`/`loadLyrics` seed instead of applying; `trackChanged` switched to `QueuedConnection`; thread asserts added to the three public mutators. Deliberately **no mutex** — two writers plus an unguarded `lyrics_.vector` would be worse. Two subtleties found and handled: a seed must *snap* (the drain pre-sets `smoothedTime_` so the lerp degenerates) or `seek` stops being exact and a mid-playback `loadLyrics` ramps from 0 instead of snapping; and `seek` with a null `AudioEngine` never drains on its own, which is what forced `syncNow()` to be public rather than a test-only backdoor. Follow-ups still open: `AudioEngine::positionChanged` is deliberately still unconsumed (polling is unchanged behaviour, and a second trigger invites a second writer), and the `Seeking → Syncing/Paused` double `stateChanged` in `seek` remains. Related: `LyricsBridge::onLineChanged`/`onWordChanged` (`LyricsBridge.cpp:180-189`) are dead slots never connected in `connectSignals` (`:96-124`) — a third would-be writer of the five position members.
- [ ] **Playlist::addFile reads metadata synchronously** — blocks the UI thread on tag reads; move to a worker.
- [x] **PFFFT static locals thread-unsafe** — shared immutable RAII setup, 16-byte-aligned per-call FFT scratch, mutex-protected state, deterministic zero-on-setup-failure, plus normal and ThreadSanitizer tests.
- [x] **SunoClient use-after-free** — request epochs fence queued retries/waiters, tracked replies are disconnected and aborted, stale restore/upload callbacks cannot publish after credential invalidation.
- [x] **QML cached singleton lifetime** — cached bridge pointers reset when their singleton is destroyed.
- [x] **VideoRecorderFFmpeg nullptr deref** — both video and audio `avcodec_alloc_context3()` results checked.
- [x] **AudioEngine scratch buffer resize in the audio callback** — scratch preallocated during `init()`; oversized callback buffers dropped.
- [x] **Application destruction order** — QML, controller, lyrics, preset, recorder, audio, and Qt application torn down in dependency order.
- [x] **Playlist::loadM3U path traversal** — relative entries weakly canonicalized and rejected when they resolve outside the playlist directory.
- [x] **Recording video/audio settings persist** — re-verified after a false report: `ConfigParsers::serialize` builds the `[recording.video]` and `[recording.audio]` sub-tables from the live struct via `CHADVIS_VIDEO_FIELDS`/`CHADVIS_REC_AUDIO_FIELDS` before flattening the plain recording fields, which is the shape the parser reads and the shape `config/default.toml` ships. No bug.
- [ ] **Stored-credential classifier rejects valid cookie jars** — found 2026-09-28. `classifyStoredCredential` (`src/suno/auth/ClerkAuthClient.cpp:171-173`) requires an **exact** cookie name of `__client` or `__client_uat`; anything else falls through to `Unsupported`. But Clerk writes **instance-suffixed** variants alongside them — the 2026-08-25 capture shows `__client_Jnxw-muT` and `__client_uat_Jnxw-muT` on the wire, and the inventory already documents the suffixed forms. A jar that carries only the suffixed pair (or only `__session`, which is the actual access-token cookie) is **rejected**, and the failure surfaces to the user as a generic unsupported-credential message rather than a wrong-cookie diagnosis. The comparison should be a prefix match on `__client` / `__client_uat` / `__session`, matching the inventory's own wording, plus a distinct error string naming which names were seen. Note the whole header is still forwarded opaquely on the happy path, so this is an ingress-UX bug, not a live auth outage.
- [ ] **`LyricsData::toSrt`/`toLrc` are dead duplicates of the real implementation** — found 2026-09-28. `LyricsData.cpp:488`/`514` are shadowed by `LyricsBridge::exportToSrt`/`exportToLrc` (`LyricsBridge.cpp:210`/`237`), which re-implement the formatting with their own `formatSrtTime`/`formatLrcTime` (`LyricsBridge.cpp:15`/`28`). Both `LyricsData` free functions have **zero callers**, so the closed "SRT/LRC export" item above is testing a *second, unused* implementation. Delete the dead pair and point the tests at the live one, or make `LyricsBridge` delegate — do not keep two formatters, because they will drift.
- [ ] **Frame pacing: the timestamps are already captured and thrown away** — this de-rates the P1 pacing item below. `GrabbedFrame` carries `frameNumber` and a microsecond `timestamp` (`src/recorder/FrameGrabber.hpp:18-24`), `VisualizerWindow::frameCaptured` already threads the timestamp through, and `VideoRecorderFFmpeg::encodeVideo` discards both and sets `pts = videoFrameCount_++` (`VideoRecorderFFmpeg.cpp:247`). The raw material for audio-locked PTS is therefore already in hand; the fix is inside the encoder, not a new capture pipeline. The drop counter is already surfaced to QML as `bufferHealth` (`RecordingBridge.cpp:81-88`), so drift is observable on screen today.

### Provenance and evidence integrity
- [!] **The 2026-09-24 corpus is gone; the hash manifest is unverifiable** — found 2026-09-28. [`docs/suno_api/raw/README.md`](docs/suno_api/raw/README.md) publishes a SHA-256 manifest for a 432-item corpus plus 13 sibling files, and most `[T1]` promotions in the master rest on it. On the current disk that corpus **no longer exists**: `~/Documents/suno-burp-exports/sept-09-2026/` now holds one file named `was-base64-need-real-text` of **0 bytes** (SHA-256 `e3b0c442…`, the digest of the empty string), and `audiophile.suno.ai`, `clerk.suno.com`, and `studio-api.prod.suno.com` are well-formed Burp XML with **zero** `<item>` elements. The surviving `auth.suno.com` hashes `1b19e06d…`, not the `fdf9794b…` the manifest claims. Consequences: (a) no manifest entry can be re-verified, and (b) the file the user believed contained the new endpoint facts is a 0-byte placeholder. The manifest is the *only* record of what was reviewed, so **do not delete it** — record the observed on-disk state alongside it, and re-capture the corpus before any promotion that depends on re-reviewable bytes. This is a documentation-integrity defect, not a reason to demote any existing `[T1]`.
- [ ] **Capture hygiene: never let an export be truncated in place** — the `sept-09-2026` directory kept its name while its contents were replaced by an empty placeholder, which is how a manifest silently stopped matching. Raw captures live outside the repo, so nothing in the tree can detect this. Make the retention rule concrete: write a new dated directory per capture, never overwrite, and have the manifest record a *locatable* path per entry so a missing file is obvious rather than silent.


### Security & credentials
- [~] **Native OAuth security** — target architecture is system browser plus an app-owned `127.0.0.1` loopback callback for Google/Facebook social login; the offline scaffold covers state/nonce/PKCE/transaction ownership. Outstanding: capture-backed proof of Clerk's loopback acceptance, plus session persistence/refresh and sign-out behavior, before the live handshake is trusted.
- [x] **History scrubbing for committed PII** — *executed and force-pushed 2026-09-28.* A secrets audit found permanent personal data in pushed history: account identifiers, a handle, a clip UUID with copyrighted lyrics, a user upload filename, complete media URLs, OAuth transaction material, and expired JWTs (in *code*, not just prose). Every credential found is expired, so nothing is replayable today; the reason to rewrite is that account identifiers do not expire. The live tree was already clean on 2026-09-26 — this was history only. **What was done:** 22 dead PII-bearing paths removed and one literal handle replacement via `git-filter-repo` 2.47.0, after a full dry run on a throwaway mirror, then force-pushed to `origin` with `--force-with-lease` for both branches and `--force` for all three tags. Handle, real JWTs, upload filename, clip UUIDs, and OAuth sign-in ids are now **0** across every ref a client can clone; `HEAD`'s tree is byte-identical to the pre-scrub mirror; 454 commits and 6 refs were preserved; old tag trees show pure deletions with 0 files added. Confirmed from a fresh ordinary clone of the remote. **Residual, and it is not fixable from the client:** GitHub's server-managed `refs/pull/{4,5,6,9}/head` still reach pre-scrub commits (all four PRs are merged or closed, and **0** real JWTs remain even there). Every deletion was refused with `deny updating a hidden ref`; an ordinary clone does not fetch them, but the PR diff pages remain on github.com and clearing them needs GitHub Support. **The account email was deliberately retained** by owner decision (285 occurrences) — do not re-scrub it without asking, and do not read its presence as an oversight. **The plan was wrong about the scope:** it assumed ~5 blobs, but **26 files** carried PII, including `docs/deepwiki/` generated exports, `.backup_graveyard/`, and `.agent/`. It also repeated a "13 commits" figure across `CHANGELOG.md:98` and the plan that disagreed with reality; commits was the wrong axis, since one commit can carry many PII files. Full record, including two `filter-repo` corrections and the false-positive audit: [`docs/PII_SCRUB_PLAN.md`](docs/PII_SCRUB_PLAN.md).
- [x] **Credential storage audit** — tokens in the OS keychain via `CredentialStore` (macOS Security.framework, atomic 0600 fallback); TOML→keychain migration on first init; secrets never written to TOML or logs.
- [x] **SQL injection risk in `search_db.sh`** — queries use SQLite `.param` binding.
- [x] **Raw capture removed from the tree** — `docs/suno_api/raw/endpoints_sniffed.list` (721 KB) held DSN keys in userinfo position, a real clip UUID with copyrighted lyrics, and a user upload artifact. Archived out of the repository; the sanitized recon is the only retained raw artifact. The graveyard was purged (546 MB, 0 tracked files).

---

## P1 — High: broken behavior, major architecture

### Suno integration
- [~] **Suno core correctness** — credential storage, lossless asynchronous restore readiness, debounced Settings ownership through `SunoClient`, credential/request epochs with reply aborts, no-replay mutation policy, queued authenticated requests, feed/clip/account parsing, exact Clerk active-session selection, method-specific Studio headers, and isolated persistence seams are implemented. Route preference, sign-out, and authorized runtime behavior remain `[VERIFY]`.
- [~] **Fail closed on unverified routes and hosts** — active Orpheus/Modal, WAV conversion, legacy Clerk-host fallback, constructed media, and user-reachable lead routes are disabled; QML artwork is restricted to exact captured Suno CDN origins; the synthetic Browser-Token is removed; declaration-only fetch surfaces are retired. Never reintroduce an unverified header without a fresh capture.
- [~] **Authentication** — exact `last_active_session_id` matching, captured `touch`/`client` envelope shapes, credential-prefix normalization, and fail-closed host policy are implemented. Outstanding: route preference and preference order, sign-out evidence, and the Clerk `tokens` route — it is captured as `[T1]` but **not implemented**; the string `tokens` appears nowhere in `src/suno/`.
- [ ] **The whole token hierarchy is now captured; only the refresh half is unwired** — *found 2026-09-28 from a 2026-08-25 `auth.suno.com` Burp export (13 API items), and the *shapes* are now documented in the master as §3.3.* Three distinct credentials are directly observable, and the docs previously described only the first: the **access token** is the JWT we already send as `Authorization: Bearer` (RS256, `kid: suno-api-rs256-key-1`, `aud: "suno-api"`, `azp: "https://suno.com"`, measured lifetime **exactly 3600 s**); the **refresh token** is a *separate* credential in the `__client` cookie (`token_type: "refresh"`, `client_id` + `secret` claims, ~1 year, matching the observed `Max-Age=31536000`); and the **handshake nonce** is a one-shot `Max-Age=0` JWT whose payload carries a literal array of `Set-Cookie` directives, which is the sanctioned mechanism for exchanging a refresh credential for a session credential. None of `aud`, `azp`, `suno/did`, `suno/handle`, `suno/user_id`, `plan`, `jit`, `x-ably-token`, or the refresh/handshake shapes appear anywhere in `src/`. The 1-hour access lifetime independently **confirms** the existing 55-minute proactive-refresh cap (`SunoClient.cpp:32-34, 672-681`) is correct rather than arbitrary. Remaining work is the wiring, not the documentation.
- [x] **`POST /v1/client/verify` is resolved to POST and its purpose is known** — *found 2026-09-28, folded into the master.* The master recorded this route as a method/contract conflict (`ENDPOINT-INVENTORY.md:484`) and its Appendix A carried it as unresolved. Five direct observations in the 2026-08-25 capture settle it: **POST**, form body `captcha_token` + `captcha_widget_type=invisible` + `captcha_action=heartbeat`, response **204 No Content**. It is a captcha-gated Clerk session heartbeat, corroborated three ways: the field is literally `captcha_action=heartbeat`, the Clerk instance reports `captcha_heartbeat: true`, and the calls are spread 1–8 minutes apart with no other traffic. The existing safety conclusion holds and is now *proven* rather than merely prudent — a 204 with no body cannot return a bearer, so this route definitively cannot be used for token refresh. Stays `not-in-code` until something needs it, and the now-stale `[VERIFY]` lead row was removed from [`OBSERVED-LEADS.md`](docs/suno_api/OBSERVED-LEADS.md) in the same pass. Note `/v1/verify` is a **different, still-unobserved** route, kept separate in four places.
- [x] **Two distinct captcha systems, previously conflated in the docs** — *found 2026-09-28, folded into the master as §3.4.* The master's hCaptcha host rows (`:156`) and its "Turnstile heartbeat" note (`:485`) both mentioned captcha without ever stating they guard different things. They do: the **Clerk auth** captcha is **Cloudflare Turnstile** (`captcha_provider: "turnstile"`, `captcha_heartbeat: true`, `captcha_widget_type: "smart"`, plus a managed and an invisible sitekey), and the tokens on the wire have Turnstile's `1.<payload>.<hash>` shape, which rules hCaptcha out for that route; the **suno.com web sign-in UI** captcha is **hCaptcha** (the sanitized recon records `api.hcaptcha.com/getcaptcha/…` on the session-recovery pages). **This did not unblock generation:** `/api/c/check` governs that, its provider is still unstated, and its `[T1]` label is unchanged — the generation gate below stands exactly as before. The master also records the rule that naming a captcha *provider* adds no captcha *host* to any allowlist.
- [~] **Library** — cursor pagination, local filtering, DB merge, and error states are wired. Authorized-account pagination and captured-host playback still need runtime verification. Range resume is intentionally disabled.
- [~] **Account and models** — runtime model catalog and numeric account/model fields are wired. Account/billing contract fixtures and limit behavior still need capture-backed tests.
- [~] **Media and download** — playback selects only HTTPS media-array entries on the exact captured `audiopipe.suno.ai` origin, rejects forbidden sentinels, userinfo, fragments, and nondefault ports, and uses manual redirects. Unpromoted Range resume is disabled; retries restart from byte zero. Note the captured `m4a-opus`/`progressive` item is currently unplayable because the downloader accepts only `content_type == "mp3"`.
- [~] **Upload lifecycle** — initialize → direct multipart → finish is implemented for the captured `.m4a` flow. Status, clip initialization, generation linkage, limits, and full validation are not captured.
- [ ] **Generation surface** — bind typed model/catalog/limit data and the captured captcha decision to the request; add fake-request contract tests and durable queued/processing/failed UI state. Generation stays disabled until a supported CAPTCHA token flow exists. **Note the captcha knowledge gained above is for Clerk, not generation** — do not let it be read as progress on this item.
- [ ] **Header drift capture** — reconcile route-specific Authorization/Browser-Token/Device-Id requirements from a fresh sanitized capture before changing shared header policy. Two specific drift signals to resolve while capturing: the code sends `GET /v1/client` with **no** `__clerk_api_version`/`_clerk_js_version` query keys and hardcodes `intent=focus` (the 2026-09-24 variant), while the 2026-08-25 capture used version query keys and an **empty** body on `touch`; and the version values are **route-specific** (`handshake` used `__clerk_api_version=2025-04-10`, `/v1/client` used `2025-11-10` in the same capture), so a single constant would be wrong.
- [ ] **Video URL origin guard** — `ClipParser` stores `video_url` raw and persists it to SQLite with no origin check, unlike the image fields, and no video host is documented in the inventory. Add a captured-host allowlist or drop the field.
- [x] **Credential readiness** — the dedicated restore worker coalesces reads without dropping callers; Library and Explore wait for definitive readiness.
- [x] **Settings credential ownership** — `SunoClient` is the sole durable credential writer; edits are debounced off the GUI thread; clearing removes live and persisted credentials together.
- [x] **Remote artwork policy** — clip image fields sanitized at parse and bridge boundaries; QML receives only HTTPS on the exact captured `cdn1.suno.ai`/`cdn2.suno.ai` origins.
- [x] **Explore and notifications** — read-only Explore and notification list/badge/mark-all-read are wired from direct captures. Following-feed pagination remains `[VERIFY]`.
- [x] **Lyrics and karaoke** — captured aligned payloads flow from authoritative playback handoff into `LyricsSync` and `LyricsBridge` for Listen and Video, with late-response gating, SRT/LRC export, local search, and context/upcoming queries.
- [x] **2026-09-24 capture and docs audit** — 432 Burp items reviewed; only directly observed contracts promoted.
- [x] **Session-selector identity corroborated** — *found 2026-09-28.* The code re-derives the active session id from the access token's `sid` claim (`SunoClient.cpp:660-663`) rather than from `response.last_active_session_id`, which looked like a deviation from the master. The 2026-08-25 capture shows all four identifiers are the same value: `last_active_session_id` == `sessions[0].id` == the JWT `sid` claim == the `{sid}` path segment of the `touch` request. Honest limit: one capture with a single session, so this corroborates the substitution rather than proving it for multi-session accounts — keep the master's "do not assume array position" rule.
- [x] **`Implemented` column re-audited against source** — *found 2026-09-28.* `SunoEndpoints.hpp` has 77 route constants of which exactly **12 are referenced**; the master's §1.3 figure is confirmed correct and no `Implemented` value is contradicted. Worth keeping as a periodic re-audit, because the ratio (65 dead constants) is the real measure of how much captured surface is unwired.
- [x] **Six OAuth providers are enabled server-side, not just Google** — *found 2026-09-28, folded into the master as §3.4.* `GET /v1/environment` reports `identification_strategies` = Google, **Facebook, Apple, Discord, Microsoft**, Apple-token, and phone number, with `password: "on"` and `preferred_sign_in_strategy: "otp"`. The product scope names only Google and Facebook. Apple Sign In is notable on macOS because `AuthenticationServices` can produce the `oauth_token_apple` credential natively, which would remove a browser round trip — but that is a design decision, not an inference, so it belongs in the proposals section below rather than here. Recorded as an instance-configuration fact, not as an endorsement or evidence that any extra provider works as a native callback.
- [x] **The master gained §3.3/§3.4 and its document map did not list them** — *found and closed 2026-09-28.* A reader could reach §3, follow its intro pointer to [`OAUTH_REDIRECT_ANALYSIS.md`](docs/suno_api/OAUTH_REDIRECT_ANALYSIS.md) for cookies, and never learn the credential *shapes* are documented at all. Both the master's `Document map` and [`docs/suno_api/README.md`](docs/suno_api/README.md) now name §3.3 and §3.4 with links. Worth noting how this was caught: a first link-checker reported these anchors broken, then a second reported the long-standing `#appendix-a--explicit-conflict-register` anchor broken. Both reports were wrong, in opposite directions — the first because it collapsed whitespace where the slugger keeps one hyphen per space, the second because it assumed em-dash stripping left a single hyphen. Anchors are only trustworthy when the slugger is implemented to match, and the final pass verified all in-page anchors across every doc against real headings.

### Core infrastructure
- [!] **`SunoWorkspaceBridge` is fully implemented and completely unreachable from QML** — found 2026-09-28, and this is the single highest-leverage defect in the tree. `src/qml_bridge/SunoWorkspaceBridge.hpp` carries **18 public members** — `generating`, `generatedClips`, `startGeneration`, `cancelGeneration`, `regions`, `addRegion`, `removeRegion`, `clearRegions`, `stems`, `requestStems`, `lyricsAvailable`, `setLyricsText`, `rendering`, `renderProgress`, `startRender`, `cancelRender`, `saveWorkspace`, `loadWorkspace` — and holds a real `vc::suno::SunoWorkspace*` (`:94`). It is simply absent from `qmlRegisterSingletonType` in `BridgeRegistration.cpp:27-39`, and no QML file references it. That is the **entire generation → regions → stems → render → persist chain**, one registration line away. Nothing else in the backlog unlocks as much finished work for as little code, so do this before any new feature work.
- [ ] **~570 LOC of fully orphaned duplicate settings UI** — `src/qml/views/SettingsView.qml`, `src/qml/panels/SettingsPanel.qml`, and all 8 `src/qml/panels/settings/*.qml` duplicate the live `SettingsWindow.qml` + `settings/*Page.qml` path, and **no `Loader` can reach them**. `panels/settings/SunoSettings.qml` is the one deliberate exception (shared with the live `AccountPage`). Either delete the dead path or re-point the shell at it; carrying two settings systems is how they drift, and it already has — see the version-string and i18n items under P2.
- [~] **Sidebar panel migration** — `PlaylistBridge` and `RecordingBridge` APIs fixed; LyricsBridge search/export and karaoke persistence are now complete. Remaining: verify each panel against the seven-surface shell.
- [x] Refactor `main.qml` with responsive layout
- [x] Refactor `AudioEngine` for granular responsibility
- [x] Throttled bridge updates in `VisualizerBridge`/`AudioBridge`
- [x] Robust settings persistence with 2 s debounced auto-save and explicit save on close
- [x] `UIConfig` expanded with `expandedPanel`, `sidebarWidth`, `drawerOpen`; parsed, wired bidirectionally, and added to `default.toml`
- [x] Config parser defaults derive from default-constructed `ConfigData` (single source of truth)
- [x] `Config::save()` errors log actionable failures at their call sites
- [x] Removed ~20 stale cmake modules; `cmake/` now holds 7 modules in active use — `CPM.cmake`, `Compiler.cmake`, `Dependencies.cmake`, `FindProjectM4.cmake`, `Install.cmake`, `Sources.cmake`, `TargetSetup.cmake`. All are included by the top-level `CMakeLists.txt`; do not delete them.
- [x] `test_PresetScanner` wired into `unit_tests`; `test_projectm_render` stub archived. *Corrected 2026-09-26: it was compiled into `unit_tests` but had no runner and never executed, so the `[x]` was a declaration, not a verification — the exact failure `AGENTS.md` §3 warns about. `runTestPresetScanner` now exists and is called from `test_main.cpp`.*

### Music video creator — the primary end goal

Recon 2026-09-26. The **encoder is done and real** (`VideoRecorderCore` state machine, genuine `avformat`/`avcodec`/`sws`/`swr` in `VideoRecorderFFmpeg.cpp:88-328`, a `JThread` worker with a bounded queue, exclusive-create + `flock` output claiming, and 1920×1080@60/CRF18/AAC defaults), and per-frame capture of the live projectM state works via an **undocumented** path: `VisualizerRenderer::renderFrame` → FBO → `captureAsync` (PBO) → `frameCaptured` → `VisualizerWindow::frameCaptured` → `Application.cpp:419-425` → `VideoRecorderThread::threadLoop`. `test_RecordingPipeline.cpp` really does run (called from `test_main.cpp:30`) and covers frames-before-and-after worker creation. Everything below is what stands between that encoder and a batch music-video creator.

- [x] **P0: projectM v4 cannot render into our FBO, so recorded video is an empty buffer** — *fixed 2026-09-26, both halves now runtime facts rather than source reading.* `ProjectM.cpp:169-170` reads `// ToDo: Allow external apps to provide a custom target framebuffer.` and then does `glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0)`, and the copy that follows (`CopyTexture.cpp:99-109`) binds only a *texture* to unit 0 and draws "to the currently bound buffer" — i.e. framebuffer 0. Our `renderTarget_` FBO was therefore **never written by projectM**; it held only the `glClearColor(0,0,0,0)` from `RenderTarget.cpp:92-93`, and the blit shader painted that emptiness over the live window. **Measured, before the fix:** with an FBO bound as `GL_DRAW_FRAMEBUFFER` across 30 rendered frames, the FBO read back **0 non-zero bytes** while the default framebuffer read back **262 107** — `engineIgnoresAnFboBoundAsDrawTarget`, which binds the FBO itself so the only thing under test is projectM's draw target. The renderer's own recording path measured **0 of 262 144** bytes non-zero. **The fix:** framebuffer 0 *is* the target. `renderFrame` now renders projectM straight into it under a scissor box and reads the recording back out of the same buffer; the `useFBO` branch, the blit program/VAO/VBO and its shader, `flipImageGPU`, `flipVertical_`, `useGPUFlip_`, `FrameGrabber::grab`/`grabScreen`, `Engine::renderToTarget`, and `RenderTarget::bindDefault`/`readPixels`/`blitTo`/`blitToScreen` are all gone, because every one of them was either the bug or a trap that invited it. A recording resolution that differs from the window's is now a *readback* scale (`RenderTarget::blitFromDefault`, the one FBO use left, and it is a copy target by construction). **Measured, after the fix:** the readback carries 262 088 of 262 144 bytes, the live native window captures 261 972, and a real 45-frame **H.264 MP4** decodes to a 256x256 stream whose brightest frame has **65 337 lit luma pixels** (`encodedFileContainsTheVisualizerPicture`). The live window no longer has a recording-specific branch at all, so the predicted "window goes black when recording starts" cannot happen.
- [x] **Spike first: prove the destination, offscreen feasibility, and the empty-FBO prediction** — *done 2026-09-26 as seven permanent tests in `tests/integration/test_ProjectMFramebuffer.cpp`, run by the `integration_gl_tests` ctest entry.* Both halves of the prediction were confirmed and are now regression guards. Two build facts in the original plan were wrong and are corrected here: `tests/integration/CMakeLists.txt` did need `Qt6::OpenGL`, and **`QOffscreenSurface` is unusable for this** — on macOS Qt hands it a **1x1 drawable**, so any larger `glReadPixels` is out of bounds and returns zeros with `GL_INVALID_FRAMEBUFFER_OPERATION` (0x506); the fixture is a real `QWindow` instead, and it needs `setSurfaceType(QWindow::OpenGLSurface)` (what `VisualizerWindow.cpp:24` already does) or `makeCurrent` refuses with "called with non-opengl surface", plus an event-loop spin until `isExposed()`. The macOS `offscreen` QPA plugin **cannot create an OpenGL context at all** ("This plugin does not support createPlatformOpenGLContext!"), so the split is real: `integration_tests` runs offscreen and every GL test *skips with a stated reason*, while `integration_gl_tests` runs the native plugin and actually executes them. A skip is not a pass — the GL entry is the one that counts, and its verbose output shows the byte counts. Two build traps closed along the way: a test-function argument list is replayed against every suite in the binary ("Function not found" in the other class), so `test_main.cpp` now runs the GL suite alone when arguments are present; and `vc::AVFormatContextDeleter` is write-side only (it dereferences `oformat`, which a demuxer never sets), which segfaults the moment a test uses `AVFormatContextPtr` to *read* a file.
- [ ] **The frame clock is the actually hard problem, not `isExposed`** — corrected 2026-09-26. The earlier claim that an unexposed window blocks offline rendering is a **red herring**: `projectm_set_window_size` is an int stash (`ProjectM.cpp:219-224`) and `libprojectM` has no window-system dependency (the SDL UI is `ENABLE_SDL_UI OFF`, `cmake/FindProjectM4.cmake:66`), so projectM needs only a current GL 3.3 context. The real constraint: **projectM v4 is wall-clock only.** `TimeKeeper::UpdateTimers` (`build/_deps/projectm-src/src/libprojectM/TimeKeeper.cpp:17-26`) computes `m_secondsSinceLastFrame` from `high_resolution_clock::now()`, and that value drives `PCM::UpdateFrameAudioData` → `Loudness::AdjustRateToFps` (`Audio/Loudness.cpp:53-56`); `ctx.time` (`ProjectM.cpp:435`) is a standard milkdrop per-frame variable, so **preset animation phase is wall-clock bound.** No `projectm_set_time` or synthetic clock exists in any public header; `projectm_set_fps` only sets a shader uniform. So 30 frames in 0.1 s is 0.1 s of projectM time, not 0.5 s. **v1 must be realtime-throttled** (sleep to each frame's correct virtual instant; a 3-minute song takes 3 minutes, recovered by running N jobs in parallel, one GL context each). Put the clock behind an injected seam so a later `TimeKeeper` fork (~20 lines in a 118-line self-contained class, requiring vendoring the CPM dep) can turn 3 min into ~15 s. **Do not pay that fork until v1 works and the wall-clock cost is measured.** Note a constant 12 ms projectM analysis latency (`AudioBufferSamples = 576` at 48 kHz, `Audio/AudioConstants.hpp:8`) — an offset, not drift, so sync holds, but beat-reactive visuals sit behind the audio.
- [ ] **Overlays and karaoke cannot appear in recorded video at all** — the single most product-critical finding after the P0 above. `VideoView.qml` embeds the visualizer as a **native `QWindow` as a texture** (`WindowContainer`, `:18-23`) and paints `VisualizerOverlay` (`:25`) and `KaraokeMaster` (`:30`) as **QML siblings on top in the `QQuickWindow` scene**. The recorder captures inside the *native* `VisualizerWindow`'s GL context. The QML scene is never rendered there, so **no overlay text, no karaoke lyric, and none of `OverlayPanel`'s work can ever reach an encoded file** — 100% of the overlay system is invisible to 100% of the video output. **Decision 2026-09-26: post-process with FFmpeg, not GL compositing.** GL compositing is not merely harder, it is *structurally* impossible here: `ProjectM.cpp:170` forces DRAW to framebuffer 0, which is precisely the framebuffer Qt Quick's scene graph owns, so projectM and Quick would fight over the same draw target every frame (upstream's own `ToDo` confirms the design does not admit it). The `QQuickWindow::grabWindow` → upload idea is worse: a ~8 MB GPU→CPU→GPU round trip at 60 fps on top of a Quick render that cannot be paced deterministically, and it is not even WYSIWYG. The FFmpeg route is a pure function of (encoded file + ASS file) → output — deterministic, re-runnable, testable headlessly, and it makes the batch product *composable* (render → attach subtitles → optionally burn text; change the font and re-run a 2-second pass, not a 3-minute render). `libavfilter` must be added in **two** places: `src/recorder/FFmpegUtils.hpp:3-10` and `CHADVIS_FFMPEG_COMPONENTS` at `cmake/Dependencies.cmake:123`.
- [~] **No frame pacing between render fps and record fps** — *guardrail landed 2026-09-26, pacing itself still open.* `videoCodecCtx_->time_base = {1, video.fps}` and `pts = frameCount++` (`VideoRecorderFFmpeg.cpp:247,349-350`) with no drop/duplicate compensation, so a frame dropped by `MAX_QUEUE_SIZE` overflow (30) still silently changes playback speed. `startRecording` now refuses a zero or **odd** record resolution (chroma subsampling) and **warns loudly when `recording.video.fps != visualizer.fps`**, with `targetFps_` finally tracking the real render rate (`setTargetFps`, fed from `VisualizerWindow::setRenderRate` and seeded from config in `initialize`) instead of the hardcoded 60 it used to compare against. Still open: pacing, and the drop compensation.
- [x] **Likely upside-down recorded video (INFERRED, unverified)** — *the inference was right and the fix is in.* `captureAsync` applied no vertical flip, so the CPU `FrameGrabber::flipImage` — the correct implementation, and dead code behind a `useGPUFlip_` branch that was never reachable — was exactly what the capture path needed. It is now a public static called on every mapped PBO before the frame is emitted, and the row-order contract is pinned twice: three unit tests (`flipImageReversesRows`, `flipImageLeavesUndersizedBuffersAlone`, `flipImageIsItsOwnInverse`) and an integration test that compares the emitted frame against a raw `glReadPixels` of the *same* frame taken between the two renders the PBO pipeline needs — the readback's bottom row is the capture's top row, and the capture is not byte-identical to the raw readback. **Mutation-checked:** deleting the one-line `FrameGrabber::flipImage` call fails that test with "captured rows are the raw bottom-up readback, i.e. upside down" while the non-emptiness test still passes.
- [ ] **Batch automation is entirely absent** — no render queue, no job model, no "render all". `--headless` is worse than useless: it only skips the window (and thus the frame-capture wiring), so it makes recording *impossible*. `DownloadQueue` (`src/suno/DownloadQueue.hpp:80`, `setMaxConcurrent` at `:99`, already tested) is the directly reusable template for a `RenderJob` model. Ship the job model + queue first, the batch **GUI** last — a polished queue over a broken render loop is a trap.
- [ ] **Download is welded to playback** — `SunoDownloader.cpp:274-276` makes `processDownloadedFile` a one-line alias for `addAndPlay`, which appends and `jumpTo`s (`:248-272`). So fetching N Suno clips to disk *audibly plays them one after another*, and `SunoBridge` has no `downloadClip` at all — only `playClip`. Split "save to disk" from "enqueue for playback"; batch needs the former anyway.
- [ ] **Scene composition, keyframes, transitions, timeline: 0% built** — verified absent: zero hits for `keyframe` across all 150 sources; every `transition` hit is projectM's own unrelated soft-cut; every `timeline` hit is a Suno *mashup* field. The word "scene" is not a domain concept anywhere. **Ship "presets-as-scenes" (a preset per segment with a duration + crossfade) before real keyframes** — the hard part is the authoring UX and interpolation semantics, not the C++, and projectM's existing soft-cut (`Engine.hpp:110-132`) is a free, working crossfade primitive.
- [ ] **Karaoke burn-in path** — no `libass`, no `.ass` writer, no VTT, no `avfilter` anywhere in `src/`; `toSrt` (`LyricsData.cpp:488`) and `toLrc` (`:514`) are sidecar-only. Cheapest useful win first: **mux as a soft subtitle track** (muxer only, no libass) by adding a `toAss()` writer for per-word karaoke (`\k`/`\kf` from the timings that already exist in `LyricsData::words`, ~80 lines), then burn in with the `subtitles=` filter once avfilter is linked.
- [ ] **Scene-graph compositing is the one genuinely hard overlay option** — rendering overlays into the *same* GL context as projectM is architecturally cleaner (one surface, true WYSIWYG) but means driving Qt Quick into a foreign GL context. Deferred behind the FFmpeg route deliberately.
- [x] **Recorded video/audio settings persist** — see P0 for the re-verification; not part of this section.

### Audio engine
- [x] **Shuffle with Repeat::Off skipped a track** — *decided and fixed 2026-09-26.* Two separate causes, both pinned by tests: `next()` pre-incremented `shufflePosition_` even when nothing was selected, so the first entry of the permutation was stepped past (3 items, 2 visited), and `AudioEngine::play()` pinned the first selection to `jumpTo(0)`, which placed the traversal in the *middle* of its own permutation so the entry before it could never be played in that pass. **The decision:** a shuffle pass is a permutation walk that starts at its first entry and covers every track exactly once. `Playlist::startPlayback()` is the single named entry point for "begin playing" and picks the first entry of the traversal, `next()` only steps when something is already playing, and `addFile` now appends to the shuffle order like `addUrl` instead of splicing in at a random position — that splice used to move entries underneath `shufflePosition_`, desynchronising the traversal from the selection so a pass could play one track twice and skip another. A newly added track plays later in the current pass. Mutation-checked in both directions: re-introducing the pre-increment fails `nextExhaustsPredictablyUnderRepeatAndShuffle`, and 25 randomized passes of `shuffledPlaybackCoversEveryTrack` cover all three tracks.
- [x] **`spectrumUpdated` had zero consumers** — *deleted 2026-09-26, including the queue that fed it.* Verified: `VisualizerBridge` contains no reference to `AudioEngine` at all, and only `stateChanged`/`positionChanged`/`durationChanged`/`trackChanged` are connected. The engine ran a whole `JThread` analyzer worker, popped 2048-sample batches, ran a PFFFT analysis per batch, copied the result into `currentSpectrum_` and emitted a 1 KB `QAudioSpectrum` argument for no reader; `currentPCM()` had no callers either. Removing the worker's only consumer made the third `AudioQueue` queue (`ana`, with its own drop counter and per-chunk copy) a queue that could only fill and drop, so it is gone too — `pushAll` now feeds two queues. `AudioAnalyzer` itself is kept: it is a standalone component with its own unit test and a real consumer is a feature, not a cleanup.
- [x] **Skipping a track while paused started playback** — `onPlaylistCurrentChanged` called `play()` unconditionally, and that handler is bound to `currentChanged`, i.e. to *every* index change including user-initiated jumps and playlist edits while paused; `autoPlayNext_` guarded only the `EndOfMedia` path. Fixed with an explicit `autoAdvance_` permission armed by `onMediaStatusChanged` **before** it advances the queue, rather than by reading the transport state: Qt makes no promise about whether `playbackStateChanged(StoppedState)` arrives before `mediaStatusChanged(EndOfMedia)`, so inferring the intent from `state_` would make gapless advance depend on that ordering. Three tests in `test_Playlist.cpp` with real generated WAV files: a skip while paused stays paused (**mutation-checked** — restoring the unconditional `play()` fails exactly that one), a skip while playing keeps playing, and a real track playing to its end advances into the next track. The last one **skips on this machine** with a stated reason: the audio backend reports `Playing` but the playhead never advances, so `EndOfMedia` cannot be produced here and the auto-advance path is verified by construction, not by observation.
- [~] **`setSource()` runs inside the `mediaStatusChanged` emission** — *confirmed real, but the obvious fix is worse than the bug; do not queue a hop.* `onMediaStatusChanged` (`:122-132`) → `playlist_.next()` (`:127`) → `currentChanged` → `loadCurrentTrack` (`:154`) → `player_->setSource()` (`:158`), synchronously inside the emit. **Evidence recorded 2026-09-26:** in the normal gapless path this is usually a *no-op*, because `prepareNextTrack` (`:162-170`) already set the next URL on `nextPlayer_` before `swapPlayers` (`:144-152`) promoted it. The hazard only fires on the non-pre-buffered branch at `:128`. Queueing the hop with `Qt::QueuedConnection` would demote an already-decoded pre-buffer that *has data ready* until the next event-loop turn — trading a source-less teardown for a **guaranteed gap on every track transition**, which is the main quality feature of a music player. **Still open:** whether Qt documents the prohibition (the locally installed Qt 6.11.1 headers are comment-stripped, so the claim could be neither confirmed nor refuted offline — verify upstream before citing it), and whether a non-queued fix exists (defer only the *reconfiguration*, not the swap). No test drives a track transition today; `QtMultimediaTestLib` is present in the install, and only call *ordering* is assertable headless (no audio device in CI), so per `AGENTS.md` §3 this needs a manual listening pass regardless.
- [ ] **projectM's broken-preset retry path is dead** — `Bridge.cpp:53-54` calls `projectm_set_preset_switch_requested_event_callback`, which **overrides** projectM's own handler (documented at `build/_deps/projectm-src/src/playlist/api/projectM-4/playlist_callbacks.h:88-91`; single-slot, last-writer-wins). That kills `PlaylistCWrapper::OnPresetSwitchFailed`'s 5-retry recovery loop (`PlaylistCWrapper.cpp:74-103`), so a broken preset now aborts the chain permanently instead of retrying. ChadVis never registers `m_presetSwitchFailedEventCallback` either, so even if it ran it would go nowhere. Behaviour is preserved *by accident* on the happy path via a `pending*` latch drained on the next frame (`Bridge.cpp:150-153`, `:132-134`), which also costs a real ~16 ms switch delay. Behavioural regression vs stock projectM, not a safety bug.
- [x] **projectM playlist re-entrancy (investigated, closed as invalid)** — the suspected `OnPresetSwitchRequested` → `itemAt()` re-entry was **already prevented** by the `pending*` latch: `presetSwitchRequested` sets `pendingNext_` and does not touch `playlist_`. The single real re-entry (`Bridge.cpp:196` → `projectm_playlist_item`) is a bounds-checked **read** of a container that projectM is not iterating and does not mutate at that point (`PlaylistCWrapper.cpp:142-154`), and the documented infinite-recursion hazard is already handled by the `syncingFromNative_` guard at `:202`/`:177`. No fix wanted. Also refuted: `scanPresets` holds **no** `loadMutex_` (`loadMutex_` guards only `pendingLoadPath_` at `Bridge.cpp:120,190`) — its real cost is a synchronous GUI-thread scan in `init()`, deferred as below.
- [ ] **`pm::Bridge` startup preset scan is still synchronous** — `Bridge.cpp:98` runs a full `presetManager_.scan()` on the GUI thread inside `init()`. `Application.cpp:391` was already converted to `scanAsync` by `f121901`, but this instance cannot: `init()` reads `presetManager_.empty()` at `:69` and then `selectByIndex(0)`/`selectByName` at `:71,:75` before returning, and gates native-playlist population on the same pass at `:103-108`. An async scan would silently skip first-preset selection. Needs the selection logic deferred to the publish callback — a behaviour change, so a separate item.
- [ ] **Triple queue redundancy** — three queues hold the same PCM data (3× memory). Consolidate to one ring buffer with multiple consumers.
- [ ] **Naive beat detection** — energy-ratio approach from the 1990s; implement onset detection or spectral flux.
- [ ] **No FFT windowing function** — rectangular window causes spectral leakage; apply Hann/Hamming before the FFT.
- [ ] **No true gapless playback** — `swapPlayers()` introduces an audible gap; pre-buffer and crossfade.
- [ ] **No sample format validation** — audio output format is never checked against the source.
- [ ] **Analyzer worker busy-wait** — fixed `QThread::msleep()` spin; use a wait condition or event-driven wake.
- [ ] **No error recovery in `loadLastPlaylist`** — a missing or corrupt file yields a silent empty playlist.
- [ ] **Playback filetypes limited** — MP3/FLAC/WAV only; add OGG, M4A, OPUS via taglib or FFmpeg.
- [ ] **Album art limited to MP3/FLAC** — no WAV, OGG, or M4A cover extraction.

### Application architecture
- [ ] **Application god object** — 150+ line `init()` owns everything; split into subsystem managers.
- [ ] **`g_app` raw pointer** — should be `unique_ptr` or stack-allocated; risk of double-delete or leak.
- [ ] **CLI X-macro pattern fragile** — poor IDE support; consider codegen or reflection.
- [ ] **TRY macro shadows `std::expected`** — migrate to the C++23 monadic idiom.
- [ ] **`Result.hpp` → `std::expected`** — the custom type has `map`/`andThen`/`orElse`; migrate to the full standard monadic API and add `[[nodiscard]]` to Result-returning functions.

### Namespace, types, and dead code
- [ ] **Controllers in the wrong namespace** — some use `vc` instead of `vc::ui`.
- [ ] **Inconsistent namespace usage** — `vc::ui`, `vc`, and top-level all appear; standardize.
- [ ] **Missing `<cmath>` include in `LyricsRenderer`** — uses `std::sin`/`std::cos` without it.
- [ ] **Dangling pointers from `getContextLines`/`getUpcomingLines`** — the maps were made owned, so verify no raw-pointer variant survives; the original finding is partly resolved.
- [x] **`PlaylistItem::valid` never read** — field is gone; `struct PlaylistItem` in `src/audio/Playlist.hpp:14-22` carries no `valid` member.
- [x] **`PresetBridge::cachedPresets_` never used** — removed; `grep -rn cachedPresets_ src/` returns 0 hits.
- [ ] **`OverlayElementConfig` animation fields unused** — declared, never applied.
- [ ] **Unused includes** — five or more files include types they never reference.
- [x] **Stale-header and unused-declaration purge** — `GLIncludes.hpp` no longer exists in the tree, `setupStyle()`/`setupQmlStyle()` are gone (0 hits repo-wide), `CircularBuffer::getSpans()` is gone, and `AudioEngine.hpp` carries no `projectM.h` include.

### Build system
- [ ] **Default build type is Debug** — should default to `ReleaseWithDebInfo` for distribution.
- [ ] **No release pipeline** — no CI/CD for tagged releases; only `opencode.yml` exists. Add GitHub Actions for build, package, and publish. (The README previously carried a CI badge for a workflow that does not exist; it has been removed.)
- [ ] **CPM.cmake downloaded at configure time** — a network fetch during build; vendor it or use `FetchContent`.
- [ ] **CPM target-name guessing fragile** — use `CPMFindPackage` with explicit targets.
- [ ] **All sources in one `CMakeLists.txt`** — split into per-module `add_subdirectory` trees.
- [ ] **`PKGBUILD` missing deps / wrong description**.

---

## P2 — Medium: performance, code quality, maintainability

### Performance
- [ ] `std::rand()` instead of `<random>` — not thread-safe, poor distribution.
- [ ] **`Signal::emitSignal` copies every argument by value** — found 2026-09-26. `Signal::emitSignal(Args... args)` takes by value, so `frameCaptured.emitSignal(std::move(buffer), ...)` moves into the parameter and then *copies* the whole frame again into each slot's parameter: one full-frame memcpy per subscriber, per frame, at 60 fps — 8 MB per frame at 1920x1080, on the GUI thread, immediately after a GPU readback that already cost the same again. Perfect forwarding (`Args&&...` + `std::forward`) fixes it for every `Signal` user in the tree.
- [ ] `PresetBridge` rebuilds `QVariantList`s on every access — use `QAbstractListModel`.
- [ ] `PresetPanel` resets the whole model on a single change — use `beginInsertRows`/`beginRemoveRows`.
- [ ] `LyricsData::search()` allocates on every call — return a view or cache.
- [ ] `flipImageGPU()` allocates textures/FBOs per frame — pool them.
- [ ] `AudioAnalyzer::pcmData()` returns a full copy per frame — return a span.
- [ ] `threadLoop()` spin-waits at ~100 fps idle — use a condition variable.
- [ ] `parseDuration()` uses `std::regex` for trivial parsing.
- [ ] `AudioSpectrum` passed by value in a signal — 4 KB copied per emission.
- [ ] `AudioFrame alignas(64)` wastes 52 bytes per ring-buffer entry.
- [ ] `MediaMetadata::formatLine` does repeated `QString::replace()`.
- [ ] No meaningful `constexpr` usage.
- [ ] `downloadAudio()` always writes `.mp3` — detect from content-type.
- [ ] `onWavConversionReady()` redundant if/else.
- [ ] `AudioEngine::analyzerWorker` and `VisualizerRenderer::initialized_` never reset.

### Code organization
- [ ] **Dead subsystems audit, re-verified 2026-09-28** — *found by a full-tree sweep.* Each of these is a declaration or a duplicate with no consumer; together they are most of a day's cheap cleanup and they hide real features from QML. `VisualizerWindow::feedAudio` (`VisualizerWindow.cpp:198`, zero callers — the renderer pulls PCM from `AudioQueue::popViz` instead); `VisualizerWindow::frameReady` (`:33`, declared and never emitted); `LyricsSync::eventOccurred` (emitted at `LyricsSync.cpp:237,243`, zero `.connect()`); `Playlist::itemAdded`/`itemRemoved` (emitted `Playlist.cpp:96,117`, zero connects); `Signal::hasConnections`/`connectionCount` (`Signal.hpp:102,107`); `FrameGrabber::queueSize` (`FrameGrabber.cpp:54`); `EncoderSettings::VideoSettings::twoPass`/`extraOptions` (`EncoderSettings.hpp:83,88`); and `ThemeBridge` (60+ color properties, deliberately unregistered at `BridgeRegistration.cpp:34-37`, and architecturally out of sync with the QML `Theme.qml` singleton, which has no persistence path). Unreferenced resources: `components/ComingSoonPage.qml`, `icons/qml/lyrics.svg`, `icons/qml/recording.svg`. Keep the 8 `EncoderSettings` static presets (`EncoderSettings.hpp:145-152`) — they are a finished UI feature, not dead code; see the proposals section.
- [ ] **Unused includes, 7 sites** — found 2026-09-28. `src/audio/AudioEngine.cpp:2` (`core/Config.hpp`), `:6` (`<QAudioDevice>`), `:7` (`<QMediaDevices>`); `src/lyrics/LyricsSync.hpp:17` (`<functional>`), `:18` (`<deque>`); `src/core/Application.cpp:19` (`<QDir>`), `:20` (`<QFile>`), `:21` (`<QFontDatabase>`). Mechanical, and a cheap way to prove include-what-you-use adoption.
- [ ] **`SunoBridge` exposes 41 properties and zero signals, and QML uses 10 of `PlaylistBridge`'s 14 members** — found 2026-09-28. Bridge surfaces have grown by accretion with no owner pruning them. Not a bug, but every unconsumed property is a maintenance cost and a lie to a future reader. Note the one real style inconsistency found: `LyricsPanel.qml:54` calls `LyricsBridge.setSearchQuery` as a method when it is declared as a property `WRITE` setter — it resolves, and it is the only such call in the tree.
- [ ] **`AVFormatContextDeleter` is write-side only and segfaults a reader** — found 2026-09-26. It reads `c->oformat->flags`, and a demuxer's `oformat` is null, so `AVFormatContextPtr` is unusable for reading a file: the first attempt to decode a recording in a test crashed on `0x2c`. Split it into an input deleter (`avformat_close_input`) and an output one, or make one that checks `oformat` before dereferencing it. This blocks any test that verifies an encoded file, so it is a prerequisite for the muxing and post-render proposals below.
- [ ] `VideoRecorder.hpp` is a pointless facade — inline or remove.
- [ ] `PlaylistBridge` takes the address of a reference parameter — possible dangling pointer.
- [ ] `loadM3U` appends rather than replaces — should clear first.
- [ ] SQLite FTS5 not enabled — add a virtual table for lyrics and clip search.
- [ ] `PresetPersistence`/`RatingManager` lack atomic writes — a crash corrupts the file.
- [ ] `sanitizeFilename()` incomplete — no Unicode, reserved names, or path separators.
- [ ] LRC metadata tags (`[ar:]`, `[al:]`) not parsed.
- [ ] `CliUtils::findClosestMatch` truncated; `CliArg` stringly typed.
- [ ] `isatty()` not portable.
- [ ] `Color::fromHex` uses allocating `std::stoi` — use `std::from_chars`; and its home in `FileUtils` belongs in a `Color.hpp`.
- [ ] Duration has several representations — unify behind a strong typedef.
- [ ] `Application::printHelp` is 80+ hardcoded lines.
- [ ] `SunoBridge::onLibraryUpdated()` hand-builds a map that a shared converter could build.
- [ ] `#pragma once` on `.inc` files; missing include guards in `CliArgs.inc` and friends.
- [ ] `PKGBUILD` hardcoded path removed — `output_directory` now uses `~/Videos/ChadVis`.

### QML / UI
- [ ] **Fullscreen shortcut `F` is not wired** — `main.qml` reveals the Video page and logs a TODO instead of toggling fullscreen. `VisualizerWindow::toggleFullscreen` is a public slot that already tracks `fullscreen_`/`normalGeometry_`, so the machinery exists and is unwired.
- [ ] **F11 declared but not wired.** `KeyboardConfig` declares `toggleFullscreen: "F"`; `main.qml:424` is the single `TODO`/`FIXME` in the entire non-Suno source tree.
- [ ] **QML hardcodes two different version strings, neither from `version.txt`** — found 2026-09-28, and this is a **regression against a closed item**. TODO P3 claims the banner consumes `CHADVIS_VERSION` and that `version.txt` is the single source of truth; that is true in C++ (`test_Version.cpp` pins the CLI banner and `--version` to `version.txt`) but the QML layer was never converted. `main.qml:270` renders `"v2.0.0 · Suno Desktop"` and `NavRail.qml:259` renders `"v2.0"`, while the real version is `1.1.0`. Neither is wired to the build, so a release bump updates `version.txt` and both QML strings go stale silently. Expose the version through a registered bridge property rather than a literal.
- [ ] **No internationalization, and there is no infrastructure at all** — the P2 item understates this. There are **zero `qsTr` calls in all 56 QML files**; every user-visible string is a raw literal. A full-tree sweep enumerated ~200 such literals by file and line (worst offenders: `OverlayPanel.qml` 14, `AccountSessionCard.qml` 10, `LyricsPanel.qml` 9). Also 7 sites use emoji as a label glyph (`LyricsPanel.qml:34`, `ClipCard.qml:90,149`, `PlaybackPanel.qml:86`, `KaraokeMaster.qml:182`, `ClipDetailSheet.qml:241`), which needs different treatment from translatable text. Note `Logger.hpp` uses fmt-style `{}` placeholders throughout, so the logging vocabulary and `qsTr` need one shared answer decided up front. A partial job is worse than none, so do this after the UI stops moving.
- [ ] **Color picker present but unimplemented.**
- [ ] **Settings panel reuses the playback icon** for a different action.
- [ ] **Theme.qml missing `textPrimaryVariant`** — a real contrast bug, not just a completeness gap; fix it before claiming any accessibility work.
- [ ] **"Show in Folder" unreliable on Linux** — `xdg-open` handling breaks on some desktops.
- [ ] **Structured logging** — add a JSON sink for agent and log-aggregation parsing.
- [ ] ~~`ThemeBridge` writable colors~~ — moot: `ThemeBridge` is deliberately not registered as a QML singleton, because it would shadow the QML `Theme` singleton.
- [x] Record button highlight corrected against `RecordingBridge::isRecording`.
- [x] `OverlayBridge` saves with a 2 s debounce and flushes on destruction.
- [x] `SettingsPanel.qml` split from 526 LOC into 8 per-category panels.

### Tooling
- [ ] `.clang-tidy` variable-naming rules do not match project convention — false positives.
- [ ] **`.clang-format` is unusable as a gate** — measured 2026-09-26: it wants `BraceWrapping.AfterFunction: true` (Allman) while the whole tree uses attached braces, so `clang-format --dry-run -Werror` reports violations in *untouched* files (13 in `core/Logger.cpp`, 54 in `visualizer/VisualizerWindow.cpp`, 99 in `recorder/VideoRecorderCore.cpp`). Either regenerate it from the real style or delete it; a config that flags the entire repository is a config nobody runs.
- [ ] `.clang-tidy` missing `modernize`, `bugprone`, and concurrency checks — enable incrementally.
- [ ] `.clangd` config is minimal — missing compilation-database hints and header search paths.
- [ ] **Four config files are inert and misleading** — found 2026-09-28. Nothing in the repo, the build, or CI invokes `.clang-format` or `.clang-tidy`; `.prettierignore` has no prettier config behind it and still lists `sst-env.d.ts`, which does not exist (leftover from another project); `.github/dependabot.yml` has `package-ecosystem: ""`, `directory: "/"`, and `interval: "manual"`, so it is a template that will never run. Either wire them or delete them — an inert config reads as coverage and delivers none.
- [ ] **`docs/dev/TESTING.md` documented a pre-fix state as current** — *found 2026-09-28, and being corrected.* It claimed 8 ctest suites (there are 9; `integration_gl_tests` was undocumented entirely), claimed `test_PresetScanner` had no `runTest*` entry point, and claimed `test_SunoEndpoints` ran at static-initialization time — all three contradicted by `tests/unit/test_main.cpp:10,11,33`. That is the doc which owns test inventory asserting the exact bug `AGENTS.md` §3 warns about. **Lesson worth keeping:** a doc that describes a *bug* as current state is the highest-risk stale documentation, because a future agent reading it will re-diagnose a fixed defect.
- [ ] **QML module and resource drift** — found 2026-09-28. `QML_SOURCES` (`cmake/Sources.cmake:188-247`) and the on-disk set are in sync at 56/56, but `src/qml/shaders/shadow.frag` is not declared, `icons/qml/lyrics.svg` and `recording.svg` are declared but unreferenced, `panels/PlaylistPanel.qml:15` uses the technology-preview `Qt.labs.platform` while the rest of the tree uses `QtQuick.Dialogs`, and 3 of the 7 nav entries (`NavRail.qml:20-22`) all render `suno.svg`.
- [ ] **Dependency reproducibility** — `cmake/Dependencies.cmake` pins 6 of 15 dependencies; the other 9 are unpinned `find_package`/`pkg_check_modules` system lookups, so builds are non-reproducible across machines by design. `pffft` and `moodycamel/readerwriterqueue` bypass the system-first fallback every other header-only dep follows, and `CPM.cmake` itself is `file(DOWNLOAD)`-ed unconditionally at `:27-29` even when no fallback is needed.
- [ ] **Orphaned files, verified 2026-09-28** — `scripts/build-fast.sh` (no doc, script, or README reference), `skills/cpp-coding-standards/SKILL.md` (referenced by nothing), `.agent/CODEBASE_EXPLORATION_PROMPT.md` (0 references), two `.DS_Store` files, and a 41.5 KB `build.log` sitting in the repo root.
- [ ] `CMakeLists.txt` still carries multiple `// TODO` comments.
- [x] `docs/suno_api/README.md` carries the unofficial/support disclaimer, authority boundary, evidence labels, and secret-handling rules.

---

## P3 — Low: polish and future-proofing

- [ ] TOML-based "Chad Config" — expose every UI constant and engine parameter.
- [ ] Profile support — save and load UI themes and visualizer preset banks.
- [ ] qss themes and a switching UI section; custom user themes auto-populated; live theme reload instead of restart-required.
- [ ] "Modern Visualizer Overlay" with reactive text and graphics.
- [ ] "Karaoke Master" mode with custom aesthetic overrides.
- [ ] `std::mdspan` for FFT; concepts/constraints on unconstrained templates; `std::array` for `CircularBuffer`; configurable shuffle seed for deterministic tests; `std::variant` for lyric sources and CLI args.
- [x] `Application::printVersion()` hardcoded `1.0.0` while `version.txt` is the source of truth — the banner now consumes the `CHADVIS_VERSION` build definition (forwarded from `version.txt` to `project_lib` and the executable) via `vc::Cli::versionBanner()`, and `tests/unit/core/test_Version.cpp` pins both the banner text and the real binary's `--version` output to `version.txt`.
- [ ] `PresetScanner` categories default to "Uncategorized" — infer from directory structure.
- [ ] README humor still displaces information, though Quick Start now carries the real build and test commands.
- [x] Karaoke settings persistence — `[karaoke]` is parsed and serialized.
- [x] Root `CHANGELOG.md` is canonical with the legacy archive in `docs/CHANGELOG_LEGACY.md`. *Note 2026-09-28: the legacy file still carries two different `1.1.0` entries (2026-09-26 and 2026-01-28) and both changelogs acknowledge the overlap without resolving it. This `[x]` also absorbed the duplicate `docs/suno_api/README.md` item that stood here separately; the surviving copy is the fuller one under Tooling, per the one-fact-one-owner rule.*

---

## P4 — Features and architecture

- [ ] **DI over singletons** — replace global singletons with dependency injection.
- [ ] **Separate audio processing from UI** — make the audio engine a headless library consumed via bridges.
- [ ] **`std::execution`/`std::jthread`** — C++23 parallel algorithms and join-aware threads in the audio pipeline.
- [ ] **QML bridge consolidation** — many bridge singletons into one namespaced backend interface.
- [ ] **Build system improvements** — per-module CMake subdirs, vendored CPM, optional Conan/vcpkg.
- [ ] **Audio mastering workstation pathway (JUCE under evaluation)** — P7 defers lightweight-DAW features until the export pipeline is stable. JUCE is the leading candidate for the audio engine because it supplies multi-stop `AudioFormatReader`/`Writer`, MIDI, and device-agnostic realtime I/O that would otherwise be hand-rolled. The `origin/experiments/juce-refactor` branch is retained deliberately for this reason, but none of its 2026-02 code is portable. See the considered-pathways note in `docs/PIVOT_PLAN.md`; revisit only after P5 and P6 conclude.
- [x] Suno library upgraded to `feed/v3` with infinite-scroll pagination.
- [x] Accordion height animations; expanded settings with engine and recorder controls; persistent state for all toggles and view modes.

---

## P5 — Testing and QA

- [ ] **Authorized-account smoke** — Library, Create, Listen, Explore, Notifications, Video, and Settings against a real session. The only item from the 2026-09-24 verification pass still open.
- [ ] **Capture-backed contract fixtures** — fake-request tests for account, billing, and limit behavior.
- [ ] **Health-check tests for agentic workflows** — fast signal for development cycles.
- [x] `ctest --test-dir build/tests --output-on-failure` runs all 9 registered suites. Note that `--test-dir build` discovers zero tests and still exits 0. *2026-09-26: found a second, quieter version of the same trap — `test_SunoEndpoints` defined `runTestSunoEndpoints` but `test_main.cpp` never declared or called it, so the fail-closed host-allowlist test compiled and never ran. It had been masked by a static initializer that called `std::abort()` on failure. Both are now called from `main()`. Lesson: a suite being in the CMake source list is not evidence it runs.*
- [x] Offscreen integration test loads the real QML module, requires a visible/exposed `QQuickWindow`, and verifies a non-null grabbed frame.
- [x] Config parsing and audio analysis have unit coverage.

---

## Codebase audit (2026-04-28) — remaining

Full audit of 19,294 LOC across 10 modules: 24 issues found, 18 fixed across five phases, net −894 LOC (38 files changed).

- [ ] **#14** `OverlayBridge` uses separate JSON persistence instead of the config system — deliberate, since JSON suits list data. Revisit for debouncing.
- [x] **#1/#12** lyrics unification; **#2** `SunoDatabase::clipFromQuery`; **#3** `SettingsBridge` X-macro table; **#4** broken QML theme references; **#5/#13** CLI argument table; **#6/#23** lyrics dedup; **#7/#8** download helpers; **#9/#22** format helpers; **#10** `VisualizerBridge` stubs; **#11** namespace migration; **#15** `vc::lerp()`; **#16** orphaned license block; **#17** duplicate include; **#18** duplicate GL state; **#19** archived `LyricsLoader.hpp`; **#21** orphaned forward declaration; **#24** stale CMake variable; **#25** native visualizer toggle; **#26** LyricsBridge SRT/LRC export and search.

---

## Capture-gated — ship gates, not blockers

Every item here is **actionable engineering**, not a wait. Each states the capture
that would upgrade it *and* the fail-closed behaviour the code ships in the
meantime, so the work is never parked. Do not read a `[?]` here as "cannot
proceed"; read it as "proceed, and leave the switch off".

- [~] **Native sign-in loopback** — every observed callback and final redirect in the current captures is Suno-owned HTTPS. **Now much sharper after the 2026-08-25 capture:** `POST /v1/client/sign_ins` is directly observed carrying `redirect_url=https://suno.com/sso-callback?auth_mode=sign-in`, and `GET /v1/client/handshake` is directly observed taking an arbitrary **absolute** `redirect_url` and appending a one-shot `__clerk_handshake` nonce JWT to it. That reduces the open question to one specific thing: *does Clerk validate `redirect_url`, or will it honour a `http://127.0.0.1:<port>` target?* Until a capture answers it, the native path stays correctly disabled. Meanwhile the **offline** scaffold is real work with no capture dependency: `AuthCoordinator::beginGoogleSignIn` still refuses because `launch_` is `std::nullopt` (`AuthCoordinator.cpp:110-114`) and `installCredentials_` is never populated (`AuthCoordinator.cpp:24-36`), so even a successful callback is a no-op (`AuthCoordinator.cpp:80-82`). Close those two gaps behind the gate so that flipping the gate later is a one-line change, not a feature. The gate itself is owned solely by [`docs/suno_api/OAUTH_REDIRECT_ANALYSIS.md`](docs/suno_api/OAUTH_REDIRECT_ANALYSIS.md) and must not be restated elsewhere.
- [ ] **Clerk route selection** — `POST .../tokens` and `POST .../touch` are both directly observed; requiredness and preference order are not established. Still true, and the 2026-08-25 capture does **not** settle it: that capture exercised `touch` (empty form body, matching the inventory's 2026-08-25 variant) and never called `tokens`. Both remain `[T1]`. Ship `touch`-first with a 401-retry fallback and keep the preference configurable, rather than picking a winner on inference. This is the largest `[T1]`-without-code gap in the inventory.
- [ ] **Server feed search** — no reviewed request contains `searchText`; use local filtering. The local filter already exists and is the shipped behaviour, so this is a watch item, not a gap.
- [ ] **Following-feed pagination** — the first page is observed, but no next-page token or cursor was captured. There is currently no following-feed surface in the client at all, so nothing is blocked; `SOCIAL_FOLLOWING_FEED` is declared-unused and correctly so. Build the surface only against a captured cursor.
- [ ] **Orpheus/B-Side/VIP/hidden features** — remain disabled unless a new direct capture and a product decision promote them. The engineering that must exist regardless: the fail-closed host guard already refuses the Modal host, so keep it, and keep `ORCHESTRATOR_CHAT`/`ORCHESTRATOR_HISTORY` declared-unused rather than deleting them.
- [ ] **Fresh captures** — playlist mutation, error and rate-limit envelopes, direct generation drift, upload status and clip initialization, and other `[LEAD]` routes. Add error/rate-limit envelope handling now: it is a parser and a UI state, both of which we need for every route regardless of which routes exist.
- [ ] **Orpheus model-name claims** — retired; `chirp-v4` and `chirp-auk` appear only in a deleted fork. Captured evidence shows `chirp-v3.5`. No action; kept so the retirement is not silently undone.

---

## Proposals — NOT USER APPROVED

> **Nothing in this section is approved, scheduled, or promised.** These are
> ideas gathered from a full exploration of the tree on 2026-09-28, grounded in
> real files and in the facts recovered from the 2026-08-25 capture. They are
> recorded so the thinking is not lost, **not** so that anyone can pick one up
> and start building. Only the user promotes an item out of this section into a
> real priority tier, and only then does it become backlog.
>
> The one hard rule that survives promotion: a proposal may not be built on a
> `[LEAD]`-only or unverified Suno contract. Items that need a capture say so.

### Highest leverage first

1. **Register `SunoWorkspaceBridge` and build the music-video UI on it.** This is not a
   proposal so much as a correction — see the `[!]` item in P1. It is listed here because
   the *feature set* it unlocks (regions, stems, render progress, workspace save/load) is
   the product, and nobody has ever seen it run.
2. **Download is not play.** `SunoDownloader::processDownloadedFile` is a one-line alias
   for `addAndPlay`, so fetching N clips audibly plays them one after another, and
   `SunoBridge` has no `downloadClip` at all. Splitting the two is small, fixes a real
   behaviour, and is the input side of every batch feature below.
3. **Frame pacing from the timestamps already in hand.** Covered as a P0 item above
   because the data is already captured and discarded. Cheap, and everything downstream
   (batch, templates, concat) inherits a correct timeline instead of compounding drift.
4. **A render job model and queue.** `DownloadQueue` is an already-tested template for
   exactly this machine (bounded concurrency, backoff, atomic `.part` rename, cancel,
   idle signal), and `SunoWorkspace` already models `renderMode`/`renderDuration` with
   `renderProgress`/`renderCompleted` signals that currently have no producer. The product
   goal is a *batch* creator; there is no job model at all today.
5. **A build-and-test CI.** The repo has no build CI whatsoever — `.github/workflows/`
   contains only a comment-triggered agent workflow. Nine ctest entries are already
   registered. This is the only item that makes every other item cheaper to verify, and
   it is small. One caveat: the GL suite needs the native QPA plugin on a real or
   software GL stack, or the entry is decorative.

### Music video creator

- **Karaoke as a muxed subtitle track before burn-in.** `LyricsData::words` already
  carries per-word `startTime`/`endTime` from the captured aligned payload, so a `toAss()`
  writer emitting `\kf` centisecond durations is the bulk of the work, and `libavformat` is
  already linked. Muxing gives real, selectable, searchable lyrics in any player, and makes
  burn-in an opt-in second pass. *Risk:* the subtitle codec is container-chosen — WebM has
  no subtitle stream, so that output must drop the track and say so rather than fail.
  Depends on fixing `AVFormatContextDeleter`, which segfaults on any read.
- **A `--render` headless mode.** Today `--headless` skips the window *and* the
  `frameCaptured` wiring, so it makes recording impossible rather than merely silent. A
  real render mode makes the video pipeline scriptable and CI-testable. *Honest cost
  note:* the window plumbing is the easy half; the real work is decoding input audio
  without `QMediaPlayer`, which does not exist yet.
- **Encoder preset gallery, including vertical profiles.** The eight `EncoderSettings`
  static presets already exist with **zero callers** — dead code that is obviously a UI
  feature. 9:16 / 4:5 / 1:1 are the interesting additions. *Risk:* projectM's aspect
  correction has never been exercised at 9:16, so the first vertical encode is where you
  find out whether it letterboxes or squashes.
- **Poster-frame export.** `RenderTarget` already reads pixels out of framebuffer 0 and
  `FrameGrabber::flipImage` is the exact row-flip a `QImage` needs. Small, and every upload
  form wants a thumbnail. *Risk:* picking the *best* frame is a product decision; ship
  "frame at N seconds" and do not pretend it is scene detection.
- **Presets-as-scenes, shipped before real keyframes.** The existing "ship presets-as-
  scenes before keyframes" call stands, with one sharpening: projectM's soft-cut is already
  plumbed and is a working crossfade, and `SunoMetadata` already carries `bpm` and `key`.
  The cost is the authoring UX and the interpolation semantics, not the C++.
- **Cut on real downbeats.** `POST /api/gen/{id}/downbeats_streaming/v2` is captured and
  declared-unused, so beat-accurate cuts have a *server-computed* source already available.
  Prefer it over a local detector, and cache per clip so a downloaded MP3 with no clip id
  still works. Note this shares a boundary with the `downbeats_streaming` capture, which
  is currently `[T1]`; the availability and rate-limit behaviour are not established.
- **An FFmpeg post-render pass: burn-in, loudness, concat.** The route is already decided
  and for a hard reason (projectM binds DRAW to framebuffer 0, which is exactly the
  framebuffer Qt Quick owns, so GL compositing is structurally impossible). *Cost is
  dominated by packaging, not code:* `libavfilter` and `libass` must be added to
  `Dependencies.cmake` **and** `FFmpegUtils.hpp`, and a stripped distro must not stop the
  app from launching. Ship behind a CMake option that degrades to "no post pass".

### Suno client

- **Read your own plan, handle, and device id out of the bearer.** `JwtUtils` already
  decodes claims and already tail-matches slash-prefixed vendor claims, so `suno/handle`,
  `suno/username`, `plan`, and `suno/did` are available from a token the client already
  holds, with zero extra requests. *Do not* silently swap the token's `suno/did` for the
  locally generated `device_id` config value — the header requirement is captured for the
  value in use today, and changing it invalidates that policy. Show `did` as diagnostic only.
- **Loopback sign-in via the captured handshake, not `sign_ins`.** The handshake takes an
  arbitrary absolute `redirect_url` and 302s back to it with a one-shot nonce, and the
  entire receiving half already exists and is tested (`LoopbackListener`, `OAuthTransaction`,
  constant-time state comparison). `AuthCoordinator` currently hardwires Google; that
  becomes a provider list. *This is still behind the capture gate* — see
  [`docs/suno_api/OAUTH_REDIRECT_ANALYSIS.md`](docs/suno_api/OAUTH_REDIRECT_ANALYSIS.md),
  which is the only place that gate may be stated.
- **Generation via browser hand-off, then import from the captured library.** The Create
  page authors a prompt against the real captured catalog, opens suno.com with it
  pre-filled, and the client picks the finished clip up from `feed/v3` and hydrates it in
  bulk via the captured `clips/get_songs_by_ids` — which is `[T1]`, `declared-unused`, and
  a ready-made import primitive. **This is a product and terms-of-service decision, not a
  technical one**, and it needs a human before any code is written. Nothing in this
  section should be read as "solve the captcha".
- **Realtime push instead of polling.** The `x-ably-token` claim is on the bearer and
  `SunoRealtimeDiscover` is already modelled with `stream_url`/`jwt_header_param`, so the
  model was written for this. **This is the one proposal that could violate the
  fail-closed rule:** the stream host is a *response value* from a captured route, not a
  captured request host, and the protocol is in no capture. Do not send the bearer to it
  without a direct capture. Until then it is research, not a feature — and its value is
  low until generation lands.
- **Per-track preset matching.** `bpm` and `key` are already captured on `SunoMetadata`,
  and the preset hand-off is already deferred safely to the next frame drain. *Honest
  limit:* those fields are absent on locally imported files, so the feature must degrade
  to "unchanged" rather than guess.
- **Preset banks with export/import.** Ratings, favourites, blacklist and categories are
  all modelled already. The blocker is that `RatingManager` is a process-wide singleton,
  which must be un-singletoned before two banks can exist at once. Decide early whether a
  bank is a curated list (portable, empty elsewhere) or a bundled archive (portable, huge);
  the first is right and the second is a much larger feature.

### Visualizer, platform, and quality of life

- **A `--diagnostics` report.** Every number already exists: encoder fallback warnings,
  queue depths, drop counters, preset counts, the GL renderer string, and a redaction
  helper. *Secret handling is the entire risk* — the config holds token and cookie fields,
  so the collector needs an explicit allowlist rather than a deny-list, and it must be
  written to a path the user chooses, never uploaded.
- **Keyboard and screen-reader accessibility.** The `F`/F11 fullscreen machinery exists
  and is unwired; the shortcuts page already exists to display bindings; `AppButton` and
  friends are centralized so `Accessible.name` is one line per component rather than per
  use. Fix the `Theme.qml` `textPrimaryVariant` contrast bug *before* claiming
  accessibility work.
- **Localization.** Kept as a proposal rather than a commitment because a partial job is
  worse than none: half-translated UI is more annoying than untranslated. It also touches
  ~50 QML files and the logging vocabulary, and every string churned afterwards is a
  re-translation. Do it after the UI stops moving.
- **Apple Sign In via `AuthenticationServices`.** The environment capture shows
  `oauth_token_apple` is an enabled strategy, and on macOS the framework can produce that
  credential natively with no browser round trip. This is a design decision enabled by
  evidence, not evidence of a design — which is why it sits here and not in P1.

---

## Roadmap order

1. Scrub committed PII from git history and rotate the exposed account identifiers.
2. Authorized-account smoke across all seven surfaces.
3. Library, account, and media correctness against that session.
4. Captcha-backed generation and the bounded upload lifecycle.
5. End-to-end aligned-lyrics karaoke.
6. Recording review, deterministic export, scene/keyframes, batch automation.

## Verification bar

A task is not complete from a stale binary, a declaration, a scan string, or a
historical commit. Verify the current source with a fresh configure/build, the
relevant tests, focused lint/format checks, and a real runtime path; record
observed results in `CHANGELOG.md`.
