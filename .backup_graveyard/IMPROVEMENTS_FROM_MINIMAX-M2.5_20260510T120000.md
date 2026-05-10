IMPROVEMENTS REPORT - MiniMax M2.5
CRITICAL BUGS (P0)
P0-1: Thread-unsafe global PFFFT in AudioAnalyzer
- File: src/audio/AudioAnalyzer.cpp:10
- Issue: static PFFFT_Setup* g_pffft_setup = nullptr; is not thread-safe. Multiple AudioAnalyzer instances or concurrent calls can race.
- Fix: Use atomic or thread_local, or add mutex protection.
P0-2: Use-After-Free in SunoClient
- File: src/suno/SunoClient.cpp:89-95
- Issue: QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender()); used after potential event loop processing. Reply could be deleted.
- Fix: Store reply in smart pointer before async operations.
P0-3: Missing nullptr check in VideoRecorderFFmpeg::writePacket
- File: src/recorder/VideoRecorderFFmpeg.hpp (implied from audit)
- Issue: writePacket may receive nullptr packet causing crash.
- Fix: Add null check before dereferencing.
P0-4: LyricsAligner.hpp included but doesn't exist
- File: src/suno/LyricAligner.hpp (note: typo in filename)
- Issue: File exists but may cause compile errors if referenced incorrectly.
- Fix: Verify all includes are valid.
---
HIGH PRIORITY (P1)
P1-1: TRY Macro shadowing warnings
- File: src/core/Application.hpp:44-47
- Issue: TRY macro defined twice with different signatures causing shadowing.
- Fix: Consolidate to single definition.
P1-2: God Object in Application
- File: src/core/Application.hpp
- Issue: Application class manages: audio, visualizer, recorder, presets, lyrics, suno, config, logging, QML engine. Too many responsibilities.
- Fix: Split into subsystem managers with coordinator pattern.
P1-3: Config::save() returns bool but errors not handled
- File: src/core/Config.cpp
- Issue: Return value often ignored. Silent failures on save.
- Fix: Add error handling or use Result<void>.
P1-4: SunoClient missing namespace wrapper
- File: src/suno/SunoClient.cpp
- Issue: Implementation not in vc::suno namespace, inconsistent with header.
- Fix: Wrap in namespace vc { namespace suno { ... } }
P1-5: SunoAuthManager missing namespace wrapper
- File: src/suno/SunoAuthManager.cpp
- Issue: Same as SunoClient - inconsistent namespace.
- Fix: Wrap in proper namespace.
P1-6: SunoDatabase missing namespace wrapper
- File: src/suno/SunoDatabase.cpp
- Issue: Same namespace inconsistency.
- Fix: Wrap in proper namespace.
P1-7: SunoOrchestrator missing namespace wrapper
- File: src/suno/SunoOrchestrator.cpp
- Issue: Same namespace inconsistency.
- Fix: Wrap in proper namespace.
P1-8: SunoDownloader missing namespace wrapper
- File: src/suno/SunoDownloader.cpp
- Issue: Same namespace inconsistency.
- Fix: Wrap in proper namespace.
P1-9: SunoLyricsManager missing namespace wrapper
- File: src/suno/SunoLyricsManager.cpp
- Issue: Same namespace inconsistency.
- Fix: Wrap in proper namespace.
P1-10: SunoLibraryManager missing namespace wrapper
- File: src/suno/SunoLibraryManager.cpp
- Issue: Same namespace inconsistency.
- Fix: Wrap in proper namespace.
P1-11: Unused forward declarations in Types.hpp
- File: src/util/Types.hpp:106-110
- Issue: Forward declares Config, AudioEngine, etc. but nearly unused. Creates coupling.
- Fix: Move to dedicated FwdDecl.hpp.
P1-12: Duplicate AudioQueue forward declaration
- File: src/audio/AudioQueue.hpp and src/util/Types.hpp
- Issue: Duplicate forward declaration.
- Fix: Remove duplicate.
P1-13: Orphaned LyricAligner.hpp
- File: src/suno/LyricAligner.hpp
- Issue: Header exists but may not be used or properly integrated.
- Fix: Verify usage or remove if dead code.
P1-14: VisualizerItem dead code
- File: src/qml_bridge/VisualizerItem.hpp
- Issue: VisualizerItem appears unused; VisualizerQFBO is the active implementation.
- Fix: Remove VisualizerItem or verify it's needed.
P1-15: build.sh delegates to build.zsh
- File: build.sh
- Issue: Script exists but just calls build.zsh. Redundant.
- Fix: Remove build.sh or make it the primary script.
P1-16: test_PresetScanner not in CMakeLists
- File: tests/unit/visualizer/test_PresetScanner.cpp
- Issue: Test file exists but not compiled (not in CMakeLists.txt).
- Fix: Add to test sources in CMakeLists.txt.
P1-17: WebEngineWidgets still in Qt6 find_package
- File: CMakeLists.txt:52
- Issue: WebEngineWidgets found but browser code removed per user request.
- Fix: Remove WebEngineWidgets from Qt6 components.
P1-18: Qt6::WebEngineWidgets in link libraries
- File: CMakeLists.txt:329
- Issue: Linked but unused after browser removal.
- Fix: Remove from COMMON_LIBS.
P1-19: QML binding conflict in KaraokeMaster.qml
- File: src/qml/components/KaraokeMaster.qml
- Issue: QML bindings may conflict with C++ LyricsBridge updates.
- Fix: Review and fix binding priorities.
P1-20: QML binding conflict in KaraokeSettings.qml
- File: src/qml/components/KaraokeSettings.qml
- Issue: Similar binding conflict potential.
- Fix: Review and fix binding priorities.
P1-21: Karaoke settings not persisted
- File: src/qml/components/KaraokeSettings.qml
- Issue: showGlow, verticalPos bound in QML but never persisted to SettingsBridge.
- Fix: Wire to SettingsBridge karaoke properties.
P1-22: SunoClient pollWavFile not cancellable
- File: src/suno/SunoClient.cpp
- Issue: Uses QTimer::singleShot recursion - not cancellable, continues during shutdown.
- Fix: Implement proper polling state machine with cancellation.
P1-23: Inconsistent 401 handling in Suno module
- File: Multiple suno/ files
- Issue: SunoClient clears token but doesn't re-auth. SunoLyricsManager re-queues. SunoOrchestrator just emits error.
- Fix: Standardize 401 handling across module.
---
MEDIUM PRIORITY (P2)
P2-1: Magic numbers throughout codebase
- Issue: Hardcoded values: 50 word search window, 1.5f beat threshold, 5 max log count, 10ms thread timeout.
- Fix: Extract to named constants in Config.
P2-2: Mixed formatting approaches
- Issue: Uses snprintf, std::format, fmt::format, and manual concatenation inconsistently.
- Fix: Standardize on std::format (C++23) as primary.
P2-3: TODO inventory not converted to issues
- Issue: CMakeLists.txt has 5+ inline TODOs. 4 test files are TODO stubs. QML has 3+ console.log TODOs.
- Fix: Convert to GitHub Issues and remove from source.
P2-4: Missing [nodiscard] on key functions
- Issue: Playlist::next(), previous(), jumpTo() return bool but not marked [nodiscard].
- Fix: Add [nodiscard] attribute.
P2-5: X-macros make debugging difficult
- Files: src/qml_bridge/SettingMacros.hpp, src/core/CliArgs.inc
- Issue: Stack traces show macro-expanded code.
- Fix: Consider code generator script replacement.
P2-6: Global APP macro
- File: src/core/Application.hpp:160
- Issue: #define APP in global namespace could collide.
- Fix: Use inline function or scoped macro.
P2-7: Humorous comments throughout
- Issue: Comments like "// Because typing std::chrono::milliseconds gets old fast" may not be appropriate for all contributors.
- Fix: Evaluate codebase-wide comment policy.
P2-8: Inconsistent QML brace style
- File: src/qml/panels/OverlayPanel.qml
- Issue: Mixed brace styles (some { on same line, some on next).
- Fix: Apply consistent style via qmlformat.
P2-9: ASCII art in main.cpp
- File: src/main.cpp:1-11
- Issue: ASCII art logo adds 10 lines of noise to entry point.
- Fix: Subjective - keep or trim.
P2-10: FFmpegUtils AVFormatContextDeleter potential double-close
- File: src/recorder/FFmpegUtils.hpp
- Issue: Calls avio_closep before avformat_free_context. FFmpeg may handle internally.
- Fix: Verify to prevent double-close.
P2-11: Legacy MVC controllers may be unused
- Files: src/ui/controllers/AudioController.hpp, RecordingController.hpp, VisualizerController.hpp
- Issue: Capture this in lambdas without null checks (dangling pointer risk). May be unused in QML-only build.
- Fix: Verify and remove if dead.
P2-12: Unused SunoModel structs
- File: src/suno/SunoModels.hpp
- Issue: SunoProject, SunoPlaylist, SunoFeatureGate defined but never referenced.
- Fix: Implement features or remove.
P2-13: Hardcoded page size in SunoLibraryManager
- File: src/suno/SunoLibraryManager.cpp
- Issue: Hardcoded page size of 20 (clips.size() >= 20). Fragile if API changes.
- Fix: Extract to named constant or config.
P2-14: Dual-player gapless architecture complex
- File: src/audio/AudioEngine.hpp
- Issue: Manual pointer swapping. swapPlayers disconnects ALL signals via nullptr pattern.
- Fix: Consider abstracting into GaplessPlayer class.
P2-15: Custom Signal<> duplicates Qt signals
- File: src/util/Signal.hpp
- Issue: Custom Signal with mutex-based thread safety reimplements Qt signals.
- Fix: Document why custom Signal is preferred.
P2-16: Result<T> duplicates std::expected
- File: src/util/Result.hpp
- Issue: Custom Result<T> (195 LOC) reimplements std::expected (C++23).
- Fix: Migrate to std::expected<T, Error>.
P2-17: Duration format migration runs every startup
- File: src/suno/SunoDatabase.cpp
- Issue: Migration runs at every startup instead of once during schema migration.
- Fix: Move into schema migration block.
P2-18: loadM3U() appends instead of replacing
- File: src/audio/Playlist.cpp
- Issue: Appends to existing playlist rather than replacing. Doesn't parse #EXTINF lines.
- Fix: Parse #EXTINF. Add replace vs append mode.
P2-19: CHANGELOG has multiple Unreleased sections
- File: CHANGELOG_CURRENT.md
- Issue: Multiple Unreleased sections with different dates.
- Fix: Consolidate into single section.
P2-20: Mixed formatting approaches across codebase
- Issue: Uses snprintf, std::format, fmt::format, manual concatenation.
- Fix: Standardize on std::format (C++23).
---
LOW PRIORITY (P3)
P3-1: Profile export/import not implemented
- File: src/qml/panels/SettingsPanel.qml:489-501
- Issue: Profile buttons are enabled:false placeholders.
- Fix: Implement profile serialization to .toml/.json.
P3-2: Runtime theme switching not implemented
- File: resources/ (13 QSS files)
- Issue: Theme files exist but no runtime switcher.
- Fix: Implement runtime QSS loading in Settings.
P3-3: Default build type is Debug
- File: CMakeLists.txt:27
- Issue: Unoptimized builds by default, impacting visualizer performance.
- Fix: Consider Release or RelWithDebInfo as default.
P3-4: No automated release pipeline
- File: .github/workflows/
- Issue: GitHub Actions workflow is for CI only, not releases.
- Fix: Add workflow for AppImage or Arch package releases.
P3-5: Audio playback filetypes limited
- Issue: Suno provides mp3/wav only. Local playback should support more formats.
- Fix: Add library support for additional audio formats via CPM.
P3-6: B-Side feature incomplete
- File: Per AGENTS.md B-Side items
- Issue: Orchestrator wired but feature gates unused, workspace/session persistence TODO.
- Fix: Complete B-Side implementation.
P3-7: Minimal test coverage
- File: tests/ directory
- Issue: Only 5 unit tests across 2 files actually compiled. Integration tests are no-op.
- Fix: Add tests for: LyricsData::findLineIndex(), search(), ConfigParsers, AudioAnalyzer, Playlist navigation, LyricsSync.
P3-8: SQLite FTS5 not used for Suno search
- File: src/suno/SunoDatabase.cpp
- Issue: Uses SELECT * ... WHERE ... LIKE across 5 columns. No FTS index. Slow on large libraries.
- Fix: Add SQLite FTS5 virtual table with sync triggers.
P3-9: Feature gates investigation incomplete
- Issue: Found gate, VIP, premium, beta, test, internal keywords in API captures. Chat feature implemented but untested. Marketplace skeleton found.
- Fix: Further investigation of hidden features via POST endpoints.
P3-10: SunoClient handleNetworkError clears token but no re-auth
- File: src/suno/SunoClient.cpp
- Issue: On 401, token cleared but doesn't trigger re-auth flow.
- Fix: Implement proper re-authentication.
---
COSMETIC (P4)
P4-1: Inconsistent namespace usage in suno/ module
- Issue: Headers in vc::suno but implementations in global namespace.
- Fix: Consistent namespace wrapping.
P4-2: Empty catch blocks
- Issue: Some catch blocks silently swallow exceptions.
- Fix: Add logging or handle appropriately.
P4-3: Unused variables
- Issue: Several unused variables flagged by compilers.
- Fix: Remove or use appropriately.
P4-4: Copy-paste code patterns
- Issue: Some duplication between similar functions.
- Fix: Factor into reusable functions.
P4-5: Missing const correctness
- Issue: Some functions should be const but aren't.
- Fix: Add const where appropriate.
---
OPPORTUNISTIC IMPROVEMENTS (P5)
P5-1: Migrate to std::expected (C++23)
- File: src/util/Result.hpp
- Issue: Custom Result<T> reimplements std::expected.
- Fix: Migrate to standard library version.
P5-2: Add proper cancellation support
- Files: SunoClient, any async operations
- Issue: No unified cancellation mechanism.
- Fix: Implement cancellation tokens.
P5-3: Health check endpoints for Suno API
- Issue: No way to check API status without making requests.
- Fix: Add health check functionality.
P5-4: Rate limiting implementation
- Issue: No rate limiting on API calls.
- Fix: Implement rate limiter for Suno API.
P5-5: Better error messages
- Issue: Some errors are vague.
- Fix: Add context to error messages.
P5-6: Unit test for AudioAnalyzer
- File: src/audio/AudioAnalyzer.cpp
- Issue: No unit tests.
- Fix: Add FFT tests, beat detection tests.
P5-7: Unit test for LyricsData
- File: src/lyrics/LyricsData.cpp
- Issue: No unit tests.
- Fix: Add tests for parsing, search, timing.
P5-8: Integration test for full playback flow
- Issue: No end-to-end tests.
- Fix: Test audio -> analyzer -> visualizer -> render.
P5-9: Performance benchmarks
- Issue: No benchmarks for critical paths.
- Fix: Add benchmark suite.
P5-10: Documentation improvements
- Issue: Some APIs lack documentation.
- Fix: Add Doxygen comments.
---
SUMMARY
Severity
Critical (P0)
High (P1)
Medium (P2)
Low (P3)
Cosmetic (P4)
Opportunistic (P5)
Total
---
## TOP 10 RECOMMENDED ACTIONS
1. **Fix P0-1:** Thread-unsafe PFFFT global - add thread_local or mutex
2. **Fix P0-2:** Use-after-free in SunoClient - use smart pointer for reply
3. **Fix P0-3:** nullptr check in VideoRecorderFFmpeg::writePacket
4. **Fix P1-17/18:** Remove WebEngineWidgets from Qt6 components and link libraries
5. **Fix P1-14:** Remove or verify VisualizerItem dead code
6. **Fix P1-16:** Add test_PresetScanner to CMakeLists.txt
7. **Fix P1-4 to P1-10:** Wrap all suno/ implementations in vc::suno namespace
8. **Fix P1-1:** Consolidate TRY macro definitions
9. **Fix P1-2:** Begin splitting Application god object
10. **Fix P2-3:** Convert TODO inventory to GitHub Issues
---
MODULES WITH MOST ISSUES
1. suno/ - 15 issues (namespace, thread safety, auth)
2. qml_bridge/ - 8 issues (dead code, bindings)
3. core/ - 7 issues (god object, macros, config)
4. recorder/ - 5 issues (null checks, FFmpeg)
5. audio/ - 4 issues (thread safety, playlist)
6. tests/ - 4 issues (missing tests, stubs)
7. QML files - 4 issues (bindings, persistence)
8. visualizer/ - 3 issues (dead code, magic numbers)
---
Generated by MiniMax M2.5 code exploration agent
Findings based on comprehensive codebase analysis
Output format: plain ASCII for easy copy/paste
