---
schema: 1
updated: 2026-10-07
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

**Size cap: 96 KB, and the board is at it.** A board you cannot read in one glance has
stopped being a board. The cap is enforced by `./scripts/task.sh audit`, which fails the repo
when it is exceeded. **Adding an item at the cap means displacing one.** Finished work moves
to [`CHANGELOG.md`](CHANGELOG.md) and the line is deleted — `git log -p TODO.md` is the archive.

---

## P0 — the product is broken, data is corrupted, or a secret is exposed

- [x] T0001 **The Settings window cannot open, so the app has no sign-in path** (`src/qml/main.qml:60`) #blocks-ui #risk-high
      `settingsWindowApi["open"]()` — `QQuickWindow` exposes `show()`, `raise()`,
      `requestActivate()`; there is no `open()`. Every `navigate("settings")` throws
      `TypeError: settingsWindowApi.open is not a function` (main.qml:45,60; reached from
      `NavRail.qml:25` and the account chip `main.qml:308-310`). Sign-in exists **only**
      behind that window (`SettingsWindow.qml:102` → `AccountPage.qml:516-524`), so
      neither Google sign-in nor manual cookie paste is reachable. Verify by launching and
      clicking Settings; the window must appear.
- [x] T0002 **Every preset click operates on preset #0 regardless of the row** (`src/qml/panels/PresetsPanel.qml:48,96`) #data-loss
      `PresetBridge::presetToVariant` (`PresetBridge.cpp:268-280`) emits exactly
      `name, path, author, category, favorite, blacklisted, playCount, rating` — **no
      `index` key** — while the panel passes `modelData.index`, which is `undefined`
      and is coerced to `0` by the `int` parameter. Verified by reading both sides.
      Fix: emit an `index` key, or use the already-existing `selectByName`
      (`PresetBridge.hpp:60`).
- [x] T0003 **Preset ratings can only ever be written to slot 0** (`src/qml/panels/PresetsPanel.qml:84-98`) #data-loss
      The star `Repeater { model: 5 }` makes `modelData` the **number** 0–4, so
      `modelData.index` at :96 is `undefined`; `setRating` then indexes
      `(*presets)[index].name`. Fix: pass the outer preset's name.
- [x] T0004 **Preset favourite/blacklist and overlay delete are impossible** (`src/qml/panels/PresetsPanel.qml:100-103`, `src/qml/panels/OverlayPanel.qml:302-315`) #blocks-ui
      A trailing full-bleed `MouseArea { anchors.fill: parent }` declared **last** is
      stacked on top of the per-item buttons and eats every click. `ClipCard.qml:299-300`
      documents this exact trap and is correct; these two are not. Fix: declare the
      delegate `MouseArea` before the controls, or use `z`/`acceptedButtons`.
- [x] T0005 **`isAuthFailure` treats any text containing "401" as an auth failure** (`src/suno/SunoAuthFailure.hpp:15`) #risk-high #blocks-ui
      `return errorMessage.contains("Unauthorized") || errorMessage.contains("401");`
      Called with `httpStatus == -1` at `SunoLyricsManager.cpp:96` (raw server text) and
      `SunoClient.cpp:1288`. A byte count, track id or path segment containing `401`
      flips the client to `NeedsReauth`, drops the bearer and emits `needsReauth()` —
      **a self-inflicted sign-out.** Fix: gate on `httpStatus == 401 || == 403` only.
      **Zero tests cover this function.**
- [ ] T0006 **A short download is renamed into place and reported `Completed`** (`src/suno/DownloadQueue.cpp:669-695`) #data-loss
      `finalizeSuccess` never compares `bytesReceived` against `Content-Length`, and
      `classifyFailure` returns `None` for any 2xx (:105). A connection closing at 90 %
      yields a plausible short MP3 that is renamed, tagged, sidecarred and announced via
      `fileSaved` (`SunoDownloader.cpp:395-406`) — precisely the failure this repo has
      shipped before. Fix: record `total` from `downloadProgress` and fail before rename.
- [ ] T0007 **Every retryable download failure leaks the `QNetworkReply`** (`src/suno/DownloadQueue.cpp:708-744`) #risk-high
      The retry branch returns without `deleteLater()` or clearing `item.reply`; only the
      terminal path does. `startItem` overwrites `item.reply` next attempt (:427), so up
      to `kMaxAttempts-1 = 2` replies per item are dropped with no owner, each still
      holding its connection and buffers.
- [ ] T0008 **Use-after-free in `DownloadQueue::cancel`** (`src/suno/DownloadQueue.cpp:361-366`) #risk-high
      `reply->abort()` **synchronously** emits `finished()`, landing in `onFinished` →
      `finishCancelled` → `reply->deleteLater()` (:700) and `retire()`. `findItem` /
      `takeFromActive` return raw `Item*` documented as valid "only while one of the
      containers still holds a reference" (:371-380) — both pointers are invalidated by
      the re-entrant call. The same pattern is already handled correctly elsewhere
      (`SunoClient.cpp:729-730`). Fix: mirror `SunoClient::abortTrackedReplies` (:855-865).
- [ ] T0009 **`opencode.yml` runs a secret-bearing action for any commenter** (`.github/workflows/opencode.yml:3-33`) #security #risk-high
      No `author_association` filter, no `if:` on the commenter, and the job holds
      `id-token: write` plus `secrets.OPENCODE_API_KEY`. Triggered by `/oc` in a comment
      body, from **any** GitHub account. The action is also pinned to `@latest` (:29),
      contradicting `dependabot.yml:16`. Fix: require
      `OWNER|MEMBER|COLLABORATOR|MEMBER` and pin to a SHA.
- [ ] T0010 **An unrecognised playlist `updateType` silently adds tracks** (`src/qml_bridge/SunoBridge.cpp:1611`) #data-loss
      ```cpp
      if (!type) { (void)m->updatePlaylistClips(playlistId, PlaylistUpdateType::Add, {}); return; }
      ```
      sitting directly under a comment claiming "An unrecognised updateType is refused by
      the service" and a header claim of "Anything else is **refused** rather than
      guessed" (`SunoBridge.hpp:202-203`). A typo or future QML value issues
      `POST /playlist/update_clips/` with `update_type:"add"`. Fix: return early and emit
      a refusal through `playlistMutationSettled`.

