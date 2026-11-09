# Build speed investigation (2026-11-08, updated same day)

Status 2026-11-08: T0453 (module split) and T0456 (BuildConfig.hpp) landed and verified
(450/450 build, ctest 33/33). T0454 env exports landed; uncacheable threshold still to confirm from cold cache.
T0354 dropped: ld64.lld == Apple ld paired benchmark (1.051 s median each, 8 reps).
Why one class edit rebuilds much of the tree on the 2018 MBA (i5-8210Y, 2 physical / 4 logical, 8 GB).
Measured from `build-fast/.ninja_log`, `ninja -t deps`, `ccache -s`, `build.log`.

## Measured cascade

- Header fan-out (TUs per project header, `ninja -t deps`): `Types.hpp` 234, `Result.hpp` 183, `HttpPolicy.hpp` 141, `SunoModels.hpp` 140, `LyricsData.hpp` 126, `AuthTypes.hpp` 123, `ClerkAuthClient.hpp` 122, `SunoEndpoints.hpp` 120, `MangoLicense.hpp` 116, `SunoClient.hpp` 115.
- `SunoClient.hpp` includes 11 project headers; 31 files include it. Any endpoint/type edit re-parses ~1/3 of the tree.
- Moc umbrella: `project_lib_autogen/mocs_compilation.cpp` has 3,037 dep entries (48 `Q_OBJECT` classes in one TU), 30–50 s per rebuild. Any `Q_OBJECT` header edit rebuilds all 48.
- One `project_lib` (80 MB archive) change relinks 31 test executables at 1.3–2.3 s each. `unit_tests` is 27 MB; 584 MB of test binaries total.
- Slowest steps at `-O0 -g0`: app link 69 s, moc 35–50 s, `Application.cpp` 24–26 s, `SunoBridge.cpp` 19–23 s, `SunoClient.cpp` 19–21 s.
- `--tests` builds all 31 targets even for one test; `--test name` only filters ctest (`build.sh:do_build`).
- Configure is ~24 s per fresh profile (pkg-config, CPM, 130 MB presets download).
- `CHADVIS_VERSION` is `PUBLIC` on `project_lib`: a version bump changes every TU's command line (~350 TUs). Only `Application.cpp` + `CliUtils.cpp` use it.
- Fat TUs: `SunoBridge.cpp` 1,874 lines, `SunoClient.cpp` 1,668, `Application.cpp` 916. Already split once (`dc8acc6`); more remains.

## Cache layers: current state and options

### ccache (in use)
- 4.13.1, 62% hits, 20% uncacheable, `could_not_use_precompiled_header` x1,603.
- PCH + ccache conflict is the core defect: the PCH saves parse time (measured 63%, commit `8fe8f83`) but every `project_lib` TU misses the cache.
- Fixes: extend `CCACHE_SLOPPINESS` (`include_file_ctime,include_file_mtime,modules`), set `CCACHE_NOHASHDIR=1` (PCH portable across the 5 profile dirs, which currently cache independently), raise `max_size` from 5 GB (0.8 GB used), `CCACHE_DEPEND=1`. Add `<variant>` + `<source_location>` to `src/pch.hpp` (pulled in x183 via `Result.hpp`, not in PCH).
- Remote: ccache 4.13 has `remote-storage` (S3/GCS/Redis/HTTP). A shared bucket makes CI + local + any second machine hit the same entries. Cheaper than any distributed compile.

### sccache (installed, 0.17.0, never launched)
- Disk backend unused (0 requests). Would need `SCCACHE_CACHE_SIZE`, `CCACHE_PREFIX=sccache` (build.sh already prefers ccache and probes sccache second).
- sccache's `local-disk` + preprocessor mode duplicates ccache; net win only if you switch to its Redis/S3/GCS backends (same shared-bucket idea as ccache remote, one tool instead of two).
- Do not run both launchers on one TU set. Pick one; ccache is already wired everywhere.

### RAM-disk cache (not recommended on 8 GB)
- This box has 2.6–2.8 GB available of 8 GB. A ccache on `/dev/disk` RAM disk evicts OS file cache and risks paging under compile load.
- ccache on the SSD is already memory-backed via macOS file cache. Measure before moving; revisit only if `ccache -s` shows storage-read misses dominating.
- Disk is also 93% full (7.5 GB free); a 5 GB cache + 1.1 GB `build-fast/` + 650 MB of per-profile presets repeats is the pressure point. Deduplicate presets/images first.

