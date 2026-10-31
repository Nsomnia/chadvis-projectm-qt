# TODO archive — completed, dropped, and refuted items

Archived from [TODO.md](../../TODO.md) on 2026-10-30. Each item's full evidence
lives in its `STATUS/<id>.log` or `git log -p TODO.md`. This file is append-only;
items move here, never back.

## Completed `[x]`

- [x] T0333 **Discover feed renders empty while signed in** — parser read two-releases-old envelope. `route-probe.mjs` settled: `unified/explore` 200 on both bases. Parser now reads `feed_id`/`feed_title`, unwraps `content_item`, drops `shortcut` tiles. VERIFY in `STATUS/T0333.log`.
- [x] T0347 **Playlists had reads captured, no read surface** — `SunoPlaylistService` owns both reads (`/playlist/me`, `/playlist/{id}/?page=N`). 1-based paging, no `has_more`. Bridge exposes playlists + invokables. Library grid swaps to open playlist. Live-verified 2026-10-30: 12 playlists. VERIFY in `STATUS/T0347.log`.
- [x] T0350 **Full build re-parses Qt headers in every TU** — shared `src/pch.hpp`. 1.8s vs 4.9s per TU (-63%). ~350 TUs. `-DCHADVIS_NO_PCH=ON` opts out. `CCACHE_SLOPPINESS=pch_defines,time_macros` keeps cacheable. VERIFY in `STATUS/T0350.log`.
- [x] T0352 **Mutation surface stays LocallyDisabled on cold start** — `setLocallyEnabled` writes resolver table but `GateResolver` not QObject, emits nothing. Fix: gate loop calls `onGatesChanged()` not `attach()`. Also `attemptEnable()` at service (re)creation. Committed `a5d2ae8`.
- [x] T0060 Dead members and methods with grep proof — removed Config overlay-element methods, `setTargetFps`, `feedAudio`, `g_app`, `cancelPoll`, `setSearchText`, `DownloadQueue::Item` fields. 75 lines gone. ctest 14/14.
- [x] T0061 `Result<T>` monadic surface unused and non-compiling — deleted `map`/`andThen`/`orElse`/`operator->`/`operator*`/`valueOr` + `TRY`/`TRY_VOID`. 5 call sites migrated to `.value()`. ctest 14/14.
- [x] T0062 `SunoBridge` god object: 1645 cpp + 429 hpp — split mutation cluster to `SunoMutationsSection`, download+play to `SunoDownloadSection`. Same QML singleton, names unchanged. 1645→1328 LOC. ctest 14/14.
- [x] T0063 ~120 LOC tolerant-JSON accessors duplicated across 4 files — one `vc::suno::json` namespace in `ClipParser.hpp`. Strict resolution: bools accept only real bool or "true"/"false". New tests pin every branch.
- [x] T0064 Duplicated enum→string and auth-failure maps — both moved to `ClerkAuthClient` as `auth::authFailureKindId` / `auth::authFailureMessage`. New vocabulary test. unit_tests pass.
- [x] T0068 ~15 dead model structs in `SunoModels.hpp` — deleted 15 structs (3.8 KB, zero refs). Kept `SunoPlanInfo`, `SunoModelLimits`, `SunoMetadata` (members of live structs).
- [x] T0069 Eleven endpoints wired in code but recorded `not-in-code` — all 11 rows now `wired`. Added paragraph: wired ≠ runtime-reachable (T0011 owns that).
- [x] T0071 ~120 dead QML-facing bridge properties/invokables — deleted 15 zero-reader items. 132→125 properties, 88→79 invokables. 27 remain with named owners. `--fast --tests` 390/390, ctest 14/14.
- [x] T0091 Windows lane passes `CMAKE_BUILD_TYPE` to multi-config generator — fixed: `-G Ninja`, `CMAKE_BUILD_TYPE=Release` real. vcpkg-manifest half moved to T0338.
- [x] T0092 `Qt6::OpenGL` linked but `OpenGL` never requested — `OpenGL` added to component list. `DBus` on `UNIX AND NOT APPLE` only. Verified on macOS/Windows/Linux.
- [x] T0094 macOS CI pays full bundle deployment on every push — `-DCHADVIS_FAST_ITERATION=ON` in macOS branch. 14m53s → 9m42s.
- [x] T0101 FFmpeg 9.0 `AVFrame::channels` → `ch_layout` migration already done — confirmed against libavutil 61.1.102. Blanket `-Wdeprecated-declarations` suppression removed. ctest 14/14.
- [x] T0111 `release.yml` believes macOS bundle packaging broken — release half fixed, dependabot updated (4 CPM pins, 7 workflows, fetch-metadata).
- [x] T0304 `docs/GEMINI_FLASH_ANALYSYS.md` orphan with misspelled filename — parked to `.backup_graveyard/`.
- [x] T0337 ChadVis Dark background pure black — `background` now `#0A0A0A`, plane ramp moved. `textDisabled` lightened to `#6A6A6A`. Contrast script clear.