## P1 — correctness, robustness, and debt that will bite

- [ ] T0011 **The whole 11-route library/playlist mutation surface is runtime-unreachable and unobservable** (`src/suno/SunoLibraryMutations.hpp:493`, `src/suno/SunoLibraryMutations.cpp:394-397`) #risk-high
      `bool enabled_{false}` and `dispatch()` always refuses. `setEnabled` is declared
      (:281) and defined (:141) with **zero production callers**. Separately, **no QML
      file** connects `clipMutationSettled`, `playlistMutationSettled`, `clipSaveRefused`
      or `libraryRefreshRecommended`, nor reads `mutationsAvailable` /
      `mutationsUnavailableReason` / `mutationsBusy`. ~727 LOC plus 11 `Q_INVOKABLE`s can
      neither run nor be seen. Decide: wire it behind an explicit user setting **and** add
      the QML handlers, or delete it and mark the constants `declared-unused`.
- [ ] T0012 **`--headless` makes recording unreachable, not merely silent** (`src/core/Application.cpp:383`) #blocks-ui
      `VisualizerWindow`, the `frameCaptured` → `VideoRecorder::submitVideoFrame` connect
      (:419-425) and `RecordingBridge::setVisualizer` (:429) all live inside
      `if (!opts.headless)`. No window ⇒ no frame. `--record`
      (`CliArgs.inc:36` → `opts.startRecording`) is then **never read anywhere**. Fix:
      reject `--headless` + `--record` at parse time, or hoist the visualizer out.
- [ ] T0013 **Two `PresetManager` instances: UI favourites never reach projectM** (`src/core/Application.cpp:388,406`, `src/visualizer/projectm/Bridge.cpp:53,98`) #data-loss
      `Application` owns one and scans the tree on a worker; `pm::Bridge` owns a **second
      by value** and synchronously re-scans the same directory. `PresetBridge` is wired to
      the Application-owned one (`BridgeRegistration.cpp:75`) while
      `VisualizerWindow::loadPresetFromManager` drives the Bridge-owned one
      (`VisualizerWindow.cpp:120-123`). Fix: inject one manager into `pm::Bridge`.
- [ ] T0014 **Ratings are lost every session — `RatingManager::save()` is never called** (`src/visualizer/RatingManager.cpp:44-56`) #data-loss
      `load()` is called once (`Application.cpp:378`); `save()` has zero callers.
      `PresetBridge::setRating` (`PresetBridge.cpp:201`) mutates only the in-memory map.
      Fix: call it from `Application::quit()`.
- [ ] T0015 **projectM is fed 48 kHz PCM while the config says 44.1 kHz** (`src/visualizer/VisualizerRenderer.hpp:98`, `.cpp:71`) #risk-high
      `audioSampleRate_` is hardcoded 48000 with no setter; it sizes the batch fed to
      projectM. The shipped `[audio] sample_rate = 44100` (`config/default.toml:4`) is
      ignored, **misaligning beat detection by ~9 %**. Fix: read the live rate off the
      audio queue.
- [ ] T0016 **Unbounded lyrics-fetch queue fed by an O(n²) loop over a full sync** (`src/ui/controllers/SunoController.cpp:133-144`) #risk-high
      `libraryUpdated` carries the **whole accumulated list after every page**
      (`SunoLibraryManager.cpp:163`) while auto-paging the entire library at 1.1 s/page
      (:171), so page *k* issues *k*×20 `getAlignedLyrics` queries; `lyricsQueue_` has no
      capacity bound (`SunoLyricsManager.cpp:24`). Fix: iterate the page, dedup by id, cap.
- [ ] T0017 **The lyrics concurrency counter is corrupted by unrelated errors, and leaks permanently when signed out** (`src/suno/SunoLyricsManager.cpp:16-18,83-84`) #risk-high
      The global `errorOccurred` broadcast decrements the lyrics counter, so any unrelated
      error frees a slot and `processQueue()` (:34) overshoots its cap of 3. Conversely
      `SunoClient::fetchAlignedLyrics` returns **silently** when unauthenticated
      (`SunoClient.cpp:1401`) emitting nothing, so the counter never decrements and **the
      queue wedges permanently at 3 after three such drops.** Fix: give lyrics its own
      reply signal and decrement exactly once per issued request.
- [ ] T0018 **`markAllRead` reports a legitimate `204 No Content` to the user as a failure** (`src/suno/SunoNotificationService.cpp:432-437`) #risk-high
      The code requires a JSON object on a 2xx. `SunoEndpoints.hpp:100-105` states no
      success body was ever captured for the mutation set and a 2xx must be treated as
      "accepted, shape unverified". A 204 takes the error path, so **all notifications
      stay unread although the server marked them.** `SunoLibraryMutations::handleReply`
      (:488-493) gets this right. Fix: accept any 2xx without parsing.
- [ ] T0019 **`SunoDatabase` has no indexes, no WAL, no `busy_timeout`, and discards transaction results** (`src/suno/SunoDatabase.cpp:83-120,295,303`) #risk-high
      No `CREATE INDEX` anywhere; `getAllClips` (:311, `ORDER BY created_at DESC`) and
      `searchClips` (:409-415, five `LIKE`) are full scans on a table with no retention
      policy. No `journal_mode=WAL` / `synchronous` / `busy_timeout`, so a crash-held
      write lock blocks every later `open()` for the default 5 s. `db_.transaction()` and
      `db_.commit()` results are **discarded**, so `saveClips` reports ok even when
      nothing was persisted. `addDatabase("suno_db")` (:84) has no `contains()` guard and
      the destructor never calls `removeDatabase`.
- [ ] T0020 **Raw `this` captured in `SunoAccountManager` request callbacks** (`src/suno/SunoAccountManager.cpp:185,197`) #risk-high
      The manager is a `unique_ptr` member of `SunoController` (:66); destroy it before
      the reply lands and the lambda runs on freed memory. Three sibling services already
      use the correct `QPointer` guard for this exact hazard (`SunoExploreService.cpp:226`,
      `SunoNotificationService.cpp:259`, `SunoAudioUploadService.cpp:230`).