### Ninja (keep)
- No replacement worth adopting. The build is ninja-correct; the cost is TU count, moc, and links, not the scheduler.
- `tup`/`Redo` would re-implement what CMake+Ninja do and break Qt/CPM integration. Better: reduce work (unity, modules, fewer links) and parallelise (more jobs). Linker already benchmarked (T0354).
- Knobs that exist and are unused here: `CMAKE_UNITY_BUILD` (batch 8 cuts ~350 TUs to ~44; trades incremental cost of 8 files per edit; only sensible with ccache fixed, since unity TUs are cacheable and parse headers once per batch), `CMAKE_PCH_INSTANTIATE_TEMPLATES`, `ninja -t deps` audits.
- Faster link: no swap. Paired benchmark 2026-11-08 (same `unit_tests` link line, 8 reps each): Apple ld 1.051 s median, ld64.lld 1.051 s median. `mold` is ELF-only.

### Network build on the 2013 MBA (cache server yes, remote compiler no)
- Do NOT use the 2013 MBA as a distributed compiler (`distcc`/`icecc`/Bazel remote execution). Identical toolchain synchronisation (same Clang, SDK, Qt path, Qt prefix `~/Qt`) plus network transfer of PCH + Qt headers per TU costs more than the 2 local cores save. Cross-OS or different macOS minor will also desynchronise object layouts and produce link failures that look like code bugs. `distcc`/`icecc` are not even installed here.
- DO use the 2013 MBA (if it is/will be on the same LAN) as a **cache server**: run `sccache` with `SCCACHE_REDIS` (or ccache `redis-storage`) on it. The 2018 MBA then compiles locally and shares hits with CI and any future machine. No toolchain parity problem because only cache blobs cross the wire.
- Same effect, simpler: a shared S3/GCS/Redis backend reachable from everywhere — no LAN dependency, no second machine to maintain. The 2013 MBA only wins on cost (free existing hardware) and survives CI evictions.

### Structural fixes (bigger than any cache)
- Split `project_lib` (one 80 MB archive, `TargetSetup.cmake:103`) into ~7 module libs by the existing `*_SOURCES` groups. Each gets its own moc TU and PCH; tests link their module, not the world. Kills ~30 redundant 17 MB links per `--tests` run and shrinks moc 48→~6–12 entries per TU. **Landed 2026-11-08 as 6 STATIC libs** (qt_add_qml_module needs STATIC; `project_bridges` owns the QML module; `project_lib` is an INTERFACE aggregator). Verify: `STATUS/T0453.log`, ctest 33/33.
- `BuildConfig.hpp` generated by `configure_file`; version/AVFILTER/AES flags leave `PUBLIC` definitions behind (only 3 files consume them). **Landed 2026-11-08**: `src/core/BuildConfig.hpp.in`, 5 consumers include it; `test_Version` green.
- Header hygiene: split `Types.hpp` (`<filesystem>`/`<span>` out of the 234-TU header), forward-declare behind `SunoClient.hpp`, split `SunoEndpoints.hpp` (668 lines, 120 TUs) per service. `iwyu` is not installed; it would find these in one pass.
- QML: 59 `qmlcache` `.cpp.o` in the fast loop. Interpreted QML in `CHADVIS_FAST_ITERATION` removes them; verify the Qt 6.12 switch via the Qt docs MCP before wiring (`TODO` T0452 already records a qmlcachegen segfault in this area).
- projectM: Homebrew `projectm` is 3.1.12 (verified 2026-11-08); MacPorts has no `projectm` port at all (verified `port info projectm` → not found). No system v4 exists on either manager, so the CPM source build (19 MB static per profile) cannot be removed via packages. Implication: keep the shared `.cpm-cache` (359 MB, already shared) and stop rebuilding projectM per profile.
- Presets: 130 MB download per fresh profile dir (5 profiles = 650 MB). `CHADVIS_BUNDLE_PRESETS=OFF` for fast, or share one presets dir by symlink.
- CI: `build.yml` ccache key deliberately excludes sources (ccache keys entries on preprocessed source; restore-keys walk back to the last good entry). Changing it fragments the bucket.
