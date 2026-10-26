---
schema: 1
updated: 2026-10-25
---

# ChadVis backlog

Canonical ranked backlog. Rules that govern how to edit it live in
[`AGENTS.md`](AGENTS.md); the claim/lease mechanism is
[`scripts/task.sh`](scripts/task.sh).

## How to read this file

**Line grammar**

```
- [<mark>] <ID> <title> (<files>) [P] #tags (after: <IDs>)
```

* `<ID>` — stable, never reused. Survives renames and reordering.
* `(<files>)` — the write set. **Two claims whose file lists intersect are a
  conflict**; the audit reports it. This is the same-file rule made static.
* `[P]` — parallelizable: disjoint from every live claim's files.
* `#tags` — cost-of-delay, as tags rather than a fake-precise score.
  `#risk-high #cod-high #blocks-ui #security #data-loss`.
* `(after: T####)` — dependency. `task.sh next` honours it.

**Status marks**

| Mark | Meaning |
| --- | --- |
| `[ ]` | todo, unclaimed |
| `[~]` | claimed — an agent is on it |
| `[?]` | **code written, NOT verified** |
| `[x]` | **done and verified.** The only finished state |
| `[!]` | blocked — reason inline |
| `[-]` | dropped, decided against |

**Only `[x]` means finished.** Code that compiles is `[?]`. An item reaches `[x]`
only when the verification command in `STATUS/<id>.log` passed. `task.sh audit`
fails the build if `[x]` has no `VERIFY` line.

**Ranking is positional** within each tier. Appending a P2 renumbers nothing, so
concurrent agents never invalidate each other's arithmetic. Tiers are P0–P3;
there is no computed score.

**One line to title and evidence; floor to an essay at `docs/backlog/TXXXX.md`.** A
checklist answers "is this done and who is on it" in one glance; a detail file holds the
repro, the log and its witnesses when one line is not enough. `task.sh audit` watches the
board at 256 KB, which is a pile-point, not a writer's cap: a writer who needs more than one
line to describe an item is describing it in the wrong place. Finished work moves to
[`CHANGELOG.md`](CHANGELOG.md) and the line is deleted — `git log -p TODO.md` is the ID archive.

---

## P0 — the product is broken, data is corrupted, or a secret is exposed


      **Closed 2026-10-25: the parser read a two-releases-old envelope.** A
      `route-probe.mjs` run against both API bases settled the ambiguity in one
      shot — `unified/explore` answers **200 with 165 KB and 2 feeds on both**
      `studio-api-prod.suno.com/api` and `suno.com/a/api`, byte-identical, so the
      host was never the problem and nothing had moved for us. The live envelope
      is the same *content_item* shape as `unified/library`:
      `feeds[]` name themselves `feed_id`/`feed_title`, every item is
      `{content_id, content_type, content_item}`, one item's `content_item` is a
      **nested feed** (`generic_feed`, whose items are the clips), and the first
      feed is six `shortcut` tiles (`shortcut_schema`) that are navigation, not
      content. The parser expected `id`/`title` on the feed and a clip at the top
      of each item, so every feed came back labelled "Explore" with zero clips
      while the backend was healthy — an empty page with a 200 behind it.
      `parsePage` now reads `feed_id`/`feed_title`, unwraps `content_item`,
      republishes a nested feed as its own labelled section (depth-bounded at 2),
      drops a container feed that held only nested sections, and drops
      `shortcut`/`shortcut_schema` entries — the last of those matters because
      `ClipParser` is tolerant enough to accept a tile (it has an `id`) and would
      have rendered a card with no artist and no duration. Pinned by
      `parsesTheContentItemEnvelopeWithNestedFeeds`, which reproduces the live
      shape key-for-key. VERIFY in `STATUS/T0333.log`. **Not verified:** the
      rendering itself — that is MT-001-D3 with a signed-in session.

- [ ] T0336 **Adopt the moved API base: `https://suno.com/a/api` — one constant, one host entry, two cursor styles** (`src/suno/SunoEndpoints.hpp`, `src/suno/CapturedHosts.cpp`, `src/suno/SunoClient.cpp`) #risk-high #cod-high
      **Downgraded from urgent on 2026-10-25, not closed.** The 2026-10-24 HAR shows the
      browser using `suno.com/a/api` for every route this client owns — `feed/v3`,
      `unified/library`, `unified/feed`, `notification/v3`, `notification/v2/badge-count` —
      while `studio-api-prod.suno.com` receives nothing but a 404 on the `/api/forbidden`
      media placeholder. But `route-probe.mjs`, run against both bases with this app's own
      bearer, gets **byte-identical 200s** for `feed/v3` and `unified/explore`, so the old
      base is a live alias, not a dead endpoint: adopting the new one is hygiene and
      future-proofing, not a fix, and it must not be sold as either.
      **Scope, in the order that makes each step checkable:** (1) `CapturedHosts` gains
      `suno.com` for `Role::StudioApi` — earned by two captures, and the allowlist is
      fail-closed, so a wrong base fails loudly rather than silently talking to nobody;
      (2) `API_BASE` becomes `https://suno.com/a/api` and the path constants keep their
      spelling (`/feed/v3`, `/notification/v3`, …) because the new base already carries `/a/api`;
      (3) `studio-api-prod` stays as a fallback only if a route is proven to answer there and
      not here — no route has been proven that way, and `unified/library` and
      `notification/v3` exist only on the new base.
      **Do not share one pagination helper:** `feed/v3` returns `has_more` + an opaque 36-char
      cursor, `unified/library` returns a numeric string offset (`"10"`) and no `has_more`.
      **Measured 2026-10-26, and it changes what the migration is worth:** `page_size` on
      `unified/library` is **ignored** — 20/50/100 all return a byte-identical 96 450-byte body
      (11 rows, `item_count 57`, `next_cursor "10"`), so this route does not sync more per call.
      `/feed/v3`, which this client already uses, *does* honour `limit` up to 100, and that is
      where the page-size win came from. `GET /playlist/{id}/?page=N` is now verified rather than
      a lead: a flat playlist body, `playlist_clips[] = {clip, relative_index, created_at}`,
      1-based pages, and no `has_more` — an empty array past the end is the only end signal.
      **Still bundle-declared only:** `unified/homepage`, `unified/homepage/explore`,
      `unified/homepage/explore/mobile`, `unified/search/omnisearch` and `/api/search/`.

- [ ] T0335 **The notification client is wired to v2 on a host the browser no longer uses, and the v3 shape is different** (`src/suno/SunoNotificationService.cpp`, `src/suno/SunoEndpoints.hpp`, `src/suno/CapturedHosts.cpp`) #risk-medium #blocks-ui
      **Downgraded 2026-10-25: v2 still answers.** Probed with this app's bearer, on both bases:
      `GET /notification/v2` → 200 with `{notified_at, notifications, next_before_datetime_utc}`
      (3 276 bytes) and `/notification/v2/badge-count` → 200 `{badge_count}` on the old base
      (the new base answered 429 — the limiter, not a missing route). So nothing here is
      broken today; the item is the eventual move the browser has already made, with the shape
      delta below the reason it is a migration and not a URL edit.
      The 2026-10-24 capture ([§4.8](docs/suno_api/ENDPOINT-INVENTORY.md)) shows the site
      calling `GET https://suno.com/a/api/notification/v3?include_hooks=true` and getting a
      200 with `{notified_at, notifications[], next_before_datetime_utc}` per-entry
      `avatars[]` / `text[]` / `thumbnail_url` / `trailing_button`. This client calls
      `/api/notification/v2` on `studio-api-prod.suno.com` and has for two captures. Three
      things have to move together and none of them alone:
      **(a) the host** — `suno.com` with base `/a/api` is a `CapturedHosts.cpp` row, and it is
      earned by that capture; `studio-api-prod` stays for the routes that still use it until a
      capture says otherwise, because a host entry is per-host and not a wholesale move.
      **(b) the parser** — `parseResponse` reads `user_profiles`, `content_title`,
      `content_message` and `caption`, none of which v3 sends; it would build notifications
      with an empty author and caption and no error. v3's text is `text[].text` concatenated,
      and the picture is `avatars[0].image_url`.
      **(c) the badge stays on v2** — and that is now measured rather than guessed: one
      session made 25 calls to `/a/api/notification/v2/badge-count` alongside 25 to
      `/a/api/notification/v3`. There is no v3 badge route, so the split is "list v3, counter
      and mutations v2", and `read`/`clear-badge` are still v2-only in the bundle. Do not
      derive the counter from `is_read` in the list; the server still publishes it.
      **Before any of it:** one request from this client that logs status, the top-level keys
      of the parsed object and the first array's length (never the body) says whether v2 still
      answers. A 2xx from v2 means this is an addition, not a migration, and (b) still applies
      the day the route flips. Note that T0067's poll now fires this route every 60 s, so a
      dead v2 becomes a visible error every minute rather than an empty list.

- [ ] T0334 **The first library sync pages the whole library at ~3.4 s/page — six minutes of spinner for 2 147 clips** (`src/suno/SunoLibraryManager.cpp`) #risk-high #perf
      **Corrected 2026-10-25: it is not infinite.** `onLibraryFetched` sets
      `hasMorePages_ = client_->hasMorePages() && !nextCursor().isEmpty()` and logs
      `Sync complete after N page(s)` on the last one, so the loop *does* terminate. What the
      2026-10-23 log shows is an unfinished sync, not a runaway one: 90 pages in 4 min 22 s
      at ~3.4 s/page (`QTimer::singleShot(1100)` plus request latency), and 2 147 clips at 20
      per page is 108 pages — about six minutes of first launch, every launch
      (`refreshLibrary(1)` runs on `Component.onCompleted`), with the spinner up and a
      status line the only feedback. The library itself renders progressively; the Discover
      page's emptiness that the user reported alongside this was a parser bug (T0333), not
      this cadence.
      **So the work is cadence and honesty, not a stop condition.** (a) **shipped
      2026-10-25:** the page size is 100, not 20. Measured with this app's own bearer — `limit`
      is honoured up to 100 (20/50/100 returned exactly that many clips) and 200 answers a
      `400`, with the wire's own default when the field is omitted being 20. 2 147 clips is now
      22 round trips instead of 108; pinned by `theFirstPageRequestsAHundredClips`, which
      asserts the request body, because nothing else in the tree would notice the constant
      reverting. A server that later caps the page lower is still handled: `has_more` decides.
      (b) still open — a sync that resumes rather than restarts, so a relaunch does not re-page
      pages 1–N to reach page N+1; (c) still open — the `{complete, pagesFetched, expectedTotal}`
      T0167 reports is unconsumed, so nothing can say "still syncing, 940 of 2 147"; a progress
      number would make the remaining minutes diagnosable instead of looking stuck.
      **Verified:** aggregate `unit_tests` suite `TestSunoLibraryManager` -> 0, full ctest 14/14.
      **Not verified:** the end-to-end wall-clock time against a live account — the 22-page
      arithmetic assumes ~3.4 s/page holds at 100 clips per page (bigger bodies, same limiter).
      **A page-size note, not a finding:** the client asks for 20 per page. The live browser
      traffic seen on 2026-10-24 pages notifications with a UTC cursor, and a v3-style feed
      may accept a larger page than 20 — but nothing captured says so, so a bigger page is
      `[LEAD]`-grade until the same capture that settles T0333's route also records its
      request body. Fixing the finish line is worth more than a bigger page: the loop above
      is ~1 800 clips against an API that has no reason to serve them twice.

- [ ] T0193 **User-defined themes need a C++ `ThemeCatalog`: QML cannot list a directory** (`src/qml_bridge/ThemeCatalog.hpp`/`.cpp`, `src/qml_bridge/BridgeRegistration.cpp`) #cod-high
      Phase 3 of the plan. Discovery of `<configDir>/themes/*.json`, the §11 validation table (schema version, `id` pattern and uniqueness, 64 KiB cap, reserved `custom`, bad-hex rejects the whole file, contrast warns but never auto-adjusts), and `availableThemes` for the Settings combo. Named `ThemeCatalog`, **never** `Theme` — a C++ type named `Theme` shadows the QML singleton, which is exactly how `ThemeBridge` became unreachable and was deleted as dead code (T0055). `Theme.qml` must keep never parsing files itself; it asks the catalog for a resolved definition.
## P1 — correctness, robustness, and debt that will bite

- [ ] T0011 **The whole 11-route library/playlist mutation surface is runtime-unreachable and unobservable** (`src/suno/SunoLibraryMutations.hpp:493`, `src/suno/SunoLibraryMutations.cpp:394-397`) #risk-high
      **The contract gap is closing:** as of 2026-10-26 the request *and* response shapes for reaction, both
      remix toggles, visibility and the whole playlist CRUD set are captured (§5.13). `setEnabled` being
      false and no QML connecting the four signals (T0320 a/b) is now the entire blocker — the wire format is
      no longer a reason to leave the surface dark.
      `bool enabled_{false}` and `dispatch()` always refuses. `setEnabled` is declared
      (:281) and defined (:141) with **zero production callers**. Separately, **no QML
      file** connects `clipMutationSettled`, `playlistMutationSettled`, `clipSaveRefused`
      or `libraryRefreshRecommended`, nor reads `mutationsAvailable` /
      `mutationsUnavailableReason` / `mutationsBusy`. ~727 LOC plus 11 `Q_INVOKABLE`s can
      neither run nor be seen. Decide: wire it behind an explicit user setting **and** add
      the QML handlers, or delete it and mark the constants `declared-unused`.
- [ ] T0049 **Qt 6.12 can convert missing `required property` into a load-time error — not used** (`src/qml/`, `src/core/Application.cpp`) #risk-high
      "When instantiating a component with missing required properties, the engine now
      reports an error instead of silently constructing an incomplete object." Marking
      C++-injected QML dependencies `required` and supplying them via
      `setInitialProperties` converts a whole class of runtime-only bug into a load-time
      failure. Also connect `QQmlApplicationEngine::objectCreationFailed` (since 6.4) — it
      is the difference between "the app showed no window" and a diagnosable error.
Carried over from the superseded backlog and **confirmed by re-reading the tree**:

- [ ] T0170 **Overlays and karaoke cannot appear in recorded video at all** (`src/visualizer/VisualizerWindow.cpp`, `src/recorder/`) #risk-high #blocks-feature
      The recorder captures inside the *native* `VisualizerWindow` GL context; the QML scene
      graph is a sibling on top in the `QQuickWindow`. **The entire overlay system is invisible
      to the entire video output.** GL compositing is structurally impossible (projectM draws
      to FBO 0, the one Qt Quick owns), so the recorded decision is an FFmpeg post-pass —
      which is exactly what T0108/T0114 add. Recorded here as the product fact that makes the
      post-pass mandatory rather than optional.
- [ ] T0171 **The first captured frame is silently discarded and `pixelFormat` is decorative** (`src/recorder/`) #risk-high
      The PBO readback gates on `pboAvailable_`, so frame 0 never reaches the encoder — every
      recording is one frame short. Separately `AV_PIX_FMT_YUV420P` is hardcoded while
      `EncoderSettings::pixelFormat` exists and `validate()` never checks that the chosen
      format is supported. Both are one-liners and both fail silently.
      **The pixel-format half shipped 2026-10-26; the frame-0 half is still open and needs GL.**
      `avPixelFormatFor()` maps the settings enum to an `AVPixelFormat`, the encoder context
      takes it instead of `AV_PIX_FMT_YUV420P`, and after `avcodec_open2` the *negotiated*
      format drives both `swPixelFormat_` (the sws target) and `videoFrame_->format` — an
      encoder that returns a different format is now logged rather than silently written into a
      frame declared as the other one. `validate()` refuses the two combinations that are
      certainly wrong: NV12/P010LE without a hardware encoder, and rgb24 with libx264/libx265
      (the error names yuv444p as the colour-lossless path). Pinned by
      `theChosenPixelFormatReachesTheMuxedStream` — which reads the finished file's
      `codecpar->format` back as yuv422p, not the settings struct — and
      `impossiblePixelFormatsAreRefusedByValidation`. The frame-0 half stays open because it is
      a PBO readback on a real GL context, and a headless run proves nothing about it.