- [ ] T0021 **`vc::Signal<>` subscriptions capture raw `this` and are never disconnected** (`src/suno/SunoLibraryManager.cpp:13-15,22-30`) #risk-high
      `vc::Signal` (`src/util/Signal.hpp:112`) stores a plain `std::function` with no
      lifetime tracking and no `disconnect`; the connection id is discarded and the
      destructor is `= default` (.cpp:60). A `SunoClient` outliving the manager calls a
      freed `this`. Same in `SunoLyricsManager.cpp:12-18`. Fix: retain the `SlotId` and
      disconnect in the destructor.
- [ ] T0022 **`Config`'s mutex protects almost nothing, and any read marks it dirty** (`src/core/Config.hpp:73-104,110-121`) #risk-high
      `mutex_` is taken only by load/save/loadDefault/addOverlayElement/removeOverlayElement;
      every section accessor bypasses it and returns a **non-const** reference, so
      `CONFIG.suno().x = y` races `Config::save`. All eight accessors call `markDirty()`
      unconditionally, so pure readers dirty the config (`EncoderSettings.cpp:272`,
      `VisualizerRenderer.cpp:27,222`, `SunoDownloader.cpp:79,226`, `SunoClient.cpp:383`).
      The header comment at :5 ("Thread-Safe: Mutex-protected access") **overstates the
      code.** (after: T0023)
- [ ] T0023 **`Config::save()` never clears the dirty flag** (`src/core/Config.cpp:23-26`) #risk-high
      `ConfigLoader::save` succeeds (:299) but nothing calls `markClean()`, so with T0022
      the file is rewritten on every quit regardless of change.
- [ ] T0024 **`setFavorite`/`setBlacklisted` mutate the published generation in place** (`src/visualizer/PresetManager.cpp:474-494`) #risk-high
      `publishGeneration` documents "Swap, never clear-and-refill: a Snapshot pinned by any
      reader keeps the old storage alive **and unmodified**" (:209-211). The toggle writes
      `liveList()[index].favorite` (:477), violating the invariant documented 265 lines
      earlier in the same file. Fix: copy-on-write the generation.
- [ ] T0025 **`Logger::get()` races on a non-atomic static** (`src/core/Logger.cpp:7,59-64`) #risk-high
      `logger_` is a `static std::shared_ptr`. Two threads logging before
      `Application::init` reaches `Logger::init` (`Application.cpp:275`) race on the
      pointer assignment and on `spdlog::register_logger`. Fix: Meyers singleton +
      `std::call_once`.
- [ ] T0026 **projectM can be destroyed with no current GL context — and it WILL issue GL calls** (`src/visualizer/projectm/Engine.cpp:56-61`, `src/visualizer/VisualizerWindow.cpp:30-35`) #risk-high
      `~VisualizerWindow` only cleans up if `makeCurrent` **succeeds** (:31); if it fails,
      `~VisualizerRenderer` (`:17-19`) cleans up anyway with no context, then
      `projectm_destroy()` runs contextless. Verified upstream: `projectm_destroy` →
      `~ProjectM()` → `Texture::~Texture()` unconditionally calls **`glDeleteTextures`** on
      every owned texture (`Texture.cpp:63` @ master), and `~ProjectM()` is an **empty body**
      that clears no state. There is no context check and no upstream documentation of the
      requirement. Worse on macOS: with the Qt Quick context current instead, a no-context
      `glDeleteTextures` can delete names in the **wrong namespace**. FIX: assert
      `makeCurrent` succeeded, and if it did **not, leak the handle deliberately** — a leak is
      strictly better than deleting GL objects in the wrong context. `RenderTarget::destroy()`
      has the matching hole (zeroes names, leaks them, `RenderTarget.cpp:109-116`).
- [ ] T0027 **GL state is set but never saved or restored around projectM — upstream will not fix it** (`src/visualizer/VisualizerRenderer.cpp:88-106`) #risk-high
      Sets viewport, scissor, `GL_SCISSOR_TEST`, `glColorMask`; never reads the previous
      values, and `GL_BLEND`/`GL_DEPTH_TEST` are neither set nor restored. Contained today
      only **by accident** of separate-surface ownership. This is a genuine upstream gap, not an
      integration error: upstream PR #981 ("preserve gl state of calling application") was
      written and reviewed, then **closed unmerged** on 2026-03-10 — maintainer `kblaschke`,
      verbatim: *"Can't test it currently so someone else would have to do it."* `GLStateGuard.hpp`
      is absent from master. libprojectM resets only FBO 0 and viewport, and its own GLES
      comment warns that per-FBO draw-buffer state *"leaks into FBO 0 on some drivers"*. The
      host must therefore guard viewport + scissor + colour mask + blend + depth itself. FIX:
      save and restore all of it around the projectM call.
- [ ] T0028 **CLI overrides bypass the parser's clamps** (`src/core/Application.cpp:318-320`) #risk-high
      `applyOverride` writes raw values **after** `clamp(fps,10,240)`
      (`ConfigParsers.cpp:259`). `--visualizer-fps 0` → `fps=0` → `VisualizerRenderer.cpp:28`
      skips its guard and keeps `targetFps_{60}` (`VisualizerRenderer.hpp:99`), so the flag
      silently does nothing.
- [ ] T0029 **`FileBackend::store` leaves a 0644 window on the refresh secret** (`src/suno/auth/CredentialStore.cpp:284-290`) #security
      `file.commit()` renames the temp file into place; `setPermissions(0600)` runs
      **after**. Under `umask 022` the secret is world-readable in between. Gated to
      `Backend::File` / `CHADVIS_NO_KEYCHAIN` only (:634), both of which `LOG_WARN` — so P1,
      not P0. Fix: open the temp file with the permissions (QSaveFile inherits them).
- [ ] T0030 **Full request URLs reach the log** (`src/suno/SunoClient.cpp:1169-1170,1296-1297`, `src/suno/CapturedHosts.cpp:308`) #security
      `url().toString()` is logged; `reply->errorString()` is logged and Qt populates it
      with the full URL on transfer errors; `describeRefusal` embeds `url.toString()` in
      every refusal sentence. `refusalSentence` echoing a **query string** is a latent leak
      — the loopback callback carries `state`. Fix: log `url.path()`.
- [ ] T0031 **`tokenChanged` carries a live bearer and has zero subscribers** (`src/suno/SunoClient.hpp:239`) #security
      Emits `token.jwt.toStdString()` (:883) and nothing ever connects. One careless future
      `connect` from QML or a log away from a leak. Fix: delete, or send a redacted form.