## Dropped `[-]`

- [-] T0073 Per-item buttons killed by trailing full-bleed `MouseArea` — duplicate of T0004. Fixed under T0004.
- [-] T0182 Bookmarklet credential handoff — `__client` is `HttpOnly`, so `document.cookie` can never read the durable credential. Paste floor stands. Real replacement is T0199.
- [-] T0140 Remove stale dead-code comment in `ConfigLoader` — reviewed and kept: comment explains `operator[]` vs `get()` null-deref, not stale.

## Refuted claims — do not re-investigate

These were checked against source, not assumed. Do not re-file or re-audit.

- **Already-fixed items from superseded backlog**: `AudioAnalyzer` dead code gone, `DownloadQueue` checks `write()` return, `LyricsBridge::exportToAss` has QML caller, `video_url` origin check documented as gate, keychain on Windows/Linux implemented.
- **moc raw-string trap**: real but NOT triggered. moc 6.11.1 can't lex `R"(...//...)"`. No current literal triggers it. Workaround: `QJsonObject` initializers.
- **`LyricsSync` dead slots stay dead**: `onLineChanged`/`onWordChanged` deliberately unconnected. Wiring creates third writer.
- **No undeclared-source defect**: all 83 `src/**/*.cpp` and 58 `.qml` files declared in CMake.
- **`textPrimaryVariant` has no contrast bug**: 6.53–9.26:1 (AAA). Past bug was missing property → implicit `undefined`.
- **Colour tokens disciplined**: 38 hex literals tree-wide, 25 in `Theme.qml`. Inconsistency is in control chrome (58 raw Controls), not colours.
- **Config has zero key drift**: parser, `default.toml`, `CONFIG.md` agree. Only `token`/`cookie` absent (correct).
- **Config write atomic**: temp → check → close → chmod 0600 → fsync → rename. Windows `MoveFileExW` fallback.
- **Config round-trip lossless by design**: unknown keys dropped on save (documented). Four historical casualties tabulated. Only secrets dropped (structural).
- **Download no longer auto-plays on batch save**: `handleItemState:401` gates `addAndPlay` on `SaveAndPlay`.
- **No credential-exfiltration path**: `Authorization` under `isAllowedStudioApiUrl` only, `Cookie` on `AUTH_BASE` only. No `LOG_*` prints token/cookie.
- **No TLS relaxation, no proxy**: zero `ignoreSslErrors`, `sslErrors`, `setPeerVerify`. `ManualRedirectPolicy` everywhere.
- **OAuth lane correctly gated**: `setCaptureApprovedLaunch` no production caller. Gate owned by `OAUTH_REDIRECT_ANALYSIS.md`.
- **Every non-idempotent POST passes `retryOnUnauthorized=false`**: no mutation silently doubled.
- **401 retry bounded to one and epoch-guarded** at four points.
- **Bounded bodies enforced during read**: one byte past cap into probe, never appended.
- **projectM calls on render thread with context current**: cross-thread via atomics + mutex. Degrades gracefully.
- **FBO creation validated, texture format agrees**: `GL_RGBA8`/`GL_RGBA` both ways. Alpha forced opaque before readback.
- **Logger thread-safe, output bounded**: rotating sink 5 MB × 3.
- **`PresetScanner` static regex not a defect**: thread-safe function-local static.
- **No test hits network, real DB, keychain, or shells to ffmpeg**.
- **`qmllint` never caught QML bugs**: static analyzer over types/syntax, no runtime knowledge. Not verification gate.
- **Separate-`QWindow` own-context render-to-FBO-0 architecture CORRECT**: projectM maintainer says only `QOpenGLWindow` + direct native surface works. Do not migrate to `QOpenGLWidget` or share scene-graph context.
- **All projectM calls on creating thread**: official rule. Two separate instances in two threads safe; one instance from two threads not. Audio ingress not mutex-protected upstream — feed PCM between frames.
- **`projectm_set_frame_duration()` does not exist** in any version. Time-override is `projectm_set_frame_time()` @since 4.2.0 master-only.