- [ ] T0173 projectM's broken-preset retry: the premise needs GL evidence before anything is changed (`src/visualizer/projectm/Bridge.cpp:48`) #risk-medium
      Originally filed as: registering `projectm_set_preset_switch_requested_event_callback` overrides projectM's own single-slot handler, killing its 5-retry recovery. **Inspection says the premise is wrong.** projectM has TWO callback slots here (`ProjectMCWrapper.cpp:98-112`): `preset_switch_requested` and `preset_switch_failed`. `Bridge::init` overwrites only the **requested** one. The retry lives in the **failed** handler — `PlaylistCWrapper::OnPresetSwitchFailed` (`PlaylistCWrapper.cpp:57-100`) — which the app never touches, and it is still installed, so a failed preset load should still retry up to `m_presetSwitchRetryCount` and advance via `PlayPresetIndex`. The claim cannot be settled by reading: it needs a real window, a corrupt `.milk` next to a valid one, and a forced switch, because the whole path is GL-dependent and every headless lane skips it. **Do not "fix" this on the current evidence** — re-installing both callbacks or forwarding failures into the app's pending mechanism would change working native wiring on the strength of a claim that inspection contradicts. If it is taken up: verify with the broken-preset experiment first (a temp dir with one truncated `.milk` and one valid preset, force play, count retries through the app's `switched` signal), and only then decide. Distinct from T0057, which is about the never-emitted `presetLoading` flag.
- [ ] T0180 **ASan lane: `TestQmlStartup::settingsWindowOpensOnNavigate` throws at teardown with the engine still mid-creation** (`tests/integration/test_main.cpp:70-126`, `tests/integration/CMakeLists.txt:33`) #risk-high
      Found by the ASan lane's first full integration run -- this lane is NOT in `SAFE_TESTS`, so `--safe` never reaches it and the fast profile cannot see it. Under `--asan`, the test fails with "Caught unhandled exception" (qtestcase.cpp:2002), preceded by `QQmlComponent: Component destroyed while completion pending` and `There are still "77" items in the process of being created at engine destruction`, then `libc++abi: terminating due to uncaught exception of type std::system_error: mutex lock failed: Invalid argument` after the totals line.
      Verified NOT caused by the T0047 theme work: the same failure reproduces with `Theme.qml` checked out at 435f91f (pre-change) and rebuilt, so it is pre-existing, instrumented-timing-only, and invisible to `--safe`. Fast profile passes the same test 4/0. The 77 pending items point at teardown ordering -- the engine is destroyed while async component creation is still pending -- and the mutex error is a separate teardown crash. Do not "fix" by making the test wait; establish what is still being created at destruction first.

- [-] T0182 **Bookmarklet credential handoff, so no user ever opens devtools** (`src/suno/auth/`) #risk-high
      **Dropped on captured evidence, not abandoned.** The item was gated on one fact — whether `__session` is `HttpOnly` — and the recon now answers it. `suno-recon-cat-stealth-modekl/suno-oauth-google.md` carries the actual `Set-Cookie` headers from a live sign-in, and **`__client` is `HttpOnly; SameSite=None; Secure`** with a one-year expiry, as is its instance-suffixed variant. `document.cookie` can therefore never read the *durable* credential — only `__client_uat`, which the same capture shows is the plain timestamp `1787705821` and is not a credential at all. A bookmarklet would hand the app a number and a pageful of analytics cookies, which is worse than the paste path because it looks like it worked. The paste floor stands, and the real replacement is T0199: read the browser's own cookie store, which `HttpOnly` does not protect.

- [?] T0199 **"Sign in with Google" opens a browser and then extracts nothing — the user is already logged in, and the app has no way to receive the credential** (`src/suno/auth/`) #blocks-ui #risk-high #cod-high
      The reported symptom. `AuthCoordinator::beginGoogleSignIn()` opens `https://suno.com/create?redirected_from=signin` and stops there, by design: Clerk's flow delivers the OAuth `code` to `auth.suno.com/social/complete/` and **never to 127.0.0.1**, so there is no callback for a desktop client to catch. For a returning user the browser simply opens on an already-signed-in page, nothing is exchanged, and the app waits forever for a paste the UI only mentions in a collapsed field. **The credential exists, in the right place, right now.** The recon answers the whole question end to end: `suno-recon-cat-stealth-modekl/suno-oauth-google.md` captures the live flow — `POST /v1/client/sign_ins` with `strategy=oauth_google` sets `__client` (`HttpOnly`, `Secure`, `SameSite=None`, **Max-Age 31536000 ≈ 1 year**, a refresh JWT whose payload says `"token_type":"refresh"`) plus `__client_uat`; `reports/endpoints.md` confirms `POST auth.suno.com/v1/client/sessions/{sid}/tokens` mints fresh `__session` bearers from it and that the `__session` bearer itself (`aud: suno-api`) is what the API wants. On this machine, Brave's cookie store holds `__client`, `__client_Jnxw-muT`, `__client_uat(_Jnxw-muT)` and `__session(_Jnxw-muT)` for `suno.com`/`auth.suno.com` — verified by read-only query on a copy of the DB. **The fix: harvest the browser's cookie store.** `HttpOnly` blocks `document.cookie` (which is why T0182 died) but not the on-disk SQLite store; the recon's own `bin/freshtok.py` already does this for Brave on macOS (keychain `Brave Safe Storage` → PBKDF2-SHA1, 1003 iters, `saltysalt`, 16-byte key → AES-128-CBC, v10 layout, 13-byte header). Implement the same in C++: copy the DB (WAL-safe), select the Clerk cookie family for `suno.com` hosts only, decrypt, assemble a `Cookie:` header, and hand it to `SunoClient::setCookie()` — which already persists to `CredentialStore` and kicks a bearer fetch, and whose `classifyStoredCredential()` already accepts exactly these three name prefixes. **Explicit user action, never automatic**: the first read of a browser's Safe Storage secret makes macOS ask the user to allow it, which is precisely the consent moment this needs. Never log a value; `suno.com` hosts only.
      **Shipped as `[?]` — code complete, all gates green, and the shipped decrypt proven against the real store, with exactly one leg unverified: a human pressing the button.** Three decrypt defects were found only by driving the real Brave store, none reproducible in the synthetic fixtures: `QFile::copy` silently refuses to overwrite the temp file `QTemporaryFile` already created (so every real-store read returned nothing while the synthetic one passed); `kCCOptionPKKS7Padding` strips-or-silently-does-not as Brave re-encrypts cookies live (so the crypto call now uses `options = 0` with an explicit tail handler); and the 16-byte host-binding prefix on `.suno.com` cookies begins with byte `0xdd`, which a control-character-only "binary" check classifies as printable — the unit fixture now uses exactly that byte. Real-data evidence: the shipped `decryptChromiumValue` on this machine's live `__client` value returns 689 clean bytes, matching openssl ground truth; the full `harvest()` path read the same store. **What remains is MT-001-D2**: each newly-linked binary gets its own one-time macOS consent prompt for "Brave Safe Storage", which on an unattended machine times out and reports no session — correct behaviour, but the shipped app will prompt the same way the first time, and the user must press **Already signed in? Import the session** once and click Allow.
      **Post-shipment, pressing the button found three more defects, each fatal on its own.** (1) `deriveChromiumKey` passed CommonCrypto a salt **length of 10 for the nine-byte `"saltysalt"`**, so the derived key included the terminator and matched no Chromium build; every real cookie decrypted to noise and the app called its own bug "this build's format support needs updating". The synthetic round-trip could not see it because it encrypted with the same wrong key. Now `sizeof - 1`, with a known-answer test (`fecbbdb2774354b6b8466d2758047cb5`). (2) A browser stores each family **twice** — host-only `__client` and `__client_Jnxw-muT` — and all four JWT copies made the header **4695 bytes against `CredentialStore`'s 2560-byte ceiling**, so the import authenticated and then could not persist: the session worked until quit. `assembleCookieHeader` keeps one cookie per family (the host-only copy) and the live header now measures **2333** bytes. (3) The backend used `kSecUseDataProtectionKeychain`, which needs an `application-identifier` entitlement an ad-hoc bundle cannot have — every write returned `errSecMissingEntitlement` (-34018), so even a small credential was never stored. It uses the login keychain now; reads stay on the worker, a denied read is a failure rather than a plaintext fallback, and `CHADVIS_LIVE_KEYCHAIN=1 … liveKeychainRoundTrip` proves a real store/load/remove. Remaining for `[x]`: one human pass with the rebuilt app (import, quit, relaunch, still signed in) — MT-001-D2.
## P2 — robustness, cleanup, and debt

- [ ] T0327 **The credential-store handoff is a captured lambda across threads, and that is the whole TSan class** (`src/suno/SunoClient.cpp:267-335`) #risk-high
      The 39-report CI baseline and the 206-warning local run are one pattern: `CredentialStoreWorker::requestRestore` posts `QMetaObject::invokeMethod(guiReceiver_, [this, outcome, generation, shutdown]{...}, Qt::QueuedConnection)`. Qt copies that functor **on the worker thread** while constructing the `QMetaCallEvent` and reads it **on the GUI thread** when dispatching; both accesses are instrumented code in our translation unit (`qobjectdefs_impl.h:548`), while the happens-before edge lives in libQt6Core's event queue, which is not built with TSan. TSan therefore sees a write and a read with no edge between them and says so 206 times, "as if synchronized via sleep" — it can only infer the interleaving from the test's wait loop. The handoff itself is correct today (`restoreInFlight_`, `restoreGeneration_`, `restoreDiscarded_` and `restoreCompletions_` are GUI-thread-only by construction; `shuttingDown_` is atomic), which is exactly why suppressing the class is tempting and wrong: **a real race inside a capture produces an indistinguishable report.** Fix by construction, not by suppression: give the outcome a mutex-guarded slot on the receiver and post a **parameterless** queued call (`QMetaObject::invokeMethod(receiver, &SunoClient::onRestoreOutcomeReady, Qt::QueuedConnection)`), so every byte that crosses the thread boundary moves under our own lock and the event carries no functor storage. Target: zero reports on `TestSunoRequestEpoch` and `TestBoundedBody`, with `cmake/tsan.supp` still empty. Rule and rationale: [INVARIANTS.md](docs/dev/INVARIANTS.md#threads-and-ownership); measurement: [TESTING.md](docs/dev/TESTING.md#the-sanitizer-lanes).
- [ ] T0328 **T0154's two new posts joined the same class, so the audio path now contributes too** (`src/audio/AudioEngine.cpp`) #risk-medium
      `postStatusRefresh()` and `postErrorSignal()` are `QMetaObject::invokeMethod(this, [this, ...]{...}, Qt::QueuedConnection)` — the correct fix for the torn `QString`, and the same TSan-invisible handoff as T0327. They sit on the audio callback path, so they are hot and they will dominate the count as soon as the buffer output works (T0322). Apply T0327's rule: an atomic or mutex-guarded status slot plus a parameterless queued call, and verify the two suites added by T0154 still assert the *emitting* thread with `Qt::DirectConnection`.
- [ ] T0329 **Nineteen `QMetaObject::invokeMethod` sites, and no written rule about what may cross a thread boundary** (`src/`) #risk-medium
      `grep -rn "QMetaObject::invokeMethod" src/` finds 19 call sites, several carrying lambdas with captured state (`RecordingBridge.cpp` four, `AudioEngine.cpp` three, `SunoClient.cpp` three, `SunoDownloader`, `PresetManager`, `SunoBridge`, `SunoController`). Audit each as same-thread or cross-thread, apply the no-payload rule where it is cross-thread, and write the rule down once in [INVARIANTS.md](docs/dev/INVARIANTS.md#threads-and-ownership). Then add `scripts/check-thread-handoffs.py` — a grep-level gate in the same spirit as [check-workflows.py](scripts/check-workflows.py) — that lists every queued invocation carrying a capture for review, so the next one is a decision rather than an accident. Do not convert same-thread `DirectConnection` calls; the point is the boundary, not the API.
- [ ] T0330 **The TSan lane counts eight entries while `build.sh --safe` runs eleven, so "39" and "206" are different populations** (`build.sh`, `.github/workflows/tsan.yml`) #risk-medium
      The CI lane runs the 8 entries that carry `TSAN_OPTIONS` in `tests/unit/CMakeLists.txt`; `SAFE_TESTS` is 11. Every local `--safe` therefore reports more, and nothing says so — the same two-allow-lists class as T0040 and T0297. Make one list the source of truth, record the per-entry baseline in a checked-in file rather than the prose `BASELINE=39`, and fail on an **increase per entry** so a new race in a suite that already reports cannot hide in a flat total. Until then, a local count is not comparable to CI's and must not be quoted as a regression.
- [ ] T0331 **A sanitizer findings run is reported as a failed ctest entry, so red under `--safe` is ambiguous** (`build.sh`) #cleanup
      `unit_tests` printed `all suites returned, status 0` and still counted as failed, because TSan exits non-zero when it reports. A reader cannot tell "the product is broken" from "the known-benign class fired". Classify exit 66 (TSan) and 67 (ASan/UBSan) in `build.sh --safe`, print the report count next to the verdict, and keep a genuine assertion failure red. This is the `--safe` counterpart of the three-outcome classification already written in [TESTING.md](docs/dev/TESTING.md#the-sanitizer-lanes).

- [ ] T0318 **The saved stream carries no lyrics, and there are no user-settable tags for it to carry** (`src/suno/SunoDownloader.cpp:832-900`, `src/suno/SunoDownloader.cpp:1085`) #feature #risk-medium
      The file that now plays back is tagged with title/artist/album/comment and the four `SUNO_*` freeform atoms, and **nothing else**. Two separate facts, both measured on this machine's 2,147-clip library on 2026-10-22: `lyrics` is empty for **2,147 of 2,147** clips (the feed field is blank; **775** clips carry `aligned_lyrics_json`), and the MP4 branch of `tagAudioFile` writes **no lyrics atom at all** — `©lyr` is never set, unlike the MP3 branch's `USLT`. So the `.txt` sidecar's `Lyrics:` line is empty too, while the Library shows lyrics for the playing track because it reads the aligned data from the DB. Fix: (a) write `©lyr` (and a SYLT/`©lyr` equivalent where the container allows) from `clip.metadata.lyrics` when present, falling back to the DB's aligned-lyrics text for the same clip id; (b) fill the sidecar from the same source; (c) **there is no tag override to apply** — no Settings field sets artist/album/title, and `artist` is the Suno handle (`clip.display_name`) while `album` is the constant "Suno AI Generations", so the report's "custom tags the user can set" describes a feature that does not exist yet. Decide the override schema in `[suno]` (`artist_template`, `album`, `write_lyrics`) and thread it here; do not invent per-file prompts in the dialog first.
- [ ] T0326 **`AudioEngine::processAudioBuffer` reads `scratchBuffer_` while `applyAudioConfig()` may resize it** (`src/audio/AudioEngine.cpp`, `src/audio/AudioEngine.hpp`) #risk-high
      Found while closing T0154 and deliberately left out of it: the callback takes `scratchBuffer_.data()`/`.size()` on the audio thread, while the GUI thread calls `applyAudioConfig()` → `scratchBuffer_.resize()`. A resize can free the storage the callback is writing through, which is a use-after-free, not a stale read. Fix needs a decision the atomics pass did not make: double-buffer with an atomic pointer swap, or refuse conversion while reconfiguring. Not reproducible on this machine because the darwin media backend delivers no buffers (T0322), so it needs a device-free test through `submitAudioBuffer` plus a reconfiguring thread.

- [ ] T0051 `QTP0004` is deliberately disabled while 58 QML files live away from the `qt_add_qml_module` call (`cmake/TargetSetup.cmake:129-134`) #risk-medium
      Qt calls this layout "a frequent source of mistakes" and 6.8 can generate per-directory
      `qmldir`s (`NO_GENERATE_EXTRA_QMLDIRS`). Either enable it and fix the relative imports,
      or flatten the QML tree beside the CMake call.
- [ ] T0165 **Preset discovery will miss `.milk7`/`.milkdrop` packs, and `.pmesh` is not a thing** (`src/visualizer/PresetScanner.cpp`) #risk-medium
      The official scan is `projectm_playlist_add_path`, whose implementation tests
      `entry.path().extension() == ".milk"` — **exact and case-sensitive**, so `.Milk`,
      `.MILK`, `.milk7`, `.milkdrop` are all **skipped**. Packs in the wild ship all of those.
      A host that globs `*` will silently load fewer presets than the official player. Note
      projectM v4 has **no `milkdrop.ini` / `*.tex` model** at all — texture resolution goes
      through `projectm_set_texture_search_paths()`, and calling it per-frame is a performance
      bug (it clears and reloads every texture). Also: `.pmesh` is a projectM **3.x-era
      frontend** file and does **not** exist in v4 — do not write a migration for it.
- [ ] T0058 `RatingManager` is a process-wide, mutex-less singleton keyed on preset **name** (`src/visualizer/RatingManager.cpp:9-25`) #blocks-feature
      One global `ratings.toml` keyspace means two preset banks containing the same preset
      name share a rating — **structurally blocks multiple banks.** `ratings_` is a bare
      `std::map` with no lock. Fix: inject it, key on path, and add a mutex.
- [ ] T0060 Dead members and methods with grep proof (`src/`) #cleanup
      `Config::addOverlayElement`/`removeOverlayElement`/`findOverlayElement` (zero callers),
      `VisualizerRenderer::setTargetFps`, `VisualizerWindow::feedAudio` (deprecated empty
      body), `BridgeRegistration::getThemeBridge`, `main.cpp`'s `g_app` (written twice, read
      never), `SunoClient::cancelledPolls_` (inserted at :1422, never read),
      `DownloadQueue::Item::cancelRequested` (set :363, reset :399, never read) and
      `Item::metadata` (written :331, never read), `SunoLibraryManager::setSearchText`,
      `SunoDatabase::searchClips`/`getAllClips`, `GateResolver::setTierPolicy`.
- [ ] T0061 `Result<T>`'s monadic surface is unused **and non-compiling** (`src/util/Result.hpp:106-159,211-224`) #cleanup
      `map`/`andThen`/`orElse`/`operator->`/`operator*` have zero users; `TRY`/`TRY_VOID`
      have zero users **and** expand `::value_type`, which a member `Result<T>` does not
      define (:83-99). `Result<void>::error()` dereferences unconditionally (:192-194) —
      UB on the success path, safe only because callers guard with `isErr()`. Fix: adopt
      `std::expected` as `SunoWorkspace` already does, and delete the rest.
- [ ] T0062 `SunoBridge` is a god object: 1662 cpp + 405 hpp, 37 properties, 40 invokables (`src/qml_bridge/SunoBridge.cpp`) #cleanup
      Mixes service lifetime, QVariant translation, list caching, download orchestration,
      chat and 11 mutation forwarders. **~60 % of the file (:1380-1662) is mutation
      forwarding `SunoLibraryMutations` already owns.** Split into `SunoExploreBridge`,
      `SunoNotificationsBridge`, `SunoUploadBridge`, `SunoMutationsBridge` (~200 LOC each).
- [ ] T0063 ~120 LOC of tolerant-JSON accessors duplicated across four files, with **disagreeing** tolerance rules (`src/suno/SunoNotificationService.cpp:19-76`, `SunoExploreService.cpp:21-32`, `SunoAccountManager.cpp:19-43`, `ClipParser.cpp:14-59`) #risk-high
      `scalarString` is byte-identical in two files. `optBool` accepts `"True"`/`"False"`
      but `booleanValue` accepts only `"true"`/`"1"` — **the two disagree on the captured
      filter strings.** Fix: one `JsonCoerce` helper in `ClipParser.hpp`.
- [ ] T0064 Duplicated enum→string and auth-failure maps, with three owners of one vocabulary (`src/qml_bridge/SunoBridge.cpp:73-88,90-106,333-374`) #cleanup
      Two `AuthFailureKind` switches in the same file plus a third in `AuthCoordinator`
      (:404-405 admits it); and the bridge re-implements `toString(DownloadState)`
      (`DownloadQueue.hpp:45-55`) so adding a state leaves the `switch` non-exhaustive.
- [ ] T0065 `SunoOrchestrator` is a permanently-failing stub with unreachable members (`src/suno/SunoOrchestrator.cpp:14-44`) #cleanup
      Both methods only `emit errorOccurred`; `onMessageFinished`/`onHistoryFinished` are
      private and never called; `client_` is written and never read. It **is** instantiated
      (`SunoController.cpp:85`) but `SunoBridge` bypasses it, so its signals are
      unreachable from QML.
- [ ] T0066 `searchText` is threaded three layers and dropped at the bottom (`src/suno/SunoLibraryManager.cpp:106,122`, `src/suno/SunoClient.cpp:1310`) #cleanup
      `Q_UNUSED(searchText)`, and `setSearchText` has no callers, so the field is always
      empty. `SunoLibraryManager.hpp:76` ("it is not sent to the feed") contradicts the two
      `fetchLibraryPage(…, searchText_)` calls. Real filtering is client-side in
      `SunoBridge::updateFilteredClips`.
- [ ] T0068 ~15 dead model structs in `SunoModels.hpp`, including a **second parallel schema for notifications** (`src/suno/SunoModels.hpp:207-337`) #cleanup
      No producer or consumer. `SunoNotification` (:240) duplicates
      `SunoNotificationService::Notification` (:28 of its header), which is the type the
      live parser actually uses.
- [ ] T0069 Eleven endpoints are wired in code but recorded `not-in-code` in the inventory (`docs/suno_api/ENDPOINT-INVENTORY.md:610-620,1206-1216`) #data-loss
      `GEN_SET_VISIBILITY`, `GEN_UPDATE_FEEDBACK_STATE`, `CLIP_TOGGLE_REMIXES`,
      `CLIP_TOGGLE_SHOW_REMIXES`, `SHARE_LINK`, `PLAYLIST_SET_METADATA`,
      `PLAYLIST_UPDATE_CLIPS`, `PLAYLIST_V2_TRACKS_ADD/REMOVE/REORDER`, `PLAYLIST_V2_COVER_IMAGE`.
      All are sent from `SunoLibraryMutations.cpp`. The inventory is **wrong about the code
      and right about the behaviour** (T0011); both halves need recording. Also 8
      constants exist in `SunoEndpoints.hpp` with no inventory row at all.
- [ ] T0071 ~120 dead QML-facing bridge properties/invokables, including word-level karaoke timing that is implemented in C++ but never surfaced (`src/qml_bridge/LyricsBridge.hpp:32,34,101,111,91`) #cleanup
      `currentWordIndex`, `wordProgress`, `getUpcomingLines`, `getContextLines`,
      `assDocument` have **zero QML readers** — the karaoke feature exists below the UI.
      Also dead: `VisualizerBridge.fps` (no FPS readout anywhere), `SettingsBridge.karaokeFont`
      (font not user-settable), `drawerOpen`, `refreshAudioDevices` (no button),
      `RecordingBridge.currentFile`, 9 `PresetBridge` members, and 11 unreferenced
      `SunoBridge` mutation invokables. (after: T0001)
- [ ] T0072 No `reuseItems` anywhere; a nested non-interactive `GridView` inside a virtualized `ListView` (`src/qml/`) #performance
      Zero occurrences across 7 views. `DiscoverView.qml:115-127` materialises every clip of
      every feed. Also 16 `layer.enabled` sites, and `KaraokeMaster.qml:117-126` re-evaluates
      a two-`MultiEffect` stack on **every `lineProgress` change**.
- [-] T0073 Per-item buttons inside delegates are killed by a trailing full-bleed `MouseArea` — same class as T0004 (`src/qml/panels/OverlayPanel.qml:310-315`) #blocks-ui
      Dropped as a duplicate of T0004: the citation names the same trailing
      OverlayPanel.qml MouseArea T0004's file list already contains, and the
      "different panel" claim contradicted its own citation. Fixed and
      verified under T0004.
- [ ] T0076 Views with no loading, empty, or error state (`src/qml/views/VideoView.qml:18-53`, `panels/PresetsPanel.qml:45-54`, `panels/PlaylistPanel.qml:87`, `components/TransportBar.qml`) #blocks-ui
      `VideoView` shows a black rectangle captioned "Ready". **Model correctly on
      `LibraryView.qml:397-448`, `DiscoverView.qml:207-261`, `NotificationsView.qml:191-243`
      and `AccountPage.qml:700-746` — those are the template.**
- [ ] T0080 Two writes to a bound `value` on `AppSlider` fight each other (`src/qml/panels/settings/KaraokeSettings.qml:52`, `panels/RecordingPanel.qml:180,264`) #risk-high
      Qt explicitly recommends interaction signals (`onMoved`) over `onValueChanged`, which
      "can lead to event cascades where the value is constantly changed because it is
      rounded or normalized".
- [ ] T0081 `AppSlider` is keyboard- and screen-reader-inaccessible (`src/qml/components/AppSlider.qml`) #risk-high
      `Item` + `MouseArea` only (:127-151): no `activeFocusOnTab`, no arrow-key handling, no
      `Accessible.*`. It **is** the seek bar, volume, CRF, beat sensitivity and karaoke
      position. Also a division by zero at :37 if `from == to`, guarded at the only two
      call sites.
- [ ] T0082 34 `Accessible.*` properties across 11 of 58 files; 47 files have none (`src/qml/`) #risk-high
      Zero in PresetsPanel, OverlayPanel, PlaylistPanel, LyricsPanel and `AppSlider`. Focus
      rings exist on 2 of 8 interactive primitives. `ClipCard.qml:327-329` sets
      `Accessible.role: CheckBox` on a `Rectangle` with no `activeFocusOnTab`, so batch
      selection has no accessible path.
- [ ] T0083 Contrast failures are real but not where the old claim said (`src/qml/styles/Theme.qml`) #cleanup
      **`textPrimaryVariant` is fine** (6.53–9.26:1, AAA). The actual failures: `textDisabled`
      **1.95–2.77:1**, `recording` 3.07:1, `textSecondary` 3.46:1, `error` 3.60:1. **And the
      real defect is text over the projectM canvas** — `TransportBar.qml:123` uses text
      over `glassBackground`, which is only 85 % opaque (`Theme.qml:48`), so a white frame
      collapses the ratio to ≈1.3:1; `VideoView.qml:50-52` uses text at `opacity 0.55` over
      raw video. Unmeasurable by construction.
- [ ] T0084 ~58 raw unthemed QtQuick.Controls where an `App*` wrapper exists (`src/qml/`) #cleanup
      Raw `Slider`, `TextField`, `ComboBox`, `SpinBox`, `CheckBox`, `RadioButton`, `Label`
      across 10 files, bypassing the theme and the focus rings. **Colour tokens themselves
      are disciplined** — 38 hex literals tree-wide, 25 of them in `Theme.qml`; do not
      "fix" colours.
- [ ] T0085 `TextAccessible` names are hardcoded English (`src/qml/`) #cleanup
      An `Accessible.name` literal is a translation bug and an accessibility bug at once.
- [ ] T0086 Remote art fetched with no `sourceSize` and sometimes not `asynchronous` (`src/qml/components/ClipDetailSheet.qml:83-88`, `ClipCard.qml:109-116`, `SunoPanel.qml:140-145`) #performance
      `ClipDetailSheet` has neither, so it blocks the GUI thread and downloads full-resolution
      art on every open. No image caching policy anywhere; zero `@2x`/`devicePixelRatio` handling.
- [ ] T0087 Icons: two duplicate copies, neither used, and three destinations share one glyph (`resources/icons/`) #cleanup
      `lyrics.svg` and `recording.svg` exist twice; the registered copies are referenced by
      **no** QML file. `NavRail.qml:20,21,22` renders Notifications, Explore **and** Create
      with `iconUrl("suno")`.
- [ ] T0088 The macOS bundle ships no icon (`cmake/Info.plist.in`, `cmake/TargetSetup.cmake:262-290`) #cleanup
      No `CFBundleIconFile`, no `MACOSX_BUNDLE_ICON_FILE`, and no `.icns` in the tree, so
      Finder, the Dock and Spotlight show a generic glyph. Linux installs only a `scalable`
      icon with no `gtk-update-icon-cache` hook (`cmake/Install.cmake:29-32`, `scripts/PKGBUILD:44-50`).
- [ ] T0089 `file(DOWNLOAD)` of CPM.cmake at configure time into the **source tree**, unverified (`cmake/Dependencies.cmake:38-42`) #risk-medium
      No `STATUS`/`TIMEOUT`, so a failure leaves a zero-byte file and an unreadable-error
      message; writing into `CMAKE_SOURCE_DIR` mutates a read-only CI checkout.
- [ ] T0090 Nine of fifteen dependencies are unpinned, and two bypass the system-first fallback (`cmake/Dependencies.cmake`) #risk-medium
      `readerwriterqueue` has no system-first probe (unlike spdlog/fmt/toml++ at :50-85), and
      `CPM_SOURCE_CACHE` defaults OFF (`CPM.cmake:157`) so projectM is re-cloned per fresh
      build dir. **The third finding is closed (2026-10-23):** all five Git dependencies —
      fmt, spdlog, toml++, readerwriterqueue and projectM — are pinned to the commit their
      tag names, not to a mutable tag, so a force-pushed or re-cut tag no longer
      changes what a fresh build dir resolves.
      **Conan was considered and rejected** — pinning the CPM tags to commit SHAs plus a
      `vcpkg.json` closes the same two gaps in hours instead of a three-platform packaging
      migration; the reasoning and the trigger to reopen it are
      [DEPENDENCIES.md](docs/dev/DEPENDENCIES.md#why-not-conan).
- [ ] T0091 The Windows lane passes `CMAKE_BUILD_TYPE` to a multi-config generator (`.github/workflows/windows.yml:123-126`) #risk-medium
      No `-G` is given, so VS2022 is selected where the variable is inert — the root
      `CMakeLists.txt:58` explicitly guards this — and is papered over with `-C` at :139,:166.
      No `vcpkg.json`, so the six `vcpkg install` lines are unpinned and CI reproducibility
      there is zero.
- [ ] T0092 `Qt6::OpenGL` is linked by two test targets but `OpenGL` is never a requested component (`tests/unit/CMakeLists.txt:176`, `tests/integration/CMakeLists.txt:18`) #risk-medium
      It resolves only transitively through qtdeclarative's config loading — an undeclared
      dependency, not a guarantee. `DBus` is likewise required unconditionally
      (`Dependencies.cmake:13`) though only linked for Linux Secret Service
      (`TargetSetup.cmake:916-921`).
- [ ] T0093 No warnings-as-errors, and clang-format/clang-tidy enforced nowhere (`cmake/Compiler.cmake:80-83`, `.github/`) #risk-medium
      `-Wno-unused-parameter` is unconditional and grep finds no `Werror` anywhere.
      `.clang-format:33-35` explicitly says do **not** add `--dry-run -Werror` to CI and
      `.clang-tidy:221` sets `WarningsAsErrors: ''`. Fix: add a non-blocking
      `git clang-format --diff main` report step now.
- [ ] T0094 macOS CI pays full bundle deployment on every push (`.github/workflows/build.yml:65`, `cmake/TargetSetup.cmake:372-381`) #performance
      The suppression needs **both** `CHADVIS_FAST_ITERATION` and a Debug build type;
      build.yml sets only the latter, so `_chadvis_do_deploy` stays ON at a measured
      **5–7 minutes per link** (:358-362).
- [ ] T0097 `unit_tests` is one binary with one `QCoreApplication` running 31 suites sequentially (`tests/unit/test_main.cpp:38,41-72`) #risk-medium
      A crash or hang in suite 30 loses suites 1–29's results, and config-singleton mutators
      are unisolated.
- [ ] T0099 Four duplicate reply fakes, and no fake `QNetworkAccessManager` at all (`tests/unit/suno/`) #risk-high
      `FakeNetworkReply.hpp` is adequate but its `factory()` takes one `QNetworkRequest`
      while `SunoClient::ReplyFactory` takes three, so it is unusable for SunoClient suites.
      **Worse: no `FakeNetworkAccessManager` exists**, so a new suite that forgets to inject
      a factory makes a **real DNS lookup** (`test_LyricsPipeline.cpp:1163` gets away with it
      by luck). Consolidate into `tests/support/` as an INTERFACE library.
- [ ] T0100 `SunoLibraryMutations` — 26 KB of write-path logic — has zero tests (`src/suno/`) #risk-high #data-loss
      The single highest-value coverage gap in the tree. Also untested with **no test file**:
      `SunoDatabase`, `SunoBridge`, `SunoController`, `ClipResolver`, `PresetPersistence`,
      `RatingManager`, `CliUtils`/`CliArg`, and **every QML bridge**. **The entire UI has
      zero tests** — `integration_tests::mainQmlLoadsAndRenders` would pass with a blank window.
- [x] T0101 `FFmpeg 9.0`: the `AVFrame::channels` → `ch_layout` migration is **already done** — verify and close (`src/recorder/`) #cleanup
      Confirmed against libavutil 61.1.102 (FFmpeg 8.x): the tree uses `ch_layout` throughout,
      the remaining `.channels` are project structs (`spec.channels`, `settings.audio.channels`),
      and `av_hwdevice_ctx_create` carries no deprecation attribute. The blanket
      `-Wdeprecated-declarations` suppression in `VideoRecorderFFmpeg.cpp` is removed: a clean
      compile followed, so it was hiding nothing and would have hidden the next real one.
      `--fast --tests` exit 0, ctest 14/14.
- [ ] T0102 `FFmpeg`: the `AVFormatContextDeleter` crash is **already fixed**; two residuals remain (`src/recorder/FFmpegUtils.hpp`) #risk-medium
      **REFUTES the obvious task.** The single deleter that dereferenced `c->oformat` (null on
      a demuxer) is gone; it is now split into `AVFormatContextInDeleter` (`:65-70`,
      `avformat_close_input`) and `AVFormatContextOutDeleter` (`:57-63`, close `pb` then free),
      and the choice is enforced by the pointer type. The custom-IO opt-out correctly tests
      `c->flags & AVFMT_FLAG_CUSTOM_IO` (`FFmpegUtils.hpp:52`) — a field of the context, not
      `oformat->flags` — which is precisely the old bug. `VideoRecorderFFmpeg::cleanup`
      follows the correct muxer order (`:319` → `:342` → `:353` → `:360`). Residual work:
      (a) `hwFramesCtx_`/`hwDeviceCtx_` survive `cleanup()` and are never freed, so a re-`init()`
      after a partial HW failure keeps the old device; (b) return `AVERROR_EXIT` from
      `read_packet` to unwind an in-flight read *before* joining the worker thread.
- [ ] T0156 **Encoder teardown (`flush` + `av_write_trailer`) blocks the GUI thread** (`src/recorder/VideoRecorderCore.cpp:168`, `src/qml_bridge/RecordingBridge.cpp:323`, `src/recorder/RenderExecutor.cpp:802-807`) #risk-high
      `RecordingBridge::stopRecording` is a QML-invoked slot, so `stop` → `flush()` +
      `cleanup()` (which writes the trailer at `VideoRecorderFFmpeg.cpp:353`) blocks on the
      encoder drain and final I/O. `RenderExecutor::teardownSlot` likewise runs inside a
      `QTimer::PreciseTimer` timeout (`:553`) and joins the pump thread. Fix: finalise on a
      worker, post the completion.
- [ ] T0157 **GUI-thread reentrancy: the render runner can join its own worker while the queue is mid-pump** (`src/recorder/RenderExecutor.cpp:516-518` inside `RenderQueue::runJob` at `RenderQueue.cpp:468`) #risk-high
      `enqueue` → `pump()` → `runner_()` runs **synchronously on the caller's (GUI) thread**;
      the `ensureBackend` failure path calls `retireSlot` → `teardownSlot` → `pump.join()`,
      blocking the GUI thread on a worker inside `nextChunk`/`pushChunkTo`. The subsequent
      `fail` → `settle` → `pump` is a no-op because `pumping_` is set, so **the queue makes no
      progress while the GUI thread is blocked.** Fix: post the runner to a thread.
- [ ] T0158 **`RenderQueue::settle` runs the receipt write (open + `fsync` + `rename`) on the caller's thread** (`src/recorder/RenderQueue.cpp:515` → `src/recorder/RenderJob.cpp:252`) #risk-medium
      Reached from `finish`/`fail`/`cancel`; the production path for `finish`/`fail` is
      `renderTurn` on the GUI thread, so every render completion does three blocking syscalls
      on the GUI thread. Fix: queue the receipt write.
- [ ] T0159 **`retireSlot` holds a `JobSlot&` across a call that re-enters `jobRunner`** (`src/recorder/RenderExecutor.cpp:844-854`) #risk-medium
      `queue_->fail` at :853 reaches `settle` → `pump` → `runner_` → `jobRunner`, which does
      `slots_.push_back` (:512); if that reallocates, `slot` and `index` are stale before
      `slots_.erase` at :854. Survives today only because the queue's own `pumping_` guard
      suppresses re-entry on this exact path. Fix: erase the slot before reporting failure.
- [ ] T0104 `FFmpeg`: `swr_alloc_set_opts` → `swr_alloc_set_opts2`; `av_init_packet` → `av_packet_alloc`/`av_packet_unref` (`src/recorder/`) #risk-high
      `swr_alloc_set_opts` is **removed** (swr 4.5.100). `av_init_packet` still exists in
      9.0.2 but is deprecated and **gone in FFmpeg 10** (gate `FF_API_INIT_PACKET` is
      `<64`). **`avcodec_close` is already removed** (8.0) and `AVStream::codec`/`av_register_all`/
      `avcodec_decode_audio4` since 5.0.
- [ ] T0105 **Do not** "migrate" `av_hwdevice_ctx_create` (`src/recorder/VideoRecorderFFmpeg.cpp`) #risk-high
      It is **current, not deprecated** (verified in 9.0.2 `hwcontext.h:294`, and upstream
      `doc/examples/hw_decode.c:53`). `av_hwdevice_ctx_alloc` is **not** a replacement: it only
      allocates a shell and does not open the device — using it without populating
      `ref->data` and calling `av_hwdevice_ctx_init` is a regression.
- [ ] T0106 Loudness: momentary/short-term are unimplemented, and surround weighting is absent (`src/audio/LoudnessAnalysis.hpp:452-465`) #risk-medium
      **The integrated path is verified correct** against BS.1770-4 / libebur128 v1.2.6:
      `kAbsoluteGateLufs = -70.0` (:457), `kRelativeGateOffsetLu = 10.0` (:460),
      `kLoudnessOffset = -0.691` (:463), `kBlockSeconds = 0.400` (:452), `kStepSeconds = 0.100`
      (:455), 4× true-peak oversampling (:616), and the gate order is eq.(7) after eq.(6)
      after the absolute half (`LoudnessAnalysis.cpp:527,545,554`). K-weighting coefficients are
      **derived** by bilinear transform from the f0/G/Q tables (:324,331) rather than
      transcribed. Two gaps remain: (a) `kChannelWeight = 1.0` (:465) — correct for stereo, but
      **BS.1770 weights surround channels by ×1.41**, so this is wrong for 5.1; (b) **momentary
      (400 ms) and short-term (3000 ms) loudness do not exist** — the public struct (:387-427)
      has `integratedLufs`, `ungatedLufs`, LRA fields, `truePeakDbfs`, `samplePeakDbfs` and
      counters, and no M/S. That is a feature gap, not a bug: decide whether ReplayGain needs
      M/S or whether integrated-only is a documented limitation.
- [ ] T0107 `AV_VERSION_INT` is an untyped `int` with no `uint32_t` overload (`cmake/`, `src/`) #risk-medium
      `avcodec_version()` returns `unsigned`, so a widened comparison warns and past major 127
      sign-extends. `AV_VERSION_MICRO` is **meaningless across branches** — never gate on it.
      Use `pkg_check_modules(... libavcodec>=62.28 ...)` at configure time and extract
      `AV_VERSION_MAJOR`/`MINOR` at runtime. **Never parse `av_version_info()`** — its own
      documentation says it "should never be parsed by code".
- [ ] T0108 Muxed subtitle tracks: `libavfilter` + `libass`, behind a CMake option (`cmake/Dependencies.cmake`, `src/recorder/SubtitleBurnIn.cpp`) #feature
      `subtitles=filename=subs.ass:fontsdir=…` or the `ass` filter. **Subtitle encoders do
      not use `send_packet`/`receive_packet`** — `avcodec_encode_subtitle` is still the only
      path. Codec/container matrix: `mov_text` for MP4, **`webvtt` for WebM** (Matroska has no
      other text subtitle). Beware: `AVSubtitle.start_display_time`/`end_display_time` are
      **milliseconds** while `AVSubtitle.pts` is in `time_base` units. The local FFmpeg
      already has libass/libfontconfig/libfreetype/libharfbuzz.
- [ ] T0109 `suno.token`/`cookie` are documented in `CONFIG.md` as not-written — keep that honest, and stop logging clip identifiers (`src/suno/DownloadQueue.cpp:323,508-703`, `src/suno/SunoDownloader.cpp:246,409,412`) #security
      The serializer is **structurally** incapable of emitting them (`ConfigParsers.cpp:186-203`)
      — preserve that guarantee. Separately, a dozen log sites interpolate `clipId`, which is
      the account-linked half of the scrub plan.
- [x] T0111 `release.yml` believes macOS bundle packaging is broken and `.desktop`/dependabot comments are stale (`.github/workflows/release.yml:19-42`, `.github/dependabot.yml:7,16`) #cleanup
      Release half was already fixed (`required: true`, obsolete premise recorded). Dependabot header updated: four CPM pins, projectM as a system lookup, pffft noted as removed, seven workflows, fetch-metadata instead of the dropped opencode action, and a parse command that works on this box.
- [ ] T0113 `.gitignore` gaps beyond the ones already fixed (`.gitignore`) #risk-medium
      Stale build-artifact comments reference "22 of the 26 untracked files"; the root
      `build.log` (41 KB) is now ignored but the file is still present in the working tree.
      `.DS_Store` files remain in `tests/`, `tests/unit/`, `resources/`, and the docs root.
- [ ] T0114 `AVSubtitle`/subtitle research and libavfilter dependency gating — confirm `libavfilter` is NOT currently linked (`cmake/Dependencies.cmake`) #risk-medium
      `Dependencies.cmake:142` links avcodec/avformat/avutil/swscale/swresample only. Burn-in
      currently relies on the `burnInBlockedReason` gate in the UI; adding a post-pass must
      degrade to "no post pass" on a stripped distro rather than failing to launch.
- [ ] T0115 Song/preset import must work with **no Suno account** (`docs/PIVOT_PLAN.md` direction) #feature
      The renderer is the defensible half of the product; a local-file path is the cheapest
      insurance against the upstream client disappearing, and it is a prerequisite for
      batch rendering. Currently the download/import surface is Suno-shaped only.
- [ ] T0116 A render-job model and queue exists but has no producer (`src/visualizer/projectm/Bridge.hpp:59-68`, `src/suno/SunoWorkspace.hpp`) #feature
      `SunoWorkspace` already models `renderMode`/`renderDuration` with `renderProgress`/
      `renderCompleted` signals that nothing emits. `DownloadQueue` is an already-tested
      template for the same machine (bounded concurrency, backoff, atomic `.part` rename,
      cancel, idle signal).

- [ ] T0197 Nothing re-applies the theme when `ui().theme` changes from somewhere other than the Appearance panel (`src/qml/main.qml`) #risk-med
      `main.qml`'s `applyTheme()` runs **once**, in `Component.onCompleted`. Every path a user can reach today is covered — Settings previews live and Apply commits, and the commit value equals what is already on screen — so nothing is broken. The gap opens the moment a second writer appears: a profile switch, a config reload, or T0193's `ThemeCatalog` adopting a theme on disk. Then `config.toml` and the window disagree, silently, which is the exact failure T0047 was filed for. **The fix is small but not free, which is why it is here rather than done:** a `Connections { target: SettingsBridge; onThemeNameChanged: … }` must *not* apply unconditionally, because T0192 makes `--theme` a session override that is supposed to outrank the stored value — an unconditional re-apply would stomp the override the instant the user commits anything in Settings. The rule to encode is "re-apply on `themeNameChanged` **unless** a session override is active", and that rule needs eyes on it (MT-001-G) rather than a green ctest.

## P3 — polish and speculative

- [ ] T0117 Localization: zero `qsTr` across 58 QML files, ~200 user-visible literals (#feature) #cod-high
      Wiring is `qt_add_translations(project_lib SOURCE_TARGETS … RESOURCE_PREFIX /qt/qml/ChadVis/i18n
      TS_FILE_BASE qml)` plus `engine.retranslate()`. **Plurals are
      `qsTr(sourceText, disambiguation, n)` with `%n`** — the second argument is a comment,
      not the plural form — and every plural form must be filled even when identical. Add
      `TranslationFunctionMismatch=error` so `qsTr` and `qsTrId` are never mixed.
- [ ] T0118 A dark/light theme pair bound to `Application.styleHints.colorScheme` (`src/qml/styles/Theme.qml`) #feature
      `colorScheme()` is on `QStyleHints` since **Qt 6.8**; `Qt.Unknown` must be handled.
      `Theme.applyBackground` (:309-316) recomputes only `background…surfaceOverlay`, leaving
      `border`/`borderLight`/`textDisabled` `readonly` at dark values, so a light accent hex
      yields 1.0–1.4:1 text. The three `.qss` files are dead (T0052) so there is no light path.
- [ ] T0119 Fix the accessible-contrast failures for real (`src/qml/styles/Theme.qml`) #feature
      Raise `textDisabled`, `recording`, `textSecondary` and `error` to ≥4.5:1, and give text
      over the projectM canvas a real scrim (`glassBackground` is only 85 % opaque) or move
      it onto `surfaceRaised`. Do **not** touch `textPrimaryVariant`.
- [ ] T0120 A waveform lyric-timing correction editor (`src/qml/`) #feature
      The repo can render word timings (`LyricsData::words` carries per-word
      `startTime`/`endTime`) but timings are write-once from a capture — there are zero
      `waveform` hits in `src/qml/`. Click-a-character-to-pin, drag-to-reflow, full undo.
      No ML. This is the missing half of karaoke and the highest-value retention feature.
- [ ] T0121 Karaoke render strategies as swappable producers (`src/qml/`) #feature
      Wipe / bouncing-ball / scroll / dual-line as an enum plus one function each — the only
      way one strategy can serve both the live overlay and the FFmpeg burn-in pass.
- [ ] T0122 A render receipt, shipped **with** the job model (`src/recorder/RenderJob.hpp:129`) #feature
      Nothing persists "this output was 1800 frames at 60 fps from these presets", so "render
      it at 4K instead" means re-rendering three minutes in real time. A versioned sidecar
      (audio path + hash, scene list, karaoke source, encoder profile, projectM version)
      makes iteration cheap. Design it as a **public schema with a version field from the first
      byte** — the first schema wins forever.
- [ ] T0123 A cheap proof render before committing to the encode (`src/recorder/EncoderSettings.cpp`) #feature
      A 6-second, quarter-res, no-audio proof costs 6 seconds. This is the correct answer to
      "watch the render live" — prove it cheap first.
- [ ] T0124 Wire up the eight dead encoder presets (`src/recorder/EncoderSettings.cpp`) #feature #cleanup
      The project history says 8 static presets exist with **zero callers** — dead code that
      is obviously a UI feature. 9:16 / 4:5 / 1:1 are the interesting additions.
      *Risk:* projectM's aspect correction has never been exercised at 9:16.
- [ ] T0125 Poster-frame export (`src/visualizer/RenderTarget.cpp`, `src/recorder/FrameGrabber.cpp`) #feature
      `RenderTarget` already reads pixels out of framebuffer 0 and `FrameGrabber::flipImage`
      is the exact row-flip a `QImage` needs. Ship "frame at N seconds"; do not pretend it is
      scene detection.
- [ ] T0126 Cut on real downbeats (`src/suno/SunoEndpoints.hpp:60`) #feature #blocks-capture
      `POST /api/gen/{id}/downbeats_streaming/v2` is captured and `declared-unused`, so a
      server-computed beat source already exists. Prefer it to a local detector and cache per
      clip. Availability and rate-limit behaviour are **not** established.
- [ ] T0127 Cut on real frame pacing from the timestamps already in hand (`src/recorder/`) #risk-high
      The data is captured and discarded. Cheap now, and every downstream feature
      (batch, templates, concat) inherits a correct timeline instead of compounding drift.
- [ ] T0128 Library sort, filter, multi-select and a context menu (`src/qml/views/LibraryView.qml`) #feature
      Local text search only; no sort, no multi-select, no bulk action; `ClipCard.qml` has
      exactly one `onClicked`. **This is the difference between a viewer and something you can
      organise a library with.** The remote-verb half is capture-gated; the local half is not.
- [ ] T0320 **No like/dislike anywhere, and the rest of the Suno surface has no UI entry point** (`src/qml/views/LibraryView.qml`, `src/qml/views/ListenView.qml`, `src/suno/SunoLibraryMutations.*`) #feature #blocks-ui
      The user's request, and the shape of the gap: `is_liked` is parsed, persisted and filterable in the feed request, and the UI has **no affordance to set it** — not on a Library card, not in the detail sheet, not on the Listen view. The verbs themselves are capture-gated (`T0267`: no captured like/unlike route; `T0011`: 11 mutation verbs exist in `SunoLibraryMutations` but the switch is off and no QML connects `clipMutationSettled`/`clipSaveRefused`), so the honest split is: **(a) now, no capture needed** — a thumb control on `ClipCard`, `ClipDetailSheet` and `ListenView` bound to `is_liked`, disabled with a reason while the verb is unavailable, plus the local optimistic state and a "queued for when mutations are enabled" path; **(b) one authenticated capture** — like/unlike, trash/restore, add-to-playlist, follow (T0267's ~30-minute session), then flip `SunoLibraryMutations::setEnabled` and connect the four signals. This is the visible half of a much larger list already on the board and deliberately not duplicated here: playlists read-back and CRUD (T0264), search/filters/FTS (T0270), stems (T0261), personas (T0263), video (T0265), export (T0271, T0275), batch ops (T0269), per-item notification actions (T0273), credits/quota UX (T0277), and the generation surface (T0259, T0315). Read T0011 first: the missing QML layer, not the missing C++, is what keeps all of it unreachable.

- [?] T0321 **Playback controls exist only inside the Listen view — there is no global transport for the current track** (`src/qml/components/TransportBar.qml`, `src/qml/main.qml`, `src/qml/views/ListenView.qml`) #feature #blocks-ui
      `TransportBar` is instantiated in exactly one place (`ListenView.qml:145`), so from the Library, Discover, Notifications, Playlist or Settings the user can start a track but has no visible way to pause it, skip it or scrub it. The keyboard shortcuts exist (`main.qml:560-575`: space/next/prev) but are invisible and undocumented in the UI (T0240 lists the rest). **One transport, not two:** a persistent bar in the shell bound to `AudioBridge`, showing now-playing title/artist and elapsed/total, with play/pause, previous/next, seek, volume/mute, and a queue affordance. It must operate on the **current track whether it came from a remote Suno clip or a local file** — the two are not different players, they are playlist entries whose `PlaylistItem` metadata came from different sources (`remote clip | local file`, T0313), so the bar reads from the playlist's current item and never from a Suno-specific bridge. Disabled states when the queue is empty; the Listen view's copy collapses into the same component rather than staying a second implementation.
      **Written.** The bar is docked in the shell below every view, tracks `navRail.right`, reclaims the strip in fullscreen, and collapses its title/volume detail under 1000 px through the existing `compact` property. It is entirely `AudioBridge`-bound, so a local file and a downloaded clip behave identically, and the transport cluster plus seek disable when `currentTrack.title` is empty while volume and *open audio file…* stay usable. The Listen copy is deleted. `TestQmlStartup` loads the new tree and `scripts/lint-qml.sh` reports no blocking problems; placement, hover/press feel and the bar over each view are MT-001-D4.3. **Still open in the item's own list:** the queue affordance (the queue lives in Listen, one nav click away) and a mute-specific control (volume-to-zero exists). **Record is now on the bar too** (2026-10-26): the capture control had no home outside Video → dock → Record, so a playing track had no visible way to be recorded; the bar carries it, shows elapsed time while running, and surfaces a refusal inline. The paired guard is in `RecordingBridge::startRecording` — a visualizer window that is not exposed is refused by name rather than recorded as frozen frames. Whether a click from the Library should also *switch* to Video is a design call, not an oversight (MT-001-D4.3).
- [ ] T0129 ReplayGain presented honestly (`src/audio/LoudnessAnalysis.cpp`) #feature #retention
      The retention feature: it is what makes the app worth keeping open between render jobs,
      and the one thing that still works if every Suno surface dies.
- [ ] T0130 A `--diagnostics` report (`src/`) #feature
      Every number already exists: encoder fallback warnings, queue depths, drop counters,
      preset counts, the GL renderer string. **Secret handling is the entire risk** — the
      config holds token and cookie fields, so the collector needs an explicit allowlist
      rather than a deny-list, and must write only to a user-chosen path, never upload.
- [ ] T0131 DI over singletons; separate audio processing from the UI (`src/core/Config.hpp`, `src/audio/AudioEngine.cpp`) #blocks-feature
      Every global mutable singleton is enumerated in the audit; `Config`'s mutex is nominal
      only (T0022) and `RatingManager` blocks preset banks (T0058). The audio engine is not yet
      a headless library consumed via bridges.
- [ ] T0132 Consolidate the QML bridge singletons into one namespaced backend (`src/qml_bridge/`) #cleanup
      Twelve files, each with its own static back-pointer and registration entry.
- [ ] T0133 Modernise the deployment path (`cmake/Install.cmake`, `cmake/TargetSetup.cmake`) #cleanup
      Hand-rolled `macdeployqt` + `codesign` `POST_BUILD` steps predate
      `qt_generate_deploy_app_script` / `qt_deploy_runtime_dependencies`, and
      **`qt_finalize_executable` is deprecated** — use `qt_add_executable`/`qt_finalize_target`.
      Add `NSDocumentsFolderUsageDescription` since the app writes recordings.
- [ ] T0134 `macdeployqt` is a hard requirement of any default macOS configure (`cmake/TargetSetup.cmake:397-410`) #risk-medium
      A Qt installed without deployment tools cannot even **configure**, not merely deploy.
      This bites vcpkg/conda Qt users and is undocumented in the README.
- [ ] T0135 `qt_add_qml_module` and `QTP0004` (see T0051) plus a QML test harness (`tests/`) #risk-high
      `qmltestrunner` is still the right answer in 2026 and there is **no** QML test today.
      ⚠️ The module is declared on `project_lib` (a STATIC lib), so a test binary must link
      **`project_lib` AND `project_libplugin`** or every test fails with
      `module "ChadVis" is not installed`. Use `waitForPolish` (Qt 6.5+) to settle bindings
      before asserting, and set `QT_QPA_PLATFORM=offscreen` **per test**, not globally.
- [ ] T0136 A GPU CI lane, or an honest "no GL coverage" badge (`.github/workflows/`) #risk-high
      `integration_gl_tests`, `test_OffscreenRenderSpike` and the burn-in test all **skip on a
      headless runner and ctest counts a skip as success.** Today that is three subsystems at
      zero coverage reported as green. Either add a GPU runner or make the skip visible.
- [ ] T0137 Preset categories are always "Uncategorized" (`src/visualizer/PresetScanner.cpp`) #feature
      Infer from directory structure.
- [ ] T0138 Standard modern C++ where the tree still reaches for older idioms (`src/`) #cleanup
      `std::mdspan` for FFT, concepts on unconstrained templates, `std::array` for
      `CircularBuffer`, a configurable shuffle seed for deterministic tests, `std::variant`
      for lyric sources and CLI args, `std::jthread` in the audio pipeline.
- [ ] T0139 Hook up the off-hooks keys (`src/qml/main.qml`) #feature #risk-high
      `main.qml:511-545` documents a fullscreen design and `:598-602` binds shortcuts, but a
      whole family of documented bindings has no handler. **Grep every key in
      `panels/settings/ShortcutsSettings.qml` (9 entries) against `main.qml` and wire or
      delete the ones with no effect.**
- [-] T0140 Remove the stale dead-code comment in `ConfigLoader` and other drifted comments (`src/core/ConfigLoader.cpp:120-123`) #cleanup
      Reviewed and kept: the comment is not stale. It states why the code uses `operator[]` instead of the older `get("debug")->value_or(false)` — `toml::table_view::get()` returns `nullptr` for a missing key, so the old spelling dereferenced null on a `[general]` table without `debug`. That is the "why" the file's style asks for, not a claim of a live bug.

- [ ] T0188 The interaction language the design docs specify: hover-grow, 44×44 targets, responsive breakpoints (`src/qml/components/AppButton.qml`) #feature
      **The hover-grow half is written, and only that half.** `AppButton` now has three states — 1.0 idle, 1.02 hover, 0.96 press — at `durationFast` (150 ms) with OutCubic going up and InCubic coming down, replacing `pressed ? 0.97 : 1.0` at `durationInstant`. The press factor was *reduced* from 0.97 to 0.96 and the hover factor is small on purpose: three adjacent buttons at 1.05 would shimmer, and the docs' own "if animation doesn't help understanding, remove it" argues against it. The easing asymmetry is what makes it read as an object rather than a value readout. Both states are gated on `root.enabled`, which was a real bug in the first cut: `AppButton` deliberately leaves its `MouseArea` enabled when disabled so the cursor can say "not now", so `pressed` and `containsMouse` are both live on a dead button and it would have shrunk on click. **Still open:** tap targets are `buttonHeight 36` and nowhere near the 44 the design philosophy asks for, `AppIconButton` was not audited for the same press-only defect, no hover state exists on `AppComboBox`/`AppTextField`/`AppSwitch`/`AppSlider`, and there is still no mobile breakpoint or bottom nav bar. Those are a redesign, not a patch.
      `AppButton.qml:80` implements `scale: pressed ? 0.97 : 1.0` — **press-only, no hover-grow** — while the user's mockup (`~/Documents/qt-qml-responsive-desktop-mobile-esque-ui-mockup`, `Nsomnia/qt-qml-responsive-desktop-mobile-esque-ui-mockup`) specifies hover `1.02` / press `0.96` on `PrimaryButton` and `1.05` / `0.94` on `IconButton`, both 120 ms `OutCubic`, plus a 150 ms `ColorAnimation` on `Card.border.color`. The repo has hover-grow on `AppSlider` (:106) and `ClipCard` (:85) but **not on the primary button**, so the most-used control is the least alive. `Nsomnia/Resonance`'s `docs/inital-input-material/ui-ux/DESIGN_PHILOSOPHY.md` gives the governing rule — "mobile app sensibility on desktop", tap targets ≥44×44, and "if animation doesn't help understanding, remove it" — while `THEMING.md` gives the timing scale (fast 100–150, normal 200–300, slow 400–600 ms) that the current 50/150/250/400/600 ladder only half matches: `durationInstant` is 50 ms, below the fast floor. The mockup also breaks at 768 px with a `MobileNavBar`; the repo auto-collapses `NavRail` at `navRailAutoCollapseBelow` 1000 and has no bottom bar at all. Scale here is not decoration — it is the affordance T0119 is chasing.
      **The user's verdict was explicitly scoped: "theme wise".** Hover-grow is the only part of this item it can be called confirmed by, and the user did not comment on it. The remainder below stands: it is a redesign of the input controls and the window shell, not a patch to `AppButton`, and it should be sized as its own piece of work rather than continuing to live on this line.
- [ ] T0337 **ChadVis Dark's background is pure black and reads too dark on a real display** (`src/qml/styles/ThemeDefinitions.qml`, `scripts/check-theme-contrast.py`) #ui
      Reported 2026-10-25 while closing T0191; the other three palettes look right. Pick a near-black
      `background` and move `surface`/`surfaceRaised` with it far enough to stay a separate plane, then
      re-run `scripts/check-theme-contrast.py` — every gated pair must still clear 4.5:1 text and 3:1
      interactive colour. The palette is QML data, so the loop is edit → launch → look, no recompile.

## Refuted claims — do not re-investigate these

Recorded so they are not re-audited. Each was checked against the source, not assumed.

- **Items carried over from the superseded backlog that are ALREADY FIXED — do not re-file them.** Checked
  against the current tree during the 2026-10-07 merge: `AudioAnalyzer` is dead code no longer present (no
  matching file in `src/`; resolved in `e058eab`); `DownloadQueue` **does** check `write()`'s return and routes
  ENOSPC to `failItem(AbortCause::ShortWrite)` (`DownloadQueue.cpp:580-592`, with the rationale inline);
  `LyricsBridge::exportToAss` **does** have a QML caller (`RecordingPanel.qml:358`); `video_url`'s missing origin
  check is already a documented gate (`FeatureFlags.cpp:244`), not an undiscovered defect; and the "no keychain on
  Windows or Linux" claim is long stale — QtDBus Secret Service (`CredentialStore.cpp:25-83`) and Windows
  `CredRead`/`CredWrite` (`:214+`) are both implemented.
- **The `moc` raw-string trap is real but NOT currently triggered.** moc 6.11.1 cannot lex a raw string literal
  containing `//`: the preprocessor strips the comment without knowing it is inside a string, the raw string never
  closes, and moc emits a **0-byte `.moc`** plus `note: No relevant classes found`. The only symptom is an error
  at `#include "test_X.moc"` that **never names the offending line**. Measured against this tree's moc; no current
  `R"(…//…)"` literal triggers it. Workaround when it does: build fixtures from `QJsonObject` initialisers.
- **`LyricsSync`'s dead slots must STAY dead.** `onLineChanged`/`onWordChanged` are deliberately unconnected;
  wiring them creates a third writer of five position members. This is a trap-warning, not a defect.
- **There is no undeclared-source defect.** All 83 `src/**/*.cpp` and all 58 `.qml` files are declared in the
  CMake lists, `main.cpp` separately, and there are **zero dangling declarations**.
- **`textPrimaryVariant` has no contrast bug.** 6.53–9.26:1 against every background token (AAA). The comment at
  `Theme.qml:57-59` records the real past bug — the property was *missing*, causing an implicit `undefined`
  colour — and that is genuinely repaired.
- **Colour tokens are disciplined.** 38 hex literals tree-wide, 25 in `Theme.qml`. The inconsistency is in
  *control chrome* (58 raw Controls), not in colour.
- **Config has zero key drift.** Parser, `config/default.toml` and `docs/user/CONFIG.md` agree across every section,
  including nested `[recording.video]`/`[recording.audio]`. Only `[suno] token`/`cookie` are absent from the
  template, which is correct.
- **Config write is atomic** — temp → completeness check → close → chmod 0600 → `fsync` → atomic rename, with a
  Windows `MoveFileExW` fallback. A crash cannot truncate the live file. (Gap: the containing directory is never
  fsynced, so the rename itself may not survive power loss.)
- **Config round-trip is lossless by design**, with bounded documented loss. Unknown keys are dropped on save by
  design; the four historical casualties are now both tabulated and consumed; the only remaining drop is the two
  secrets, which is a **structural** guarantee.
- **Download no longer auto-plays on batch save.** `handleItemState:401` gates `addAndPlay` on
  `SaveAndPlay`; pinned by `saveOnlyBatchDoesNotTouchThePlaylist` and `aLiveRequestIsEscalatedNotDiscarded`.
- **There is no credential-exfiltration path.** `Authorization` is stamped only under
  `auth::isAllowedStudioApiUrl`; `Cookie` only on the hardcoded `AUTH_BASE`; Production-tier CDN/CloudFront/S3
  hosts **cannot** receive a bearer because `isAllowedStudioApiUrl` requires `Role::StudioApi`. No `LOG_*` call
  prints a token, cookie or `Authorization` header.
- **There is no TLS relaxation and no proxy path.** Zero hits for `ignoreSslErrors`, `sslErrors`, `setPeerVerify`,
  `QSslSocket`, `QNetworkProxy`. Redirects are `ManualRedirectPolicy` everywhere and never followed.
- **The OAuth lane is correctly gated and must not be "fixed".** `setCaptureApprovedLaunch` has no production
  caller; the gate is owned by `docs/suno_api/OAUTH_REDIRECT_ANALYSIS.md`.
- **Every non-idempotent POST already passes `retryOnUnauthorized=false`** — no mutation can be silently doubled.
- **401 retry is bounded to one and epoch-guarded** at four separate points.
- **Bounded bodies are enforced during the read, not after** — one byte past the cap into a bounded probe, never
  appended. `test_BoundedBody.cpp` even source-scans the tree for the `readAll()` misuse class.
- **projectM calls are all on the render thread** with the context current; cross-thread requests go through
  atomics + one mutex. The projectM-unavailable path degrades gracefully rather than crashing.
- **FBO creation is validated and the texture format agrees** (`GL_RGBA8`/`GL_RGBA` both ways — no RGB/RGBA
  mismatch), with alpha forced opaque before readback.
- **The Logger is thread-safe in steady state and its output is bounded** (rotating sink, 5 MB × 3).
- **`PresetScanner`'s `static const std::regex` is not a defect** (thread-safe function-local static).
- **No test hits the network, touches the real app-support DB, touches the keychain, or shells out to ffmpeg.**
- **`qmllint` never caught this project's QML bugs, and that is not a tooling failure** — it is a static analyzer
  over types and syntax with no knowledge of runtime values, lifetimes, or dynamic properties. Do not treat it as
  the verification gate for runtime behaviour; see T0048/T0049.
- **The separate-`QWindow`, own-context, render-to-FBO-0 projectM architecture is CORRECT — do not "fix" it.**
  Maintainer `kblaschke`, projectM discussion #820 (2024-06-22), verbatim: *"I was never successful in getting Qt
  and projectM to work well together... some objects projectM allocates in OpenGL (VBOs, FBOs and textures)
  randomly vanish after the first frame... **I've never seen this behaviour with any other OpenGL integration —
  just in Qt.**"* and *"**The only way of getting projectM to work is using a plain `QOpenGLWindow`, and drawing
  directly into the native surface.**"* Do not migrate to `QOpenGLWidget` or share the Qt Quick scene-graph
  context. This is exactly the arrangement `VisualizerWindow` already uses.
- **All projectM calls must be on the thread that created the instance and its GL context** — this is the official
  documented rule, so the current single-render-thread design is right. Two *separate* instances in two threads are
  safe; touching *one* instance from two threads is not. Caveat worth knowing: the GL resolver is a process-global
  singleton, not per-instance, so two threads only work if their contexts are compatible. Audio ingress is
  explicitly **not** mutex-protected upstream, so feed PCM *between* frames, never during `render_frame`.
- **`projectm_set_frame_duration()` does not exist** in any version — not 4.1.6, 4.1.7, 4.1.8, not master. Do not
  plan against it. The time-override entry point is `projectm_set_frame_time()`, and it is `@since 4.2.0` —
  **master-only and unreleased**, which is what makes offline rendering a pin-policy question rather than a task.

## Capture-gated — ship the gate, not the blocker

These are engineering tasks with a stated dependency, not waiting items. Do not read `[!]` here as "cannot
proceed"; read it as "proceed, and leave the switch off".

- [ ] T0141 **Clerk route selection — `tokens` vs `touch`** (`docs/suno_api/ENDPOINT-INVENTORY.md`) #blocks-capture
      Both `POST .../tokens` and `POST .../touch` are `[T1]`-observed; requiredness and preference order are not
      established, and the 2026-08-25 capture exercised only `touch`. Ship `touch`-first with a 401-retry fallback
      and keep the preference configurable. **The largest `[T1]`-without-code gap in the inventory.**
- [ ] T0142 **Error and rate-limit envelope handling** (`src/suno/`) #risk-high
      No reviewed request contains a rate-limit or error envelope shape. This is one parser plus one UI state
      needed for **every** route regardless of which routes exist — the broadest capture win.
- [ ] T0143 **Library mutation success-response shapes** (`src/suno/SunoLibraryMutations.cpp`) #blocks-capture #data-loss
      Request schemas were recovered; the **success** shape of every mutation verb was not, which is why the
      surface stays disabled (T0011). A 2xx must be treated as "accepted, shape unverified" — see T0018 for the
      case where the code gets this wrong.
      **Broadly captured 2026-10-26** ([§5.13](docs/suno_api/ENDPOINT-INVENTORY.md)), response side:
      `update_reaction_type` (LIKE → `"L"`, DISLIKE → `"D"`, null → **no** `reaction_type`, and `clip` is an
      id), `toggle_remixes` / `toggle_show_remixes` (`{clip_id, can_remix|show_remix}`), `set_visibility`
      (the whole updated clip), and the playlist verbs — `create`, `set_metadata`, `tracks/add`,
      `tracks/remove`, `reorder-by-index`, `trash` (flat `{playlist_id}` → **204 empty**). Traps worth keeping:
      `trash`'s 422 names `spec.playlist_id` for a body schema it does not use, `tracks/add` reports
      `{successes, failures}` under a 200, and a cover image is an *image* id (a clip id is a clean
      `404 cover_source_not_found`). Still uncaptured: `gen/trash` and the image-id space.
- [ ] T0144 **Fresh notification captures** (`src/suno/SunoNotificationService.cpp`) #blocks-capture
      Decides whether the three wired notification routes are real or a 404 generator — the one open risk in the
      notification surface.
- [ ] T0145 **Loopback acceptance capture** (`docs/suno_api/OAUTH_REDIRECT_ANALYSIS.md`) #blocks-capture
      The only capture that can unblock a P0-class gate; **a rejection is an equally valuable negative** that would
      settle the native-sign-in question permanently. Before it, T0011's offline scaffold gap
      (`beginGoogleSignIn` still refuses because `launch_` is `std::nullopt`) is worth closing so flipping the gate
      is a one-line change.
- [ ] T0146 **New host leads need a capture before they are wired** (`src/suno/CapturedHosts.cpp`) #security
      `content.suno.run`, `cdn3.suno.ai`, `suno.sng.link`, plus a CloudFront allowlist and an `audiopipe-dev`
      staging origin. **Record as deliberately excluded, do not wire:** the money-moving and consent routes,
      `/b-side/*` (77 staff routes including `impersonate` — disclosure material only), contest downloads, and
      `/labs/*`.
- [ ] T0147 **Keep excluded on purpose** (`src/suno/`) #security
      `ORCHESTRATOR_CHAT`, `ORCHESTRATOR_HISTORY`, `MODAL_BASE` and the `MODAL_BASE` host refusal
      (`CapturedHosts.cpp:444-446`) are intentionally unreferenced. Do not wire them and do not delete them
      without a decision.
- [ ] T0148 **Capture hygiene before any capture** (`docs/suno_api/raw/README.md`) #security
      Use Burp or mitmproxy and **export HAR** — do not retain a vendor session DB. Disable active scanning.
      Sanitize at ingest: keep hostnames, route paths, field *names*, status codes and header *names*; strip
      values, tokens, cookies, account/clip ids and complete media URLs.
- [ ] T0174 **The 2026-09-24 capture corpus is gone and its SHA-256 manifest cannot be verified** (`docs/suno_api/raw/`) #risk-high
      The published directory now holds a **0-byte** placeholder, three Burp XMLs have zero
      `<item>` elements, and the surviving `auth.suno.com` artifact hashes differently from the
      manifest entry. **This is not a reason to demote any `[T1]`** — a `[T1]` records what was
      observed, not what is still on disk. But the manifest is the only record of *what was
      reviewed*, so **do not delete it**; record the on-disk state alongside it instead. This
      was invisible to a fresh source audit by construction: no code change can surface it.
- [ ] T0175 **A `[REGISTERED]` evidence grade is needed, distinct from `[T1]`** (`docs/suno_api/`) #risk-medium
      Roughly 62 endpoints are *proved to exist* by a 401 response, but a 401 observes **no
      request and no response body**. Three axes — existence, contract, entitlement — are
      currently conflated under one grade, and `Implemented` is a second axis on top. Splitting
      existence from contract is what lets the inventory say honestly "we know this route is
      real and we know nothing about its shape." Sits directly under AGENTS.md §1.
- [ ] T0176 **Credential material lives outside the repository in three places** (`~`, not the repo) #security
      **2026-10-24 update: a fourth.** The active-investigation directory's
      `app-endpoints.md` embeds a plaintext Clerk handshake JWT and a playlist id inline, and
      the HARs beside it carry full session cookies. That directory is outside the worktree, so
      no `.gitignore` covers it, and it is exactly the kind of file someone pastes into `docs/`
      "for reference". Read it, cite the route names, never copy the file.
      A live `__session` cookie file, a ZAP log containing `Authorization: Bearer`, and a
      directory of HAR exports. The repo's secret rules govern what is *committed*; they do not
      reach files outside the worktree, so this needs a standing operating rule rather than a
      `.gitignore` entry. Confirm what still exists on this machine before acting.
- [ ] T0177 **Nine `[T1]` route spellings have drifted and should be re-probed** (`docs/suno_api/ENDPOINT-INVENTORY.md`) #risk-medium
      This is the **opposite direction from T0069**: where T0069 records routes the code sends
      that the inventory under-claims, this records routes the inventory *over*-claims as
      captured when the spelling no longer resolves. Caveat: a static negative is weak evidence —
      re-probe before demoting anything.

## Human decisions — not tasks

Phrased as forks with real cost on one side. None should be resolved by an agent inferring intent from the code.

- **Generation / Turnstile.** *Never solve it — hand off to the browser and import the result* (recommended) ·
  *embedded-webview managed solve* · *drop the Create surface entirely.* Option 1 costs an afternoon; option 2 is a
  terms-of-service exposure; option 3 costs the "Suno client" label.
- **Third-party client posture.** Ship as explicitly unofficial and unaffiliated, with no redistribution of audio,
  no money-moving routes, no legal/privacy-consent writes (recommended) · *something more assertive.* A legal
  judgement only the user can make, and it constrains packaging and marketing permanently.
- **Auth: one more capture session, or manual credentials forever?** Spend a few hours on a scoped authenticated
  capture to settle Clerk loopback acceptance, sign-out, and `touch` vs `tokens` (recommended — it unblocks native
  sign-in permanently) · *accept manual credential paste as the shipping 1.1.x path.*
- **The scope call.** Is the client accepted as **download manager + library view + account**, with the renderer as
  the product (recommended)? This is *the* decision, because it determines which half gets the next six months.
- **Windows in CI.** Add `windows-latest` as `continue-on-error` to collect signal (recommended) · *omit Windows.*
  Windows is the major userbase and its credential storage is a plaintext file, so silence there is the worst option.
- **The projectM pin policy.** Stay on 4.1.6 and ship realtime-throttled v1 · *pin an unreleased master SHA for
  offline render support now.* The first is free; the second buys offline rendering at the cost of a permanent
  upgrade obligation.
- **Burn-in dependency policy.** Add `libavfilter` + `libass` as a hard dependency · *add them behind a CMake
  option that degrades to "no post pass"* (recommended) so a stripped distro can still launch the app.

## Verification bar

A task is not complete from a stale binary, a declaration, a scan string, or a historical commit.

1. `./build.sh --fast --tests` builds the test targets; the app itself is not needed for most work.
2. `ctest --test-dir build-fast/tests --output-on-failure -j"$(sysctl -n hw.ncpu)"`.
   **Not** `--test-dir build` — that directory has no `CTestTestfile.cmake`, discovers zero tests, and exits 0.
3. Record the exact command and its result as a `VERIFY` line in `STATUS/<id>.log`, then flip `[?]` → `[x]`.
   `task.sh audit` fails the repo if an `[x]` has no `VERIFY` line.
4. Sanitizers before merge for anything touching the recorder or session threads:
   `./build.sh --tsan --tests && ./build.sh --tsan --safe`, and the `--asan` equivalents.
5. **A green run is not evidence for GL, burn-in, or audio.** Those tests skip on a headless runner and ctest
   counts a skip as success. Anything in those subsystems needs a real runtime observation or a human on a screen.
6. Record user-visible or behavioural outcomes in [`CHANGELOG.md`](CHANGELOG.md).

## Additions from 2026-10-18 agent-memory parse

Source: full-repo agent-memory parse at clone HEAD `4e8deab` (2026-10-18).
Entries compressed to board grammar; each entry's fix is summarized inline.
Skipped as duplicates of already-tracked items: poster-frame export (T0125),
library sort/filter UI (T0128), notification polling (T0067 — new polling entry
below is scoped to Explore + Account), resampler rate-conversion detail (T0153),
`pixel_format` / `fromConfig()` detail (T0171), `encodeAudio` O(n²) (T0162).

### P0 — the product is broken, data is corrupted, or a secret is exposed

- [?] T0201 **Mutations `Rejected` unreachable** (`src/suno/SunoLibraryMutations.cpp`) #risk-high
      `transportReason()` never returns empty, so every non-2xx settles as `Failed` (retryable) and server `detail` is never delivered. Fix: check HTTP status first, `Rejected` for 4xx with `rejectionReason(body)`, `Failed` only for transport/no-status.
      **Code written, `[?]` on purpose: nothing exercises it.** `handleReply` is private, the class takes no reply factory, and `tests/unit/suno/` still has no `FakeNetworkAccessManager` (T0099) — so the fix is a branch reorder proven by reading and by the suite still linking, not by a failing-then-passing assertion. The first `[x]` needs that fake; the gap itself is T0100. Detail in `STATUS/T0201.log`.
- [?] T0202 **`SunoDownloader::embeddedSunoId` unchecked `fieldList()[1]`** (`src/suno/SunoDownloader.cpp`) #risk-high
      TXXX frame with description but no value → out-of-bounds read. Fix: guard `size() >= 2` (test helper already guards).
      **Guarded, and a malformed frame now `continue`s rather than returning, so a later well-formed `SUNO_ID` is still found.** `[?]` for the same reason as T0201: the function is private-static and only reachable through the reuse path, which needs a fixture mp3 with a value-less `TXXX` frame. `STATUS/T0202.log`.

### P1 — correctness / robustness

- [ ] T0204 **Wire or delete dead config keys** (`src/core/Config*.cpp`, `config/default.toml`)
      Five `[recording]` keys (`enabled`, `auto_record`, `record_entire_song`, `restart_track_on_record`, `stop_at_track_end`) + `[suno] embed_metadata` + `[overlay] enabled` — zero consumers each.
      **Deferred on purpose 2026-10-25.** Deleting these on the first pass would turn T0208's new unknown-key warning into noise: an existing user config written by this app carries the keys, and every launch would then report seven unknown keys. The keys stay parsed and round-tripped until a deprecation/migration path can remove them from `config.toml` first.
- [ ] T0212 **`FileUtils::writeText` weaker than `ConfigLoader::save`** (`src/core/FileUtils.cpp`)
      No fsync, no 0600, no Windows replace handling. Upgrade or document "not durable".
- [ ] T0213 **Bad-config `markDirty()` contradiction** (`src/core/ConfigLoader.cpp`)
      Corrupt `config.toml` marked dirty then overwritten on quit (signal path), contradicting "not overwritten / only on next settings save" comment. Decide one behavior, make comment match.
- [ ] T0214 **Overlay `position` partial-table asymmetry** (`src/core/ConfigParsers.cpp`)
      Missing `x`/`y` → `0.0`, missing table → `0.5,0.5`. Fix `parseVec2` default to `{0.5,0.5}` in overlay path.
- [ ] T0217 **Mutations races** (`src/suno/SunoLibraryMutations.cpp`) #risk-medium
      Unbounded concurrency, no per-subject serialisation, last-write-wins by COMPLETION order; no dedup/coalescing; gate/switch TOCTOU; `setEnabled(false)` mid-flight does not cancel; destroyed-mid-flight `QPointer` guard drops reply silently with no settlement + `inFlight_` leak; `handleReply` double-entry unguarded.
- [ ] T0218 **`SunoLibraryManager` races** (`src/suno/SunoLibraryManager.cpp`)
      `page>1` refresh path can leave sync never-settled if `hasMorePages_` false; manual `requestNextPage()` during 1.1 s auto-page timer races timer; `errorOccurred` fires for ANY client error while `isSyncing_`; second `refreshLibrary` silently dropped, no queueing.
- [ ] T0219 **`SunoLyricsManager` races** (`src/suno/SunoLyricsManager.cpp`)
      Jitter uses `rand()` (global, unseeded, not thread-safe); `needsReauth` clears queue but already-scheduled `QTimer::singleShot` callbacks still fire; progress accounting never resets (`totalLyricsToFetch_` only grows; in-flight items counted as processed).
- [ ] T0220 **`SunoExploreManager` races** (`src/suno/SunoExploreManager.cpp`)
      No dedup — repeated/overlapping cursor pages duplicate feeds/clips; `refresh()`/`authStateChanged` interplay can strand in-flight `reloadStoredCredentials` outcome.
- [ ] T0221 **Notifications: good list discarded, `markAllRead()` blocked** (`src/suno/SunoNotificationService.cpp`)
      List reply succeeds but badge reply fails → `failRefresh` discards good list; `markAllRead()` blocked whenever `loading_` true.
- [ ] T0222 **`SunoAccountManager`: duplicate refreshes, reply-time auth check** (`src/suno/SunoAccountManager.cpp`)
      No in-flight guard; handlers check `isAuthenticated()` at reply time, not request time. Also: handlers never check HTTP status.
- [ ] T0223 **`SunoController::directLyricsCache_` unbounded** (`src/suno/SunoController.cpp`)
      Grows one entry per fetched clip, never cleared.
- [ ] T0224 **DB error handling** (`src/suno/SunoDatabase.cpp`)
      `getAllClips` exec failure ignored → `ok(empty)`; `saveAlignedLyrics` to nonexistent id succeeds silently; `hasLyrics` exec failure → `false`; migration failures `LOG_ERROR`-only, init still succeeds.
- [ ] T0225 **`SunoWorkspace::workspaceId_` collision + non-atomic save** (`src/suno/SunoWorkspace.cpp`)
      MD5 of date-only string; two workspaces same day share id; `loadWorkspace` ignores stored id. Save is trunc + write, no temp-file/rename/fsync.
- [ ] T0226 **Wire or delete `SunoWorkspace`** (`src/suno/SunoWorkspace.cpp`)
      Never instantiated outside tests — entire local-creation surface unreachable in shipped app.
- [ ] T0227 **Wire or delete callerless subsystems** (`src/lyrics/LyricTiming.*`, `src/audio/LoudnessAnalysis.*`)
      `LyricTiming` (pins, reflow, nudges, undo, plausibility, `.chadtiming` TOML — no QML bridge, no UI, no `LyricsData` conversion) and `LoudnessAnalysis` (full BS.1770 implementation, no caller).
- [ ] T0228 **No lyric offset** (`src/lyrics/LyricsSync.*`, `qml/`)
      No global/per-track karaoke delay/offset config or UI, no sync calibration.
- [ ] T0229 **LRC line-level only** (`src/lyrics/LyricsAligner.cpp`)
      Multi-timestamp lines, `[offset:]`, `[ti/ar/al/by]` tags, A2 word-level tags unsupported; last LRC line gets fabricated `+5 s` end.
- [ ] T0230 **Karaoke rendering: no per-word timing for LRC/SRT, no ASS style config** (`src/lyrics/LyricsExport.cpp`)
      Words list empty → no karaoke sweep, only line highlight; ASS style/font/size hardcoded (Arial 60 @ 1920×1080 in `toAssDocument`); `KaraokeConfig` (font/colors/y-position) separate QML-side config not wired to ASS writer.
- [ ] T0231 **No crossfade; no true gapless** (`src/audio/AudioEngine.cpp`)
      `swapPlayers()` instant cut; preload reduces warm-up but no overlap/sample-accurate handoff; no gapless toggle/test.
- [ ] T0232 **No EQ/effects; no per-track gain/fade/mute; no resampler** (`src/audio/AudioEngine.cpp`)
      No EQ bands, bass/treble, balance, compressor, limiter, reverb, loudness button; no per-track gain, fade-in/out, mute state, volume persistence; 44.1 kHz sink PCM fed as-is; surround truncated; UInt8/Int32 sinks drop all audio.
- [ ] T0233 **Preset: no preview/editor/per-preset duration/hotkeys/beat signal/load-error** (`src/visualizer/`)
      No preset preview/thumbnail, preset editor, per-preset duration/transition override, preset hotkeys beyond Next/Prev, beat/BPM signal out, preset-load error signal (`loadPreset` returns void).
- [ ] T0234 **MilkDrop compat: none beyond extension** (`src/visualizer/PresetManager.cpp`)
      No `.milk` parsing/validation, version/preset-format check, warning for presets projectM v4 cannot render.
- [ ] T0235 **`PresetManager` O(n) copies/search; `playCount` write-only** (`src/visualizer/PresetManager.cpp`)
      `setFavorite`/`setBlacklisted` copy-on-write O(n) full-list copy per toggle; `search` O(n) per keystroke, no index; `selectByName` up to 3 linear passes; `playCount` incremented but never persisted/displayed/sorted/filtered/reset.
- [ ] T0236 **`RatingManager` singleton, unsynchronised, no auto-save; name-keyed collisions** (`src/visualizer/RatingManager.cpp`, `src/qml_bridge/PresetBridge.cpp`)
      Unsynchronised map, no auto-save; `setRating` rejects 0 so ratings never clearable from QML; name-keyed fav/blacklist/rating: same-stem presets in different folders collide; renaming file loses fav/blacklist/rating/play-count.
- [ ] T0237 **Settings coverage gaps (config exists, no UI)** (`qml/panels/settings/`)
      `[recording]` auto_record / record_entire_song / restart_track_on_record / stop_at_track_end / output_directory / default_filename / container, `[recording.video]` pixel_format / gop_size / b_frames, `[recording.audio]` sample_rate / channels, `[suno]` auto_download / save_lyrics / embed_metadata / download_format, `[visualizer]` force_preset / use_default_preset / hard_cut_sensitivity / texture_paths, `[karaoke]` font_family / font_size / bold / colors, keyboard editor, overlay enable/disable + per-element editor (currently JSON-only via OverlayPanel, `[overlay]` config orphaned — pick ONE store and delete other).
- [ ] T0238 **Duplication: palette block, two "current preset" sources, two overlay colour systems** (`qml/`)
      `main.qml` palette block vs `SettingsWindow.qml` palette block (identical 19-line mapping, hand-synced — extract to shared component/function); two "current preset" sources (`VisualizerBridge.currentPreset` vs `PresetBridge.currentPreset` — pick one source of truth); two overlay colour systems (OverlayPanel 12-colour palette vs Theme); `AppearancePage.qml:32` hardcodes theme names — generate from `Theme.availableThemeIds()`.
- [ ] T0239 **Accessibility** (`qml/`)
      `Accessible.*` in only 31 lines across 12 files. Missing: all `panels/settings/*` controls, `TransportBar`, `PresetsPanel` stars/buttons, `OverlayPanel`, `RecordingPanel`, `SunoPanel`, `VideoView` dock tabs, `Create`/`Discover`/`Notifications` views. `AppSwitch`/`AppSlider`/`AppComboBox`/`SettingSpinRow` have no accessibility hooks. No visible focus indicator audit; no font-size / UI-scale setting; no reduced-motion setting; no keyboard navigation into `GridView`/`ListView` delegates.
- [ ] T0240 **Keyboard shortcuts** (`qml/main.qml`)
      Implemented in `main.qml:560-665` + unconditional `F11`, `Escape` (fullscreen-only), hardcoded `M` (rail toggle). All disabled while `settingsWindow.visible`. Missing: mute/volume, next/prev view, open Settings, karaoke toggle, preset search focus. `M` undocumented (not in Shortcuts page, not in Config — hardcoded, unremappable, invisible). Shortcuts page lists only 7 Config keys; `F11`, `Escape`, `M` missing. No keybinding editor (`QKeySequenceEdit`-style capture + conflict check + persistence).
- [ ] T0241 **Theme gaps: no light theme, no per-token editor, no export** (`qml/theme/`)
      NO light theme (branching exists, no definition exercises it — light-mode ramp unproven); NO per-token custom editor (Phase 4): only accent + background editable; NO "Export as theme" button; `Theme.qml` fallback literals `#00bcd4`/`#1a1a1a` intentional, keep.
- [ ] T0242 **Chapters from `RenderJob.scenes[]`** (`src/recorder/`)
      Scenes ARE chapter data (preset name + duration) — emit `AVChapter` entries from scenes, opt-in via `[recording] chapters = true`.
- [ ] T0243 **Pause/resume recording: NO state, NO API** (`src/recorder/VideoRecorderCore.*`)
      `RecordingState` has no `Paused`. For a music-video tool, pause-on-track-change / resume is real workflow (cf. `restart_track_on_record`, `stop_at_track_end` keys that DO exist).
- [ ] T0244 **Disk-space preflight: none** (`src/recorder/`)
      No free-space check before 3-min render. `ENOSPC` classified Permanent only AFTER render fails. Preflight (`statvfs` × expected size estimate) in `VideoRecorder::start` / `RenderQueue::enqueue` would fail fast.
- [ ] T0245 **Audio-only / video-only toggles: NO config key** (`src/recorder/`)
      Missing codec → silent video-only fallback exists but accidental; no audio-without-video path. Explicit `[recording] audio_enabled / video_enabled` would make fallback intentional + testable.
- [ ] T0246 **Output filename tokens: `{date}`/`{time}` only** (`src/recorder/EncoderSettings.cpp`)
      Add `{title}`, `{artist}`, `{preset}`, `{codec}`, `{resolution}`. `formatDefaultFilename` is seam.
- [ ] T0247 **10-bit / HDR: `p010le` schema-legal but encoder-hardcoded YUV420P kills it** (`src/recorder/`)
      No HDR metadata (Mastering Display / Content Light Level) anywhere. Phase-2 (after T0171).
- [ ] T0248 **`CHADVIS_*_FIELDS` X-macros: 5 hand-handled keys + `general.debug` + overlay elements outside tables** (`src/core/Config*.cpp`, `tests/`)
      "Single source" claim is 90% true. Compile-time or test-time check that every struct member has a table entry; keep + extend `roundTripPreservesEveryField` for every new field.
- [ ] T0249 **Header-essay dedup** (`src/recorder/`, `docs/dev/TESTING.md`)
      macOS drawable finding narrated in full in `OffscreenRenderSpike.hpp`, `RenderQueue.hpp`, AND `RenderExecutor.hpp`. Keep essay in ONE home (spike header), replace other two with 3-line pointer + one number that matters locally (ceiling=1; writeFailed sticky). Same for `sws`/`swr`/`ENOSPC` lore across `VideoRecorderFFmpeg.hpp` + `RenderQueue.hpp` + `TESTING.md`. Do not strip rationale — strip repetition.
- [ ] T0250 **Rationale essays in headers** (`src/audio/LoudnessAnalysis.hpp`, `src/lyrics/LyricTiming.hpp`, `src/lyrics/LyricsExport.*`, `src/visualizer/Playlist.hpp`, `src/visualizer/PresetManager.hpp`, `src/audio/AudioEngine.hpp`, `src/lyrics/LyricsData.hpp`, `src/lyrics/LyricsSync.hpp`)
      `LoudnessAnalysis.hpp` + `LyricTiming.hpp` ~70–80% rationale essays: move rationale to `docs/dev/` companion, keep terse header. Same pattern smaller in the other headers. `LyricsExport` ASS rationale split across header + cpp — consolidate into header.
- [ ] T0251 **`SunoClient.cpp` / `ClerkAuthClient.cpp` / `CredentialStore.cpp` long explanatory comments** (`src/suno/`)
      Some 30–60 lines per decision, with full bug archaeology. Keep one canonical "decision log" per invariant in doc, trim call-site comments to one-line pointer.
- [ ] T0252 **TODO.md grammar: essays inside entries repeat code comments** (`TODO.md`)
      Rule: TODO entry = one-line problem + one-line fix + file:line + risk tag. Long archaeology moves to `docs/backlog/TXXXX.md` (one file per item, linked from TODO.md). Keep TODO.md as index, not journal.
- [ ] T0253 **No tests for `SunoAccountManager`, `SunoController`, `SunoLyrics`/`LyricsAligner`, `SunoOrchestrator`, `ClipResolver`, `generate`/`pollWavFile`** (`tests/`)
      All service tests are pure/static-function tests; no test exercises live or mocked network reply, `QNetworkReply`, or cross-service interaction (e.g. Controller lyrics fan-out, Bridge wiring).
- [ ] T0254 **No tests for `AudioEngine`, `AudioQueue`, `PresetPersistence`, `RatingManager`, `pm::Bridge`/`Engine`/`Playlist`, `VisualizerRenderer`, `VisualizerWindow`, `RenderTarget`, `LyricsSync`, `MediaMetadata`, `LyricsFactory`** (`tests/`)
- [ ] T0255 **No tests for SettingsBridge setters/auto-save/reset, PresetBridge search/category, OverlayBridge JSON round-trip, `pendingThemeId`, RecordingBridge codec persistence, KaraokeSettings glow** (`tests/`)
      PresetBridge search/category test would have caught dead search.
- [ ] T0256 **No tests for VP9/AV1/ProRes/FFV1 encode, HW accel, `RenderExecutor::pumpLoop`, crossfade, receipt `bFrames`/`videoBitrate`, metadata/faststart, `fromConfig()` `pixel_format`, `SunoLyricsManager`, `AudioFileDecoder` stereo→mono** (`tests/`)
- [ ] T0257 **CI: no leg with `CHADVIS_POSTPROCESS=OFF`, `CHADVIS_USE_SOXR_ENGINE=0`, FFmpeg 9.x vs 8.x matrix, or `TESTING.md` counts vs `ctest -N` check** (`.github/workflows/`)
      Code has `#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61,13,100)` branches — only CI's FFmpeg version exercised.
- [ ] T0258 **CI believes it tests burn-in but Ubuntu's libavfilter lacks `ass` filter** (`.github/workflows/`, `tests/`)
      Burn-in always reports unavailable there; test asserts *report*, not render, passes vacuously on Ubuntu. Add lane with libass-enabled libavfilter, or mark burn-in render test as known-skipped on Ubuntu.

### P3 — features

- [ ] T0259 **Generation submit/poll/complete: none** (`src/suno/`)
      NO `submitted → streaming → complete` polling, generation timeout, cancellation. Needs captcha-token flow + durable-processing contract captures first (human decision: never solve Turnstile — browser handoff + import). Hazard: `POST /api/generate/v2-web/` reportedly answers `200 {"status":"error","clips":[]}` after drawing credits — empty `clips[]` must not read as success.
- [ ] T0260 **Extend / remix creation: none** (`src/suno/`)
      NO `extend` endpoint/method anywhere; remix *relations* exist as endpoint constants but no caller implements remix/extend creation or listing. Upload `completed(uploadId)` has no downstream consumer.
- [ ] T0261 **Stem separation: no request, download, or stem-aware naming** (`src/suno/`)
      `isStem()` + feed `stem` filter + `metadata.type` exist, but no stem-split request, stem download, or stem-aware naming; stem metadata fields (`history`, `infillLyrics`) dropped by parser; `stems` deleted from `WorkspaceState`.
- [ ] T0262 **Cover: constant exists, no download/upload** (`src/suno/`)
      `PLAYLIST_V2_COVER_IMAGE` constant exists; no cover download/upload in downloader; `image_url` never fetched.
- [ ] T0263 **Personas: ABSENT** (`src/suno/`, `qml/`)
      No persona struct, endpoint, UI, or mention (except `is_persona_root` bool on clip).
- [ ] T0264 **Playlists sync / CRUD reads: ABSENT as reads** (`src/suno/`)
      No playlist model, fetch-list/get-tracks, create/delete/duplicate, local playlist cache/table, or reconciliation. Mutations can add/remove/reorder/rename/cover-image but never read back; Explore silently drops `type=="playlist"` items.
      **The read contract is captured (2026-10-26, read-only probe), so the blocking half is gone:**
      `GET /a/api/playlist/{id}/?page=N` returns the playlist object flat at top level with
      `playlist_clips[] = {clip, relative_index, created_at}`, 1-based pages and no `has_more` —
      an empty `playlist_clips` past the end is the end signal. The listing half is
      `unified/library`, whose `library_playlists` feed carries every owned playlist with
      `song_count`/`num_total_results` in one page of 10. What remains is the model, the cache and
      the UI — no capture needed.
- [ ] T0265 **Video: `video_url` parsed, no download or status polling** (`src/suno/`)
      `VIDEO_GENERATE_STATUS` / `VIDEO_GEN_PENDING_BATCHES` constants exist; no video download or status polling.
- [ ] T0266 **WAV: conversion + `pollWavFile` stubs; `downloadFormat==WAV` refused** (`src/suno/SunoDownloader.cpp`)
      `m4a-opus` download refused (pinned P1 defect) -- **the refusal is now specified**: the
      captured progressive body is AES-128-CTR encrypted and needs a content-rights call
      (T0317); the `m4a-opus` form is no longer "unsupported", it is "not yet decrypted".
- [ ] T0267 **Trash/restore and permanent delete: NO verb, NO endpoint** (`src/suno/`) (capture-gated)
      Deliberate — no captured contract; `POST /api/clips/delete/` 404'd. DB *stores* `is_liked`/`is_trashed`, so read side implies verbs that don't exist.
      **Like/unlike left this item on 2026-10-26: captured from the real UI, both directions.**
      `POST /a/api/gen/{id}/update_reaction_type/` takes `{"reaction":"LIKE","recommendation_metadata":{}}`
      and `{"reaction":null,...}` to clear; the 200 is
      `{play_count, skip_count, flagged, reaction_type, clip, updated_at}` with **`reaction_type: "L"`
      only on a like** and **`clip` as an id string, not the clip**. Contract and the three
      asymmetries: [ENDPOINT-INVENTORY §4.5](docs/suno_api/ENDPOINT-INVENTORY.md). Dislike is still
      uncaptured (the letter is presumably `"D"` — a guess). Implementing the verb is now a code
      task; the UI affordance is T0320's half (a).
- [ ] T0268 **Clip rename: NO clip-title rename verb** (`src/suno/`) (capture-gated)
      Only *playlist* rename via `setPlaylistMetadata`.
- [ ] T0269 **Batch ops: ONLY playlist add/remove accept batch (1–100 `clip_ids`)** (`src/suno/`)
      No batch visibility, batch feedback, batch share, batch download/delete/tag, bulk mark-read (only mark-*all*-read), or multi-subject atomic op. No chunking helper for >100 ids.
- [ ] T0270 **Filters / search: DB + FTS + search endpoint** (`src/suno/`)
      NO liked/trashed/public/status/type/model filter in Manager or DB API; `searchClips` 5-column substring only, no FTS, no pagination, wildcard-unsafe. No search endpoint/query in Explore or anywhere. Explore body cursor-only. (T0128 covers the UI side: sort/filter/multi-select/context menu.)
- [ ] T0271 **Export: none** (`src/suno/`)
      NO export (JSON/CSV/playlist file) of library, clips, or lyrics. No LRC export, no plain-text synced export, no user-triggered "export lyrics" action. Only automatic `.srt` sidecar on download + `.txt` metadata sidecar.
- [ ] T0272 **Download queue UX: NO pause, NO priority, NO persistence, NO user-facing resume** (`src/suno/`, `qml/`)
      Byte-zero restart only.
- [ ] T0273 **Notification actions: NO per-item read/delete/open** (`src/suno/`)
      `contentId` carried but never acted on; no pagination, no filter by type/read-state.
- [ ] T0274 **Multi-account / switching: ABSENT** (`src/suno/`)
      No sign-out/logout method.
- [ ] T0275 **Lyrics export: ABSENT as user feature** (`src/suno/`)
      Only automatic `.srt` sidecar.
- [ ] T0276 **Polling: NONE for Explore/Account** (`src/suno/`)
      All manual/event-driven. (T0067 covers the Notifications side.)
- [ ] T0277 **Credits/quota UX: read-only snapshot** (`src/suno/`, `qml/`)
      **Contract captured 2026-10-26** (§5.13): one authenticated `GET /billing/info/` carries
      `total_credits_left` (the site's "Credits Remaining"), `monthly_usage`/`monthly_limit` (period spend
      against the allowance), `download_usage` (the download allowance), `renews_on`/`period_end` (billing
      date) and `plan` (the tier). No UI reads any of it today.
      No low-credit warning, spend tracking, purchase/upgrade flow, refresh scheduling, per-feature cost model.
- [ ] T0278 **Missing endpoints to wire (with capture)** (`src/suno/`) (capture-gated)
      `POST /v1/client/verify` (heartbeat, 204), `GET /v1/environment`, `POST /v1/client/sign_ins`, social legs, `GET /v1/client/handshake`; `DELETE /v1/client` (remote sign-out) never observed → sign-out local-only; `[T1]`-but-absent constants (increment_play_count/v2, listen_milestone, notification/v2/clear-badge, onboarding/current|start, statsig/forked-onboarding, labs/configs, challenge/progress). Adding any requires constant + call site + test.
- [ ] T0279 **Feature-flag visibility: none** (`src/suno/`, `qml/`)
      Diagnostics panel from `GateResolver::evaluateAll()` (already deterministic) + raw `flags` map viewer + `configs.gen-endpoint` display + per-gate "why" sentences.
- [ ] T0280 **Staging toggle: `TierPolicy::raiseTo(Staging)` exists, NO UI/config wiring** (`src/suno/`, `config/`)
      Add `suno.tier = "production"|"staging"` (explicit opt-in) with exact-match warning (raising to staging *narrows* to staging-only, prod becomes unreachable — surprising, must be surfaced).
- [ ] T0281 **Per-route toggles: `suno.enable_generation`, `suno.enable_mutations` as explicit opt-ins** (`src/suno/`, `config/`)
      Currently `SunoBridge` sets them; no user control. Atop `GateResolver` switches.
- [ ] T0282 **Observability: no structured auth-event log, queue-depth / in-flight diagnostics, last-`Retry-After`** (`src/suno/`)
      Log timestamps, route, status, failure kind — no secrets.
- [ ] T0283 **Auth diagnostics (no secrets): surface `AuthFailureKind` + last refusal + `exp` countdown + cookie age in Settings → Account** (`src/suno/`, `qml/`)
      All data already exists; only `authFailureKindChanged` exposed. Add "Test session" button = forced `GET /v1/client`.
- [ ] T0284 **Browser import options: `suno.import_browsers` order override, Firefox/Safari, "import from profile X"** (`src/suno/`, `config/`)
      "Which browser did it come from" already surfaced via `browserName`.
- [ ] T0285 **Credential hygiene: "Revoke / rotate", "export diagnostic (redacted)", "forget this browser import", per-profile keychain naming** (`src/suno/`, `qml/`) (revoke capture-gated)
      "Revoke" needs `DELETE /v1/client` capture.
- [ ] T0286 **`[general]`: `log_level`, `log_file`/`log_max_size`/`log_max_files`, `log_to_file`, `config_version`, `check_for_updates`** (`config/`, `src/core/`)
      Today: only bool log switch; log file hardcoded 5 MiB × 3.
- [ ] T0287 **`[audio]`: `volume`, `mute`, `crossfade_seconds`, `gapless`, `replaygain` / `normalization` / `loudness_target_lufs`, `output_channels`, `exclusive_mode`, `resample_quality`, `audio_delay_ms`, `balance`, `eq_*` / `eq_preset`, `device_refresh`, per-device profiles** (`config/`, `src/core/`, `src/audio/`)
      BS.1770 loudness analysis exists with NO config surface.
- [ ] T0288 **`[visualizer]`: `render_scale`, `vsync`, `fullscreen_on_start`, `window_geometry`, `preset_transition`, `preset_duration_min/max`, `preset_blacklist` / `preset_favorites` / `min_preset_rating`, `quality`, `idle_preset` / `idle_timeout`, `beat_sensitivity` per-preset override, `texture_quality`, `fps_display` vs `fps_render` split, `preset_transition_curve`, `preset_random_mode`, `preset_blacklist_enforced_in_rotation`, `texture_size`, `beat_sync_preset_switch`, `show_beat_indicator`** (`config/`, `src/core/`, `src/visualizer/`)
- [ ] T0289 **`[recording]`: filename-template placeholders, `split_by_track`, `audio_only`, `bitrate_mode`, `hardware_encoder`, `max_file_size`, `max_duration`, `burn_in_subtitles` toggle, `thumbnail`, `chapters`, `output_name_collision`, `record_fps`, `[recording.video] bitrate_kbps` + `maxrate`/`bufsize`, `[recording.video] hw_accel` + `hw_device`, `[recording] title_template` / `artist` + `av_dict_set` metadata, `[recording] audio_enabled / video_enabled`** (`config/`, `src/core/`, `src/recorder/`)
      Filename template today only `{date}`/`{time}`; add `{artist}`, `{title}`, `{album}`, `{preset}`, `{codec}`, `{resolution}`. `hardware_encoder`: `auto`/`nvenc`/`vaapi`/`software` — today raw codec string with no validation.
- [ ] T0290 **`[suno]`: `download_concurrency`, `preferred_quality` / `lossless`, `auto_download_filter`, `lyrics_format`, `download_filename_template`, `generation_model_default`, `generation_style_default`, `auto_like`, `history_retention_days`, `api_timeout` / `api_retries`, `cache_dir`, `offline_mode`, `download_lyrics_format`, `refresh_margin_secs` (300), `refresh_max_delay_secs` (3300), `staleness_cap_secs` (opt-in 1800), `force_refresh_on_start`, `request_timeout_secs`, `upload_timeout_secs`, `max_requests_per_sec` (1.0), `max_concurrent_requests` (1), `retry_401` (true), `retry_on_429` + `retry_after_max_secs`, `response_cap_mb`** (`config/`, `src/core/`, `src/suno/`)
      `lyrics_format`: `srt`/`lrc`/`ass` — writer exists per format, no selector. `retry_on_429` is unblocked: the client classifies 429/5xx itself now.
- [ ] T0291 **`[karaoke]`: `timing_offset_ms`, `outline_width`/`glow`/`blur`, `highlight_mode`, `lines_visible`, `font_fallback` list, `shadow_offset`, `animation` as validated enum, `karaoke_in_recording`, `offset_ms`, `ass_font`, `ass_font_size`, `word_sweep_enabled`** (`config/`, `src/core/`, `src/lyrics/`)
      `animation` today: string renderer ignores in favor of numeric index — string↔index mapping is actual bug. `karaoke_in_recording`: burn-in toggle; today overlays/karaoke never appear in recordings (T0170). `word_sweep_enabled` wires ASS `\kf` vs line highlight.
- [ ] T0292 **`[overlay]`: `global_opacity`, `default_font`, `safe_area`, `toggle_shortcut`, per-element `rotation`/`scale`/`blend_mode`** (`config/`, `src/core/`)
- [ ] T0293 **`[ui]`: `window_geometry`, `always_on_top`, `start_minimized`, `system_tray`, `language`/`locale`, `font_scale`, `notifications` toggle, `confirm_on_close`** (`config/`, `src/core/`)
- [ ] T0294 **`[keyboard]`: seek `±5s`/`±10s`, volume up/down, mute, shuffle toggle, repeat mode, preset-random, screenshot, stop-recording, lyrics toggle, overlay toggle, theme cycle, `random_preset`, `lock_preset`, `favorite_preset`, `blacklist_preset`, `rate_preset_up/down`, `seek_forward/back_seconds`, `lyrics_offset_up/down`, plus modifier-grammar validation + conflict detection** (`config/`, `src/core/`)
      Today any string accepted, duplicates silently collide.
- [ ] T0295 **New sections: `[performance]`, `[network]`, `[privacy]`, `[profiles]`, `[lyrics]`, `[presets]`** (`config/`, `src/core/`)
      `[performance]` (thread pool, GPU selection, memory caps), `[network]` (proxy, timeout, retry, user-agent override policy), `[privacy]` (telemetry/analytics switches), `[profiles]` (named config profiles — Settings has Profiles page but no config section), `[lyrics]` (provider preference, cache TTL, auto-fetch), `[presets]` (ratings/favorites/blacklist persistence — today split across `preset_state.txt` + ratings files, not config).
- [ ] T0296 **New CLI flags: `--list-devices`, `--list-presets`, `--dry-run`, `--log-level`, `--reset-config`, `--config-dir`, `--recording-fps`, `--recording-width/height`, `--volume`, `--seek`, `--theme-list`** (`src/core/CliArgs.inc`)
      `--dump-config` + unknown-key warnings landed with T0208; the rest close most "my config did nothing" reports.

### Candidates for removal (verify before deleting)

- `[recording] enabled`, `auto_record`, `record_entire_song`, `restart_track_on_record`, `stop_at_track_end` — zero consumers (see T0204).
- `[suno] embed_metadata`, `[overlay] enabled` — zero consumers (see T0204).
- `[ui] show_playlist`, `show_presets`, `show_debug_panel` — no QML reader found; wire or delete.
- `Config::addOverlayElement` / `removeOverlayElement` / `findOverlayElement` — zero callers; overlay state lives in `overlays.json` via `OverlayBridge`.
- `main.cpp` `g_app` — written twice, never read.
- `Logger::setLevel` — zero callers.
- `Result<T>` monadic surface + `TRY`/`TRY_VOID` — unused and non-compiling (see T0061).
- `VideoSettings::extraOptions`, `twoPass`; `EncoderSettings::title/artist/comment`; `RenderJob::twoPass` — never consumed/written; wire or delete.
- `Container::AVI` — compatibility trap; consider removing or pinning to H264+MP3/PCM.
- `EncoderPreset::Placebo` — keep in schema (frozen vocab), remove from UI preset list.
- `SESSION_CATALOG` alias, `PROMPTS` LEAD constant, `ORCHESTRATOR_*` LEAD constants, `CDN_CLIP_*` LEAD paths, `CLERK_API_VERSION` phantom comment, stub-behind code (`onGenerateReply` unreachable, `cancelledPolls_`, wav-poll machinery) — delete or gate.
- Callerless/duplicate QML + Suno members per T0071/T0060 (incl. `VisualizerWindow::feedAudio` stub, `Engine::addPCMData` duplicate, `RenderTarget::bind()/unbind()`, `LyricsSync::Config::lookaheadTime`, `SunoOrchestrator::onMessageFinished/onHistoryFinished`, `SunoClient::onGenerateReply/onWavConversionInitiated`, `cancelledPolls_`, `DownloadQueue::Item.metadata`, `SunoDownloader::tagAudioFile` FLAC branch, `ClipParser::selectImageUrl` fallback param, `parseClipArray` `expected` path, `SunoLibraryManager::clipUpdated` signal, `SunoDatabase::saveClip` single, `SunoLibraryManager::syncDatabase` rename-or-implement, `LyricsAligner::estimateTimings/parseLrc`, `SunoLyricsManager` "Lyrics processing:" branch, `SunoWorkspace` saved `workspace_id`/`version`, `SunoAudioUploadService` dead guards, `SunoController` debug-lyrics scaffolding).

## Additions from 2026-10-20 production sweep

Source: full sweep of `src/`, `docs/`, `.github/workflows/`, `cmake/`, `scripts/`, and
`.opencode/skills/` at `7c60fe4` (2026-10-20), plus the live GitHub Actions history. Every
claim below was re-checked against the tree or the run log, not carried from prose. Kept
terse per T0252; long evidence belongs in `docs/backlog/TXXXX.md`.

### P2 — production scaffolding and doc drift

- [ ] T0299 **Linux ships only TGZ/ZIP — no `.deb`, `.rpm`, AppImage, or Flatpak** (`cmake/Install.cmake:43`) #feature
      `CPACK_GENERATOR` is `TGZ;ZIP`, with `DragNDrop`/`NSIS` appended per platform. `scripts/PKGBUILD` exists but no CI job builds it and no distro package is emitted anywhere. Decide the supported formats, add the CPack generators, and add a packaging lane that installs the result.

- [ ] T0300 **Nothing measures coverage** (`.github/workflows/`, `CMakeLists.txt`, `cmake/`) #risk-med
      Zero hits for `gcov`, `llvm-cov`, `lcov`, or `coverage` outside comments. A green gate says the 14 suites pass, not what fraction of `src/` they execute. Add a `CHADVIS_COVERAGE` option and one reporting lane; the untested surfaces are already enumerated by T0100 and T0253–T0256.

- [ ] T0301 **No static-analysis, SBOM, or dependency-vulnerability lane** (`.github/workflows/`) #security
      No CodeQL, no CycloneDX/SPDX SBOM, no `vcpkg.json` (T0091), and Dependabot watches only `github-actions` (`.github/dependabot.yml:14-17`). The CPM-pinned and system dependencies have **no CVE signal at all**. Add CodeQL for C++ and an SBOM step; keep CPM pins on a manual review list until a manifest exists.

- [x] T0304 **`docs/GEMINI_FLASH_ANALYSYS.md` is an orphan with a misspelled filename** (`docs/`) #doc #cleanup
      Parked to `.backup_graveyard/2026-10-26-gemini-analysis/`: an unreviewed AI architecture analysis with no owner, on a subject `docs/dev/ARCHITECTURE.md` already owns. Nothing referenced it but this line; a rename would have made an unowned second copy look canonical.

- [ ] T0305 **Four overlapping C++ skills, two of them with unclear provenance** (`.opencode/skills/`) #cleanup
      `c-cpp-profi`, `modern-cpp`, `cpp-coding-standards` and `cpp-pro` all cover modern C++ standards/idioms. `c-cpp-profi` has no upstream license; `cpp-pro` has no license file either. Consolidate to one standards skill and one security/idioms skill, record the license for each, and delete the rest. Skill sprawl costs context on every task that loads them.

- [ ] T0309 **The browser-cookie import is macOS-only in code but platform-blind in the UI** (`src/suno/auth/BrowserCookieImport.cpp:223-260`, `tests/unit/suno/test_BrowserCookieImport.cpp`) #risk-med
      `deriveChromiumKey` and `decryptChromiumValue` are `#ifdef Q_OS_MACOS` and return `{}` everywhere else **by design**, and `candidateBrowserNames()` probes a hardcoded `~/Library/Application Support/`, so on Linux and Windows the Import affordance can only ever produce "No Suno cookies were found in …" — a message that blames the user's browser for a platform the code does not implement. Found the hard way: seven tests in `test_BrowserCookieImport.cpp` asserted against empty ciphertext and **failed the Linux gate** (run `39395268941`, `unit_tests` 53.99 s, six `FAIL!`). Those seven now `QSKIP` off macOS via `REQUIRE_MACOS_SAFE_STORAGE()` — the honest fix for the *test*, and deliberately not a fix for the product. Either implement the non-macOS paths (DPAPI on Windows, libsecret on Linux) or gate the affordance by platform with an explicit "not available on this OS" message; a third option, hiding the button off macOS, is the one that costs nothing and stops the misleading report.
- [?] T0322 **The idle preset renders but does not react to music — the PCM ingress into projectM is unverified, and likely not arriving** (`src/visualizer/VisualizerRenderer.cpp:88-106`, `src/audio/AudioQueue.hpp`, `src/core/Application.cpp:502`, `src/audio/VisualizerAudioTap.cpp`) #blocks-ui #risk-high
      User report 2026-10-22: "the default preset doesn't seem to react to music", while the same idle preset reacts in their older build. **Correction to the first filing of this item:** projectM v4 loads its own idle preset (`ProjectM.cpp:215`, `idle://Geiss & Sperl - Feedback (projectM idle HDR mix).milk`), so the canvas is NOT blank — it is a rendered frame that never moves. That rules out "no preset" as the cause of this report and puts the PCM hop under suspicion. The path is nominally wired — `Application.cpp:502` gives the renderer `&audioEngine_->audioQueue()`, `AudioEngine::processAudioBuffer` pushes the converted sink buffer via `pushAll`, and `VisualizerRenderer::renderFrame` pops `popVizBatch` and calls `projectM_pcm_add_float` — and it is the same pop-per-frame shape the pre-queue architecture used (`git show 6177f82:src/audio/AudioEngine.cpp` emitted `pcmReceived`, `VisualizerWindow::feedAudio` appended it, and the render frame fed `min(framesToFeed, available)`), so this is a wiring bug or a queue-identity bug, not a redesign. **Instrumented in this change:** one INFO when the first batch arrives (`VisualizerRenderer: audio is reaching projectM (… Hz, queue depth …)`), then at most one WARN per 10 s while it stays empty, carrying `vizDepth()`/`vizDropCount()`/rate. Read those three numbers before touching anything: depth 0 and dropped 0 means the producer never pushed (AudioEngine/QAudioBufferOutput); depth > 0 with drops means nothing is draining this queue; depth 0 while audio plays and the queue is the engine's means the renderer holds a different queue. Also confirm `renderFrame` runs at all by the FPS readout. Do not "fix" this by adding a second feed path.
      **The log answered it, and the answer was not a wiring bug: the producer never pushes, because the backend cannot.** The 10:46 run printed `600 frames rendered with no PCM in the visualizer queue (depth 0, dropped 0, rate 48000 Hz)` — depth 0 and dropped 0 is the "producer never pushed" branch, with the queue identity correct and the render loop running. Cause, from Qt's own `QAudioBufferOutput` page: **"QAudioBufferOutput is only supported with the FFmpeg backend."** macOS defaults to the darwin backend, and this Qt ships only `libdarwinmediaplugin.dylib`: `QT_MEDIA_BACKEND=ffmpeg` logs `Available backends QList("darwin")`, falls back, and a standalone 1.2 s tone probe emits **zero** `audioBufferReceived` signals while `playbackState == Playing`. So the visualizer has never received PCM on this machine with this Qt, and no amount of wiring would change it. **`VisualizerAudioTap` is the fallback:** when no buffer has arrived, the engine decodes the selected local file with `AudioFileDecoder` on a 40 ms GUI-thread tick, keeps ~400 ms ahead of `QMediaPlayer::position()`, re-seeks past a 1.5 s divergence, and pushes into the same `AudioQueue` projectM and the recorder already drain. `QMediaPlayer` remains the only output and the master clock; the first delivered buffer clears the tap, so the supported path is used where it exists. Pinned by `TestVisualizerAudioTap` (accounting, seek realignment, end-of-file, undecodable file) and `TestAudioFileDecoder::seekingRepositionsTheCursorAndTheResampler`, and end-to-end by `playingAnOnDiskClipStartsTheTransport`, which asserts a non-zero visualizer queue. **Still open:** the GL observation — that the canvas actually *moves* — is MT-001-D4.4 and only a human can mark it; the log line to look for is `VisualizerRenderer: audio is reaching projectM`.

- [ ] T0311 **The gate produces no binary and no release — an artifact exists only when someone pushes a `v*` tag** (`.github/workflows/build.yml:42-46,54-55`, `.github/workflows/release.yml:78-80`, `version.txt`) #feature #blocks-release
      `build.yml` triggers on `push: branches: [main]`, runs the 11-entry gate, and stops: `permissions: contents: read`, no `cpack`, no `upload-artifact`. A binary exists only when someone pushes a `v*` tag and `release.yml` runs. Wire the gate to a publish job: after the 11 entries pass, package the same build with `cpack`, then `gh release create` (or `gh release upload --clobber`) a **pre-release** versioned from `version.txt` plus the commit — `v<version>-build.<run_number>` or `v<version>+<short_sha>` — and never `latest`, so a CI build cannot masquerade as a cut release. Three constraints: (a) `contents: write` on the publish job only, never on the build matrix; (b) never publish from a red, cancelled, or skipped run — `needs:` the gate and read the **job** conclusion, since `continue-on-error` is exactly how the Windows lane reports green today (T0298); (c) the macOS `.app` is ad-hoc signed and not notarised (T0039), so the generated notes must keep saying so. Start with the Linux/macOS archives `cpack` already produces; add Windows once T0298's NSIS artifact exists, and follow T0299 for the Linux formats.
      **Partially delivered 2026-10-20: the artifact half is done, the publish half is not.** `build.yml` now runs `cmake --build --target package` and uploads TGZ/ZIP (+`.dmg` on macOS) as `chadvis-<os>-<compiler>-debug` on every push, and `windows.yml` uploads a `windeployqt`-deployed ZIP — so every lane now emits a runnable binary, which was the "no artifact at all" defect. Deliberately **not** wired to `gh release create`: publishing on every push is a policy call (constraint (a)/(b) above) and the version scheme is a human decision. `release.yml` still publishes only on `v*`, still Linux + macOS only; a Windows leg is waiting on T0298 going green.

## Additions from 2026-10-20 user request

Source: the user's own feature request. Every entry below is a **feature**, so they sit in
P3 unless a gate is named. Terse per T0252; the long form belongs in `docs/backlog/TXXXX.md`.

### P3 — features

- [ ] T0312 **QML's JavaScript as the UI logic layer: bindings, item functions, imported `.js` namespaces** (`src/qml/js/`, `src/qml/styles/Theme.qml`, `src/qml/main.qml`) #feature #cod-high
      54 QML files, **zero** `.js` files and **zero** `.js` imports, so every shared behaviour is a
      binding or a per-file copy of the same expression — the 19-line palette block is duplicated
      between `main.qml` and `SettingsWindow.qml` (T0238). Qt Quick documents exactly three JS
      affordances — expressions in bindings, functions on items, and `.js` files imported as
      namespaces (`qtquick-usecase-integratingjs.html`) — so the shared layer is a directory of
      plain `.js` files, one concern per file, imported as `import "js/theme.js" as ThemeMath`.
      The engine is **not** a browser runtime: no DOM, no `fetch`, no `require`, no module
      system (a `.js` file is a flat global namespace, hence one concern per file), and anything
      touching the network, the filesystem or a mutex stays in C++. There is no QML test harness
      yet (T0135), so the JS layer needs one before it grows.
- [ ] T0313 **One playlist of Suno clips and local files, with the format set owned by the Qt Multimedia plugins** (`src/audio/AudioEngine.cpp`, `src/audio/Playlist.cpp`, `src/qml/panels/PlaylistPanel.qml`) #feature
      Playback already runs on `QMediaPlayer` + `QAudioBufferOutput` + `QMediaDevices`
      (`AudioEngine.cpp:91,207,231`), so the *decoded* format set is whatever the FFmpeg backend
      ships — do not hand-roll a format table, and do not shell out to `ffmpeg` for a second
      decoder. What is missing is the **input** half: no capture device, no local-file import,
      and a playlist whose entries are Suno clip ids only. Fix: one source model where an entry
      is `remote clip | local file`, `QMediaPlayer` as the single playback path for both, and the
      unsupported-format case surfaced as a message rather than a silent no-op. Anchors: T0115
      (import must work with no Suno account), T0116 (a render-job model with no producer),
      T0264 (playlist reads absent as reads).
- [ ] T0314 **End-user theme creation and sharing** (`src/qml/panels/settings/AppearanceSettings.qml`, `src/qml/styles/ThemeDefinitions.qml`, `docs/user/CONFIG.md`) #feature
      The engine ships four built-ins and a C++ `ThemeCatalog` is being built (T0193), but there is
      no per-token editor, no light-theme definition, no export, no import, and therefore no
      sharing. T0241 owns the token editor and the export button; T0191 owns measured palettes.
      Sharing is **file-based**: a theme is one JSON file, exportable, importable by path, and
      copy-to-clipboard for a forum post — no server, no account. The gate is T0191's checker:
      `scripts/check-theme-contrast.py` re-implements the ramp and WCAG luminance, so a submitted
      theme that fails a gated pair is **refused**, not warned about — a shared theme is the one
      path by which an unmeasured palette reaches another user's screen. A light theme has to be a
      measured definition, not a branch the ramp happens to take. (after: T0193, T0241)
- [ ] T0315 **Mine the local recon corpus for suno.com's generation contract** (`docs/suno_api/ENDPOINT-INVENTORY.md`, `docs/suno_api/OBSERVED-LEADS.md`, `src/suno/SunoEndpoints.hpp`) #feature #blocks-capture
      `POST /api/generate/v2-web/` is already `[T1]`-captured — request keys (captcha token,
      generation type, prompt, model key, flags, metadata, cover/continue/artist/persona refs,
      client transaction id, lyrics-project linkage) and response keys (batch id, `clips[]`,
      batch/model/status metadata, creation time, batch size) are recorded in the inventory §5.4 —
      and it is still `declared-unused`, because the **web client's own current call sequence** is
      not in the repo's evidence and the captcha gate is unresolved. The corpora are on this
      machine: `~/Documents/suno-recon/` (Next.js bundles and source maps, `js-maps/`,
      `API-SPY-API-PROBE/`, `bin/`) and `~/Documents/suno-recon-cat-stealth-modekl/`
      (`captured_endpoints*.json`, `network-endpoints.txt`, `reports/api_paths_from_assets.txt`,
      `reports/feature_flags_client.txt`, `reports/recon_summary.md`, `out/`, `bin/*.py`). Answer
      four questions and write them down: which route the create page calls today
      (`configs.gen-endpoint` **names** it server-side — read the flag, do not hardcode the
      path), the model-list source, the captcha-token mint route, and the completion channel
      (`POST /api/feed/v3` and `GET /api/clips/get_songs_by_ids` are known; `GET /api/gen/{id}`
      stays a `[LEAD]`). The deliverable is graded rows plus a capture plan — **not** a client.
      Discipline: a string in a bundle is a **lead, never evidence** (AGENTS.md); every route
      still needs an authenticated capture before wiring (T0148 hygiene, T0146 hosts), and the
      generation route stays unwired until the captcha handoff is decided — the board's standing
      answer is *never solve Turnstile; hand off to the browser and import the result*. Distinct
      from T0259 (implementing the flow), T0142 (error envelopes) and T0143 (mutation shapes).
- [ ] T0316 **Advanced scene designer: a scripted "music video" stagecraft** (`src/recorder/RenderJob.hpp`, `src/qml/panels/OverlayPanel.qml`, `src/lyrics/LyricsExport.cpp`, `src/suno/SunoDownloader.cpp`) #feature #cod-high
      A scene today is `{presetName, durationSeconds, crossfadeSeconds}` (`RenderJob.hpp:240-244`)
      — a preset and a duration, nothing else. There is no textual element, no position, no
      colour, no per-time script, and the overlay JSON that `OverlayPanel` persists never reaches
      the recorded video (T0170 — the FFmpeg post-pass is mandatory, not optional). Lyrics
      structure and meta tags (`[Verse]`, `[Chorus]`, `<meta>`) are not parsed at all: zero hits
      in `src/lyrics/`. Fix, in four parts: (a) extend the scene model with typed elements (text,
      image, colour, position) and a time script — absolute seconds, track-relative, or percent of
      the track; (b) a small declarative script language for the scene graph, **evaluated by C++ at
      render time, never by QML at draw time**, so the burned-in video and the live preview cannot
      disagree; (c) a timeline editor UI over that model; (d) lyric tag filtering — a rule set of
      regex/wildcard patterns marking structure and meta tags keep/drop/rewrite, applied to
      Suno-supplied lyrics and to the ID3 `USLT`/`SYLT` frames `SunoDownloader::tagAudioFile`
      already writes, with the original text kept in the sidecar so nothing is lost. The scene
      inputs are the Suno clip metadata (title/artist/tags/prompt) and the ID3 frames (`TXXX`,
      `USLT`, `SYLT`) already written today. Anchors: T0242 (chapters from scenes), T0120
      (waveform timing editor), T0121 (karaoke render strategies), T0108 (libavfilter/libass
      burn-in), T0230 (ASS style hardcoded). One evaluator, two consumers — a second
      implementation is how the preview stops matching the file.

## Additions from the 2026-10-20 audit rerun

Source: the user-requested full-repo ponytail audit of 2026-10-20 (whole tree, 20 ranked
findings delivered as the audit report). Every finding maps to an already-open ID, so the pass
is a **confirmation pass, not a new-defect pass** — recorded here as a rank index over existing
items rather than 20 duplicate lines, per one-fact-one-document hygiene. Two suspected NEW
findings were checked and closed clean before writing anything:

- **The add_test census.** An unanchored grep counts 13 hits in `tests/unit/CMakeLists.txt`; the
  anchored count was 12 registered `add_test` lines + 2 in `tests/integration/CMakeLists.txt` =
  **14**, exactly the number T0303 closed with — no T0040/T0297-class recurrence. The 13th hit is
  a mid-line token, not a registration.
- **`test_PresetManager.cpp`** was absent from every registration surface (never compiled, never
  run) when this was written; T0306 registered it since.

The audit's Must group was closed in full (`T0200`–`T0216`, now in [`CHANGELOG.md`](CHANGELOG.md)).
What still guides work: Should group: **T0011+T0100 · T0058+T0059 · T0224+T0223+T0219 ·
T0067+T0221 · T0081+T0082+T0239 · T0072+T0086 · T0204 · T0298+T0311** (T0299, T0095
and T0039/T0111 ride the same release lane). Nice group: **T0063+T0064+T0238** (four tolerant-JSON
helper sets that disagree on `"True"`, three auth-vocabulary copies, two hand-synced palette
blocks, two current-preset sources) and the dead-code ledger **T0065+T0060+T0068+T0071+T0061+T0226+T0227+T0306**.
Smaller hits already tracked individually: T0088, T0230, T0244, T0079, T0110.

Lean upper bound, measured with `wc -l` over the delete candidates named on the board (several
are wire-or-delete decisions, not verdicts — see T0226/T0227/T0011): LyricTiming 2,042 +
LoudnessAnalysis 1,362 + SunoWorkspace 785 + SunoLibraryMutations 1,228 + SunoBridge mutation
forwarders ~950 + dead scripts 327 + SunoOrchestrator 103 + test_PresetManager 211 + dead
structs/members ~450 → **≈ 6.1k lines** possible. **-0 dependencies**: every third-party dep has
an in-tree consumer (spdlog, toml++, readerwriterqueue `AudioQueue.hpp:15`, projectM, FFmpeg,
Qt, TagLib). Foundations note: deleting LyricTiming/LoudnessAnalysis kills T0120/T0129's
substrate — those two are park-or-wire, not delete-to-today's-numbers.