- [ ] T0032 **Failed worker-thread join leaves a use-after-free window** (`src/suno/SunoClient.cpp:189-194`) #risk-high
      On `wait()` failure the destructor only logs, sets `worker_=nullptr`, and the object
      dies — while queued lambdas at :215/:219 capture **raw `this`**. The comment at
      :186-188 claims destruction joins the thread, which a failed join does not deliver.
- [ ] T0033 **Duplicate host allowlists survive outside the registry** (`src/suno/SunoDownloader.cpp:121`, `src/suno/ClipParser.cpp:72-73`, `src/suno/SunoAudioUploadService.cpp:163`) #risk-high #security
      `CapturedHosts.hpp:11-14` documents replacing three per-file predicates as the reason
      the registry exists; three remain hand-rolled, so `hostsForRole` has **no production
      caller** and editing a registry row changes nothing. All three are anonymous fetches
      today, so no bearer leaks — but the fail-closed guarantee is triplicated.
- [ ] T0034 **Recursive preset scan has no depth or breadth cap** (`src/util/FileUtils.cpp:242-247`) #risk-high
      `recursive_directory_iterator` with no `max_depth`. Symlink loops are safe and
      permission errors handled, but a user-supplied tree is walked unboundedly.
- [ ] T0035 **No upload size limit, no cancel, no retry, wrong content type** (`src/suno/SunoAudioUploadService.cpp:206-212,329-330,341-343`) #risk-high
      Validates `size() <= 0` but sets no maximum and offers no `cancel()` — the only aborts
      are private and destructor-driven, so a 2 GB upload cannot be stopped. Exactly one
      storage POST, no retry, no `Retry-After`, and on failure the S3 ticket is dropped
      (:390) leaving orphaned parts. Content type is hardcoded `application/octet-stream`
      although only `m4a` is accepted, so it should be `audio/mp4` — and the validated
      `ticket_->fields["Content-Type"]` (:150-154) is ignored.
- [ ] T0036 **`SunoController` fans every client error into `libraryFetchFailed`** (`src/ui/controllers/SunoController.cpp:160-168`) #risk-high
      A failing Explore page, a 429 on the badge endpoint, or a blocked upload **clears the
      library spinner and sets the library error** (`SunoBridge.cpp:315-318`;
      `SunoLibraryManager.cpp:22-30`). The comment in `SunoWorkspace.hpp:83-90` names this
      exact anti-pattern and leaves it in the one place it survives.
- [ ] T0037 **Blocking TagLib and file I/O on the GUI thread in the download completion path** (`src/suno/SunoDownloader.cpp:419,543,576`) #risk-high
      `tagAudioFile` opens and rewrites the file in place; two `ofstream`s and two
      `fs::exists` run synchronously inside `DownloadQueue::finished` on the GUI thread. A
      40-clip batch tags and writes 80 sidecars in one event-loop turn.
- [ ] T0038 **`scripts/build-fast.sh` hijacks the Release build directory with a `-O0` Debug config** (`scripts/build-fast.sh:1`) #risk-high
      Directly contradicts `build.sh:7-10` ("every profile gets its OWN directory"). No
      shebang, no `set -e`, hardcodes `sccache` while `Compiler.cmake:170` probes both, and
      reuses `CMAKE_CXX_FLAGS` so it sticks in the cache. Fix: delete it, or make it a
      two-line wrapper over `./build.sh --fast`.
- [ ] T0039 **`release.yml` misstates macOS signing and believes `MACOSX_BUNDLE` is unset** (`.github/workflows/release.yml:19-42,190-192,300-306`) #risk-high
      It passes the CMake signing variables and the notes claim the `.app` is unsigned,
      but `cmake/TargetSetup.cmake:868` always ad-hoc-signs (`_chadvis_sign_identity`
      defaults to `"-"` at :855-856). It also asserts `MACOSX_BUNDLE` is **not** set — it
      **is** (`TargetSetup.cmake:263`) — so the macOS release leg is `required: false`
      (:93,:96) and can never block a release.
- [ ] T0040 **`SAFE_TESTS` silently omits three registered ctest entries** (`build.sh:50-59`) #risk-high
      `test_CapturedHosts`, `test_FeatureFlags` and `test_OffscreenRenderSpike` are
      registered but absent, so `--tests` does not build them and `--safe` does not run
      them — including the suites `tests/unit/CMakeLists.txt:98-105` calls "the only tests
      covering the tree's fail-closed host policy". The comment at `build.sh:37-40`
      documents this failure having happened before. Make `build.yml:271-291` the single
      source of truth. (after: T0041)
- [ ] T0041 **Three ctest entries are instrumented but carry no `TSAN_OPTIONS`** (`tests/unit/CMakeLists.txt:339-350`) #risk-high
      `test_HttpPolicy`, `test_RenderExecutor` and `test_OffscreenRenderSpike` are missing
      from the block `tsan.yml:53-57` records as a known gap. `test_RenderExecutor` is
      documented headless-safe (:203) and belongs there.
- [ ] T0042 **The TSan lane cannot distinguish a new race from the old ones** (`.github/workflows/tsan.yml:191-193,163,207`) #risk-high
      Non-gating, and "expected to report 39 races" since it was written. Add a report-count
      threshold so regressions bite while known noise stays tolerated. **Do not** fill
      `cmake/tsan.supp` — a suppression that hides a real race is worse than a red lane.
- [ ] T0043 **A failed worker-thread join in `SunoClient` is compounded by no ASan/UBSan lane** (`.github/workflows/`) #risk-high
      `CHADVIS_SANITIZER` accepts `address`/`undefined` (`Compiler.cmake:139-140`) and
      `build.sh:167-168` exposes `--asan`/`--ubsan`, but only TSan has a workflow — and the
      PCM/decoder/encoder code is exactly what the unexercised lanes would catch.
- [ ] T0044 **A stale Clerk failure reply can tear down a healthy session** (`src/suno/SunoClient.cpp:950-953`) #risk-high
      `onClerkAuthFailedInternal` clears the bearer and bumps the request epoch **without**
      the epoch guard its sibling has at :926. A failure from an exchange invalidated by a
      credential change signs out a live session. No test covers the failure path.
- [ ] T0045 **`LyricsBridge::setSearchQuery` is not `Q_INVOKABLE`, so lyrics search never fires** (`src/qml_bridge/LyricsBridge.hpp:61`, `src/qml/panels/LyricsPanel.qml:54`) #blocks-ui
      A plain `public:` member, outside the `public slots:` block starting at :63. QML can
      only invoke slots/`Q_INVOKABLE`s, so this fails at runtime and the panel's search
      field never sends its query.
- [ ] T0046 **`SettingsWindow.onClosing` is shadowed on the instance — accent edits are lost** (`src/qml/main.qml:484-490`) #data-loss
      Declaring `onClosing` on the instance **replaces** the component-level handler, so
      `appearancePage.apply(); SettingsBridge.save()` (`SettingsWindow.qml:67-70`) never
      runs. Fix: merge into a `Connections { target: settingsWindow }`.
- [ ] T0047 **Theme accent and background are not persisted although the UI says they are** (`src/qml/panels/settings/AppearanceSettings.qml:23-26`) #data-loss
      `Theme.applyAccent/applyBackground` write only the in-memory singleton
      (`Theme.qml:296-316`); `SettingsBridge.hpp` declares no such property. The claims at
      `SettingsView.qml:39` ("auto-saved as you tweak") and `SettingsPanel.qml:8` are false.
- [ ] T0048 **No qmllint configuration and no lint gate** (`.qmllint.ini`, `.github/`) #risk-high
      Zero `.qmllint.ini` and no `all_qmllint` in CI. `qt_add_qml_module` auto-creates a
      `<target>_qmllint` target — wire it with `-W 0 --json -`, and turn on
      `CompilerWarnings=warning` (qmlsc diagnostics, disabled by default),
      `UnusedImports=error`, `UnqualifiedAccess=error`,
      `TranslationFunctionMismatch=error`. This is the single highest-value guard
      available against the runtime-only QML class. (after: T0049)
- [ ] T0049 **Qt 6.12 can convert missing `required property` into a load-time error — not used** (`src/qml/`, `src/core/Application.cpp`) #risk-high
      "When instantiating a component with missing required properties, the engine now
      reports an error instead of silently constructing an incomplete object." Marking
      C++-injected QML dependencies `required` and supplying them via
      `setInitialProperties` converts a whole class of runtime-only bug into a load-time
      failure. Also connect `QQmlApplicationEngine::objectCreationFailed` (since 6.4) — it
      is the difference between "the app showed no window" and a diagnosable error.
- [ ] T0050 **The Qt floor is 6.7, which is EOL** (`cmake/Dependencies.cmake:20-25`) #risk-high
      Standard support ended 2025-03-26. **Target Qt 6.8 LTS at minimum (6.8.8 current,
      supported to 2029-10-08, and it receives the backported screen-reader fixes), and plan
      6.12 LTS** (released 2026-09-30, supported to 2031-09-30). The only API gating the
      floor is `QNetworkRequest::setTransferTimeout` (6.7), which both satisfy. Qt 7 does
      not exist and no migration cliff is scheduled.

Carried over from the superseded backlog and **confirmed by re-reading the tree**:

- [ ] T0166 **The OAuth loopback accepts an IdP error response as success** (`src/suno/auth/oauth/OAuthLoginService.cpp:138-163`) #risk-high
      `acceptCallback` validates path, URL cap, state presence, a 256-byte state cap,
      `constantTimeEquals`, and the deadline — then goes straight to `State::CallbackReceived`
      and emits `callbackReceived()`. It **never checks for a `code` parameter and never
      handles `error`/`error_description`**, so `?error=access_denied&state=<valid>` reaches
      the authenticated state. Verified by reading the whole function. Also in that function's
      blast radius: any non-matching path (a hostile `<img src="http://127.0.0.1:PORT/">`)
      calls `rejectAndClose`, which resets the transaction and kills an in-flight sign-in for
      the full 7-minute timeout; and `stop()` closes only the listening socket, leaving
      accepted `QTcpSocket`s alive with no idle timer.
      **The lane is deliberately gated, so this is not a live exploit — but it is a
      precondition for flipping that gate (T0145), not something to do after.**
- [ ] T0167 **A short library sync is indistinguishable from a complete one** (`src/suno/SunoLibraryManager.cpp`, `src/suno/DownloadQueue.cpp`) #risk-high
      Neither layer reports *completeness*, so a truncated sync is presented as a finished
      library. Prescribed shape: `{complete: false, projects, errors, stats: {pagesFetched,
      expectedTotal, elapsedMs}}`, and a short page must not be treated as proof of completion
      when the reported total disagrees.
- [ ] T0168 **Transfer failure is matched by clip *title*, not id** (`src/qml/views/LibraryView.qml`) #risk-high #data-loss
      `downloadStatus` is the only terminal channel, and the view parses `"Download failed for "`
      then matches the remainder against `clip.title` **by exact equality** — so a rename or a
      duplicate title attributes the failure to the wrong clip. `downloadStatus` is also never
      cleared, so an hour-old failure still reads as current. Fix: a
      `downloadStateChanged(clipId, state, reason)` signal.
- [ ] T0169 **`resolveDestPath` reuses a plain-named file left by an earlier session** (`src/suno/SunoDownloader.cpp:301-347`) #data-loss
      A different clip whose title matches still collides with a stale plain-named file. The
      `SUNO_ID` TXXX frame the tagger already writes is the discriminating value; reading it
      closes the collision without changing the filename.
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
- [ ] T0172 **The audio pre-buffer computes the wrong next track when shuffle is on** (`src/audio/AudioEngine.cpp:201`) #risk-high
      It derives the linear `index+1` while `shuffleOrder_` is the real traversal order, so
      pre-buffering after a shuffle plays the wrong track. Independent of the re-entrancy
      defect the encoder lane found on the same path. Small and low-risk.
- [ ] T0173 **projectM's broken-preset retry loop is dead** (`src/visualizer/projectm/Bridge.cpp:8-14`) #risk-medium
      Registering `projectm_set_preset_switch_requested_event_callback` **overrides** projectM's
      own single-slot handler, killing its 5-retry recovery. A behavioural regression against
      stock projectM rather than a safety bug, but it means a preset that fails to load is
      simply lost. Distinct from T0057, which is about the never-emitted `presetLoading` flag.

## P2 — robustness, cleanup, and debt

- [ ] T0051 `QTP0004` is deliberately disabled while 58 QML files live away from the `qt_add_qml_module` call (`cmake/TargetSetup.cmake:129-134`) #risk-medium
      Qt calls this layout "a frequent source of mistakes" and 6.8 can generate per-directory
      `qmldir`s (`NO_GENERATE_EXTRA_QMLDIRS`). Either enable it and fix the relative imports,
      or flatten the QML tree beside the CMake call.
- [ ] T0052 All four `.qss` files are **100 % dead** and target classes that cannot exist (`resources/styles/*.qss`, `resources/dark-theme.qss`) #cleanup
      The app is a `QGuiApplication` (`Application.cpp:353`) and **no `setStyleSheet` call
      exists anywhere**; grep for "qss" in `src/`/`cmake/` returns only the three `<file>`
      entries in the `.qrc`. `dark.qss` selects 27 Qt **Widgets** classes — none of which
      exist in a pure-QtQuick app. They are a fork of a different app ("VibeChad",
      "VibeVisuals"). Fix: delete both `dark-theme.qss` and the four styles.
- [ ] T0053 `src/qml/shaders/shadow.frag` is dead, uncompilable, and not even in the binary (`src/qml/shaders/`) #cleanup
      No `qt_add_shaders` anywhere, zero `ShaderEffect` in QML. It is GLSL ES 1.00
      (`varying`, `gl_FragColor`, `texture2D`, no `#version`) and **cannot compile on any
      Qt 6 backend** (RHI compiles GLSL ES 3.00); it also declares a `lowp sampler2D`,
      which is invalid. Fix: delete the directory.
- [ ] T0054 Six orphaned QML files (`src/qml/views/SettingsView.qml`, `panels/SettingsPanel.qml`, `panels/PlaybackPanel.qml`, `components/AccordionContainer.qml`, `AccordionPanel.qml`, `ComingSoonPage.qml`) #cleanup
      `ComingSoonPage.qml` is real content loss (the roadmap surface). `PlaybackPanel.qml`
      also carries a latent bug: it overwrites `AppButton`'s own `buttonRadius` binding.
- [ ] T0055 `ThemeBridge` is compiled, linked, and entirely unreachable (`src/qml_bridge/ThemeBridge.{hpp,cpp}`, `cmake/Sources.cmake:188-189`) #cleanup
      59 properties and two `Config::save()` calls, zero QML references, deliberately
      unregistered at `BridgeRegistration.cpp:34-37`. A complete duplicate theme system.
- [ ] T0056 `SunoWorkspaceBridge` is compiled and unregistered by design (`cmake/Sources.cmake:196-197`, `src/qml_bridge/BridgeRegistration.cpp:41-64`) #cleanup
      ~190 header + 622 cpp LOC, 16 properties, zero QML references. `CreateView.qml` uses
      `SunoBridge.generate` instead. Decide: wire it or delete it.
- [ ] T0057 `pm::Bridge::presetLoading` is declared, connected, branched on, and **never emitted** (`src/visualizer/projectm/Bridge.hpp:45`, `src/visualizer/VisualizerRenderer.cpp:32,92-94`) #cleanup
      `presetLoading_` is permanently false, so the "clear to black while loading" path is
      dead. **Research settles the options:** projectM v4 has **no async/preload API in any
      released version** — loading is synchronous inside `projectm_load_preset_file()`, and
      an integrator measured a **600–1200 ms** stall at exactly the crossfade moment, with
      ~177 ms of that being HLSL→GLSL transpile (projectm discussion #1008, 2026-07-06).
      A preload API was offered upstream and is **not merged**. So either emit the flag around
      the genuinely-blocking load (honest about blocking, which the UI can at least respond to)
      or delete the flag and the branch. Do not invent a two-phase load the library cannot do.
- [ ] T0164 **projectM is pinned two releases stale — 4.1.8 fixes an out-of-bounds read** (`cmake/FindProjectM4.cmake:62`) #risk-high
      The pin is `GIT_TAG v4.1.6` (2025-11-28). **v4.1.7** (2026-07-14) and **v4.1.8**
      (2026-10-06) are both newer. 4.1.8 fixes `projectm_pcm_get_max_samples()` returning a
      wrong value (**invalidate any cached value on upgrade**), an **out-of-bounds read in
      custom waveform code**, an infinite loop on self-referential shader macros, and four
      HLSLParser correctness bugs, plus a large shader-transpile speedup. 4.1.7 fixed a
      `nullptr` deref in the texture sampler, inverted Y/angle to per-pixel expressions, and
      **"reset to default framebuffer (FBO 0) after rendering presets and transitions"** —
      which is directly relevant to T0027. The 4.1.7→4.1.8 public API delta is
      **byte-identical** (`core.h`, `parameters.h`, `render_opengl.h` unchanged), so this is a
      free, low-risk upgrade.
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
      `std::map` with no lock. Fix: inject it, key on path, and add a mutex. (after: T0014)
- [ ] T0059 `RatingManager::save()` and `PresetPersistence::saveState` are non-atomic (`src/visualizer/RatingManager.cpp:51-54`, `src/visualizer/PresetPersistence.cpp:42-52`) #data-loss
      Plain truncate-and-write, unlike `ConfigLoader::save`'s temp+rename. `saveState` runs
      on **every favourite toggle** (`PresetManager.cpp:556`), so it is a hot path, not a
      once-per-session one.
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
- [ ] T0067 `SunoNotificationService` has no polling timer, no dedup, and discards a successful list when the badge fails (`src/suno/SunoNotificationService.cpp:248,313-315,349-352,486`) #risk-high
      Zero `QTimer` hits, so `unreadCountChanged` never fires on its own. `enqueueRefresh`
      requires **both** requests; a badge failure routes through `failRefresh`, which
      throws away `pendingResponse_` — a fully successful notification list is dropped
      because a decorative counter 404'd.
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
- [ ] T0070 `preset_path` and path-typed `[suno]` keys are rewritten absolute on save (`src/core/ConfigParsers.cpp:456,223,533`) #data-loss
      The shipped `preset_path = ''` becomes e.g. `/usr/share/projectM/presets` on first
      save, converting a portable template into a machine-pinned file **and defeating the
      auto-detect fallback documented at `docs/user/CONFIG.md:126`. Fix: serialize `~/`-relative.
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
- [ ] T0074 `Escape` closes the whole Settings window while the "Reset all settings?" dialog is up (`src/qml/SettingsWindow.qml:156-160`) #risk-high
      Window-scope `Esc` outranks the modal dialog, so a user backing out of a destructive
      confirmation instead dismisses the window — losing unsaved edits per T0046. Directly
      contradicts `main.qml:560-561`.
- [ ] T0075 Destructive actions with no confirmation (`src/qml/panels/PlaylistPanel.qml:268,262`, `src/qml/panels/SettingsPanel.qml:83`) #risk-high
      "Clear All" destroys the whole playlist in one right-click. The live path
      (`SettingsWindow.qml:141-146`) does have a dialog — copy it.
- [ ] T0076 Views with no loading, empty, or error state (`src/qml/views/VideoView.qml:18-53`, `panels/PresetsPanel.qml:45-54`, `panels/PlaylistPanel.qml:87`, `components/TransportBar.qml`) #blocks-ui
      `VideoView` shows a black rectangle captioned "Ready". **Model correctly on
      `LibraryView.qml:397-448`, `DiscoverView.qml:207-261`, `NotificationsView.qml:191-243`
      and `AccountPage.qml:700-746` — those are the template.**
- [ ] T0077 `NotificationsView` renders the literal string `@undefined` (`src/qml/views/NotificationsView.qml:154,163,172,181`) #risk-high
      `x !== ""` guards a possibly-`undefined` map key, and `undefined !== ""` is true. A
      direct sibling of the flat-`Repeater` class. Also `VisualizerOverlay.qml:15-16,20,28,38-41,61-91`
      has **9 unguarded `modelData` dereferences per overlay** over JSON-persisted state, so
      schema drift is guaranteed.
- [ ] T0078 Pseudo-reactive bindings that never update (`src/qml/components/KaraokeMaster.qml:195`, `components/VisualizerOverlay.qml:33`) #risk-high
      `Math.sin(Date.now()/500 + index)` inside a binding — `Date.now()` is not a bindable
      property, so the "animation" is computed once and frozen.
- [ ] T0079 The `"M"` shortcut has no text-entry guard, unlike `"F"` (`src/qml/main.qml:598-602`) #risk-high
      Typing "m" in the Library search box collapses the nav rail on every keystroke. The
      guard already exists at `main.qml:144-165`; this shortcut just forgot it.
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
      `readerwriterqueue` has no system-first probe (unlike spdlog/fmt/toml++ at :50-85),
      `CPM_SOURCE_CACHE` defaults OFF (`CPM.cmake:157`) so projectM is re-cloned per fresh
      build dir, and every pin is a mutable tag rather than a SHA.
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
- [ ] T0095 Every CI `ctest` invocation is serial (`.github/workflows/build.yml:297,229`) #performance
      Add `--parallel $(nproc)`. `release.yml:121-123` admits it has no dependency caching
      while still installing ccache.
- [ ] T0096 Recorder tests write receipts into the repository root (`.gitignore:99-108`, `tests/recorder/test_RenderExecutor.cpp`, `test_RecordingPipeline.cpp`) #risk-high
      The ignore rule is a **guard, not a repair**: `RenderJob::kReceiptSuffix` sidecars land
      in CWD because the tests point the encoder at relative paths. 17 of 45 test files use
      `QTemporaryDir` correctly — copy them. **This recurs on every full ctest run.**
- [ ] T0097 `unit_tests` is one binary with one `QCoreApplication` running 31 suites sequentially (`tests/unit/test_main.cpp:38,41-72`) #risk-medium
      A crash or hang in suite 30 loses suites 1–29's results, and config-singleton mutators
      are unisolated.
- [ ] T0098 Tautological assertions in the offscreen suite (`tests/unit/recorder/test_OffscreenRenderSpike.cpp:92-94,225-226`) #cleanup
      `QCOMPARE(f(report_.verdict), f(report_.verdict))` compares an expression to itself and
      can never fail; the surrounding comment claims it verifies agreement with the verdict.
      Also brittle: :338-363 asserts **seven literal substrings** of a human-readable report.
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
- [ ] T0101 `FFmpeg 9.0`: the `AVFrame::channels` → `ch_layout` migration is **already done** — verify and close (`src/recorder/`) #cleanup
      **REFUTES the obvious task.** The old fields are gone at FFmpeg 7.0 (lavu 59), but this
      tree already uses `ch_layout` throughout (`VideoRecorderFFmpeg.cpp:823,888,906,909`;
      `AudioFileDecoder.cpp:182,186`) and `av_hwdevice_ctx_create` is **not** deprecated in
      9.0.2 (`hwcontext.h:294` carries no `attribute_deprecated`, and the doc comment
      *recommends* alloc+init for callers needing control). The only `channels`-shaped hit is
      Qt metadata (`src/audio/analysis/MediaMetadata.cpp:86`). Action: confirm by compiling,
      then close this item so nobody "migrates" working code. Note
      `VideoRecorderFFmpeg.cpp:29-32` blanket-suppresses `-Wdeprecated-declarations`, which
      will hide a real deprecation on upgrade — narrow it.
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
- [ ] T0153 **The resampler never rate-converts: `in_rate` is set to the encoder's rate** (`src/recorder/VideoRecorderFFmpeg.cpp:911`) #risk-high #data-loss
      `swr_alloc_set_opts2` is called with `settings.audio.sampleRate` as *both* `in_rate` and
      `out_rate`, while the queue carries the **observed sink rate**
      (`src/audio/AudioEngine.cpp:522`). The file is self-documented as "a real bug and it is
      not fixed here" at `:925-931`. A 44.1 kHz sink encoded as 48 kHz plays at the wrong
      speed. Fix: thread the queue's real rate through `VideoRecorderThread` → `encodeAudio`
      into `in_rate`. **This is the same class as T0015** (projectM fed 48 kHz against a
      44.1 kHz config) and both must be fixed from one source of truth.
- [ ] T0154 **The audio callback thread mutates GUI-affine state without atomics** (`src/audio/AudioEngine.cpp:476-479,507-511,533,537-539`) #risk-high
      `processAudioBuffer` runs on the Qt audio thread (the `QAudioBufferOutput` connection at
      :213 is auto/direct) and writes plain `int observedSinkRate_` (`AudioEngine.hpp:213`),
      plain `bool conversionFaulted_` (:220), plain counters (:216-217) and **`QString`
      `outputStatus_` (:215)** via `refreshOutputStatus()` — all read on the GUI thread by
      `SettingsBridge::attachAudioEngine` → `audioOutputStatus()` (`SettingsBridge.cpp:315,322`).
      **`QString` assignment is a non-atomic multi-word mutation, so a torn read is UB, not a
      stale value.** Fix: atomics for the scalars, and marshal the status string through a
      queued invocation.
- [ ] T0155 **`LOG_ERROR` and `emit errorSignal` from the real-time audio callback** (`src/audio/AudioEngine.cpp:537-539`) #risk-high
      `reportConversionFailure` formats, writes the log, and emits from the RT thread;
      `SettingsBridge`'s slot (`SettingsBridge.cpp:310-312`) then mutates a QML singleton
      property — **a GUI-thread write from an audio thread.** Fix: post the report.
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
- [ ] T0160 **`sws_getContext` return is unchecked, then dereferenced; and the context is keyed on the first frame only** (`src/recorder/VideoRecorderFFmpeg.cpp:413-426`) #risk-high
      The result is `reset()` into `swsCtx_` with **no null test**, and `sws_scale(swsCtx_.get(), …)`
      runs on the next line. Every other allocation in this file is checked, and the comment at
      :771-783 explains exactly why this one must be too. Separately, the context is created
      once (`if (!swsCtx_)`) from the *first* frame's dimensions, so a mid-recording resize feeds
      `sws_scale` a source linesize of 0 for the new width while the scaler strides by the old
      one. Fix: null-check, and key the context on `(frame.width, frame.height)`.
- [ ] T0161 **`av_hwframe_get_buffer` failure leaves a half-built `hwFrame_`** (`src/recorder/VideoRecorderFFmpeg.cpp:790-793`) #risk-high
      On failure only a `LOG_WARN` is emitted, but `hwFramesCtx_` stays non-null, so
      `encodeVideo:438` takes the `hwFramesCtx_ && hwFrame_` branch and calls
      `av_hwframe_transfer_data` on a frame whose buffers are null. Fix: reset the context on
      failure. Also `av_buffer_ref` at :1420 is unchecked, and `avcodec_parameters_from_context`
      is ignored at three sites (`:761, :879, :1311`), leaving an `AVStream` with an unpopulated
      `codecpar` — exactly the failure the surrounding comments call fatal to
      `avformat_write_header`.
- [ ] T0162 **`encodeAudio` is O(n²) and strided by the wrong channel count** (`src/recorder/VideoRecorderFFmpeg.cpp:581-588`) #performance #risk-medium
      Each iteration builds a `std::vector<f32>` from the front of `buffer` then
      `buffer.erase(buffer.begin(), …)`, shifting the whole tail — ~47 allocations and 47
      full-tail memmoves per second of audio at 48 kHz stereo. Use a cursor, as
      `AudioFileDecoder::Impl::pendingHead` (`AudioFileDecoder.cpp:125`) already does. The same
      loop strides by the caller's `channels` (hardcoded `2` at `VideoRecorderThread.cpp:163`)
      while `swr_convert` reads `frameSize` samples from the layout — for a 1-channel encoder
      (`EncoderSettings.hpp:108` allows any `u32`) the slice is twice the input and mis-strided.
      Derive the stride from `audioCodecCtx_->ch_layout.nb_channels`.
- [ ] T0163 **Stereo→mono downmix leaves libswresample's default matrix** (`src/recorder/AudioFileDecoder.cpp:211`) #risk-medium
      `swr_set_matrix` is applied only for `inChannels == 1 && spec.channels == 2`; the mirror
      case (stereo source, `job.audioChannels == 1`, which `RenderJob.cpp:444` explicitly
      permits) gets the library's default coefficients — a different gain than the
      `AudioQueue::pushInternal` mono path this decoder is compared against. Set an explicit
      matrix for 2→1 too.
- [ ] T0103 `FFmpeg 9.0`: add the `swr_convert` flush loop (`src/recorder/ResamplerEngine.hpp`) #risk-high #data-loss
      `doc/examples/resample_audio.c` omits it. Loop
      `swr_convert(ctx, out, cap, nullptr, 0)` until it returns 0, or **the tail samples are
      silently lost** and the output length will not match. Size with `swr_get_out_samples`
      (an upper bound, invalidated by any intervening call), not `swr_get_delay`.
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
- [ ] T0110 Fix the documented wrong executable path (`README.md:41`, `docs/user/INSTALL.md:111`, `docs/dev/manual-qa.md:37`) #risk-medium
      The target is a real `MACOSX_BUNDLE` (`cmake/TargetSetup.cmake:263`), so the binary is
      `build/chadvis-projectm-qt.app/Contents/MacOS/chadvis-projectm-qt`. `build.sh:371-376`
      was already corrected; the docs were not.
- [ ] T0111 `release.yml` believes macOS bundle packaging is broken and `.desktop`/dependabot comments are stale (`.github/workflows/release.yml:19-42`, `.github/dependabot.yml:7,16`) #cleanup
      The workflow asserts `MACOSX_BUNDLE` is unset (it is set) and its macOS leg is
      `required: false`. `dependabot.yml` says "the single workflow" when there are five, and
      still lists `pffft` among pinned deps although it was removed 2026-10-04.
- [ ] T0112 Delete three dead scripts and two empty dirs (`scripts/`) #cleanup
      `install_projectm_v4_local.sh` is both dead **and destructive** — it `sed -i`s a line
      that no longer exists (silent no-op), `rm -rf build` (:130), pins v4.1.1 against
      FindProjectM4's v4.1.6, and rewrites `config/default.toml`'s `preset_path` to an in-tree
      relative path that would ship. `check_deps.sh` reports false negatives for every Qt
      component (Qt6 ships no `.pc` files). `run_debug.sh` and `setup-arch.sh` point at the
      pre-bundle binary path. `skills/` and `tests/manual/` are empty.
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
- [ ] T0140 Remove the stale dead-code comment in `ConfigLoader` and other drifted comments (`src/core/ConfigLoader.cpp:120-123`) #cleanup
      The comment claims a fixed null deref that is already correct. Harmless but misleading.

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