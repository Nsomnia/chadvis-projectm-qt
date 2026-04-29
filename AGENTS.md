# ChadVis ProjectM-QT Refactor: AGENTS.md

## Sys Instructions and requirements
- **Caveman integration:** Ensure the caveman skill is installed for token efficiency first. `npx skills add JuliusBrussee/caveman`
- **Oh-my-opencode-slim:** OMO-slim is used on the users host loca system (not cloud sessions generally).
- **Never rm:** Never run `rm` commands or other destructive ones, instead append a date-time string to the file name and then move to appropriate directory within `.backup_graveyard/` for archival just-in-case. It doesn't nessecarily have to be commited to git in such a case.
- **git:** Ensure git commits are made frequently enough that there is a great history of operations that an agent can quickly parse when doing a `git history --oneline`. Also do very thorough detailed commit messages where appropriate so that an agent can get more details when and where needed.
- **General guidlines:** Worth a perusal when a new session, but not mandatory nor the law: `GENERAL_LLM_STARTING_PROMPT.md`.

## Legend & Rules
- `[ ]` untouched.
- `[~]` (large task that may take multiple context compressions or user opencode sessions) in-progress task.
- `[x]` Finished for user review. Only the user may remove tasks, however the LLM model/agent is free to refactor, add, and reorganize all elements freely. The user may make changes at anytime with this noted.
- `[?]` NOTE: something is blocking work being done.
- `[!]` User or model attention is needed as soon as possible.

## General Guidline
- Git is a powerful history for future agent chat sessions as they *always* exceed the compression or max token usage per chat limits and thus context is lost. This project spans
months and months with hundred(s) of millions of tokems invested over many models and generations.
- expanding on the previous: if you find code or project maintence that needs attention, whether immediate or far down the road, then add it to this list and you may just see another agent working on it simutaniously or have it done in the future (you may even work on it if you choose).
- Self prioritize task selection when given free reign over improving and expanding the codebase.
- Write modern code that uses appropriate programming paradigms.
- Unlmiited usage of tool calls as well as any packages on the users system such as debuggers, tui web browsers to dump sites like lynx, gh (github-cli), git, ddgr, and anything else avilable under `pacman -Qq`, If you require a specific tool then inform the user at end of output or stop poutput when needed immediately, This might include debuggers or language servers.
- A "Chad" and "Arch, BTW" vibe as if linus Torvolds and Linus Tech Tips were orchestrating this project, all while Richard Stallman gives gnu kung-fu wisdom in the backgroud meditating to song.
- Doducmentation, especially the root CHANGELOG.md and the history CHANGELOG.md(s) within docs/ should be kept up-to-date.
- Write test if truely benenfital to the project in the long run or if it allows you to verify logic is working programmatically.
- If you need to orchestrate a specific task to a specific model you may be able to find an approach. `opencode --models` will list models with any containing "free" being fair game . The configured API's nay not be operational or have quota so be warned. You can then run `opencode --model provider/name run "input_message_tokens"`.
- Use appropriate programming paradigms based on the current task. May be helpful to seed your context window with a brief overview of how you'll ensure maintance of current and writing of new portisons of the codebase. *Nesting files and directories is "free" whereas input and output tokens, as well as edit tool calls being tougher on huge code blocks, all ask cost with every token not being directly worked with. The user has been told 500 LOC for C++ 23 classes is a general good limit but your free to use best judgment.
- Follow industry stadards whenever both relevent and possible. Every aspect should not only be production ready but also ooze the vibe that it was done by a human to really try to keep everything both maintainable and to not become burden with slop (which is already happening in small degrees all over the project!).

## Ralph Loop/Self-Repetition/Infinite Mode
- You have complete freedom to, and are requested to, work as long as seems appropriate on this TODO list, so long as either there are tasks in this AGENTS.md TODO list, or you are aware of improvments that may be possible to the codebase. In these cases design and use a simple self-harness for repeating until your confident that your work is done and start with this harness wrapper, then end with a self promise statement of all tasks being done, compiled, tests run where appropriate, verified, logs checked, git commits made and then pushed to remote, and so-on until one step is fatally blocked or broken without being able to amend. 

---

## High Priority: Core Infrastructure (Milestones)
- [x] Refactor Main.qml with responsive Drawer and SplitView layout
- [x] Refactor AudioEngine for better organization and granular responsibility
- [x] Implement throttled bridge updates in VisualizerBridge/AudioBridge
- [~] Complete full migration of Sidebar panels (Library, Presets, Recording) — PlaylistBridge + RecordingBridge APIs fixed; LyricsBridge search/export still stubbed
- [x] Finalize robust persistence for all settings (SettingsBridge + TOML auto-save)
  - [x] Debounced auto-save (2s QTimer) on every SettingsBridge setter
  - [x] Explicit save() on app close via `onClosing` in Main.qml
  - [x] UIConfig expanded with `expandedPanel`, `sidebarWidth`, `drawerOpen`
  - [x] SettingsBridge Q_PROPERTYs for UI state (expandedPanel, sidebarWidth, drawerOpen)
  - [x] ConfigParsers parseUI/serialize updated for new UI fields
  - [x] Main.qml wired bidirectionally: accordion/drawer/sidebar ↔ SettingsBridge
  - [x] default.toml updated with new [ui] keys

## High Priority: Suno "Chad" Integration
- [x] Upgrade Suno API to feed/v3 for library access
- [~] **[NEW]** Implement "B-Side" feature set: Access hidden/beta endpoints (orchestrator, experiment gates) — Orchestrator wired into controller/bridge; endpoint map centralized; feature gates still unused
- [ ] **[NEW]** Implement Generation Surface: Full creation suite (prompt, style, seeds) with client-side overrides
- [~] **[NEW]** B-Side Chat/Orchestrator: Integration of experimental Suno conversational generation — Orchestrator wired, chat flows through bridge; workspace/session persistence still TODO
- [x] Implement infinite scrolling for Suno Library (Pagination)
- [ ] Refine Suno Library search and filtering (local + remote). Perhaps local should only be kept in-sync/tracked with remote if it stays within the defult file struture for the suno library. Ultimately downloading of tracks is optional.
- [ ] check filetypes that API scanners use such as `for i in suno api sniff scope burp etc_tools; do fd --hidden --no-ignore --estension json --extension tsv $idx /home/nsomnia; done`to verify all abilities on the public suno website are also able to be done in the package, as well as including b-side/testing/VIP and other hidden, experimental, secret, or gated, features. This can be expanded on by adding local logic such as advanced sorting options based on API data and keeping a suno libary database with all available options and any additional that we add for more features.

## Medium Priority: UI/UX & Polish
- [x] Implement smooth height animations for AccordionPanel transitions
- [x] Expand Settings.qml with comprehensive engine/recorder controls
- [ ] Implement "Modern Visualizer Overlay" with reactive text/graphics
- [ ] Add "Karaoke Master" mode: Synced lyrics with custom aesthetic overrides

## Low Priority: Random
- [ ] qss themes and theme switching. The user touched some theme files within resources that should have a section in one of the settings window canvas areas to change it. These default qss theme files can probaby be kept in a sub-directory.
- [ ] Ability for end user to add custom themes to an appropriate directory or custom directory and have them populated to be able to be switched to.
- [ ] Any tests that are either industry standard or useful for agentic workflows and the overall health of the codebase during development cycles.

## Strategic Goals: Maximum Customizability
- [ ] TOML-based "Chad Config": Every UI constant and engine parameter exposed
- [ ] Profile Support: Save/Load different UI themes and visualizer preset banks
- [x] Persistent state for all sidebar toggles and view modes

## Codebase Audit & Refactoring (2026-04-28)

Full audit of 19,294 LOC across 10 modules. 24 issues found, 18 fixed across 5 phases.

### Completed (Phases 1-4)
- [x] **#1/#12** Lyrics unification: LyricsFactory canonical parser + AlignedLyrics conversion methods; removed dead LyricAligner.hpp
- [x] **#2** SunoDatabase: Extracted `clipFromQuery()` helper (3 identical blocks → 1)
- [x] **#3** SettingsBridge: X-macro table + SettingMacros.hpp (453→~120 LOC)
- [x] **#4** Fixed broken QML Theme refs in KaraokeMaster.qml + KaraokeSettings.qml
- [x] **#5/#13** CLI argument table: CliArgs.inc + per-type X-macros + applyOverride<T> (687→584 LOC)
- [x] **#6/#23** SunoController lyrics dedup: Uses LyricsFactory directly, removed 84-line fallback parser
- [x] **#7/#8** SunoDownloader: Extracted `getDownloadDir()` (5x) + `sanitizeFilename()` (4x)
- [x] **#9/#22** Unified formatDuration/formatBytes into FileUtils (removed 3 local duplicates)
- [x] **#10** VisualizerBridge: Wired stubs to real VisualizerWindow (added visualizerWindow Q_PROPERTY, actualFps())
- [x] **#11** Namespace migration: `chadvis` → `vc::ui` in SunoPersistentAuth/SystemBrowserAuth
- [x] **#15** Lerp consolidation: Single `vc::lerp()` in Types.hpp (removed 3 duplicates)
- [x] **#16** Removed orphaned MIT license block from SunoAuthManager.hpp
- [x] **#17** Removed duplicate `#include <QtSql/QSqlDatabase>` from SunoDatabase.hpp
- [x] **#18** Removed duplicate GL state setup in VisualizerQFBO::render()
- [x] **#19** Archived dead LyricsLoader.hpp to `.backup_graveyard/lyrics/`
- [x] **#21** Removed orphaned `OverlayEngine` forward-decl from Types.hpp + Application.hpp
- [x] **#24** Removed stale `${KISSFFT_INCLUDE_DIRS}` from CMakeLists.txt

### Remaining (Phase 5+)
- [ ] **#14** OverlayBridge uses separate JSON persistence instead of Config — documented, left alone (JSON appropriate for list data; future: add debouncing)
- [ ] **#20** Remove ~20 stale cmake modules (only CPM.cmake + FindProjectM4.cmake are used; Conan.cmake, Vcpkg.cmake, Doxygen.cmake, etc. are dead)
- [ ] **#25** VisualizerBridge::toggleActive() still no-op (no pause/resume in VisualizerWindow)
- [ ] **#26** LyricsBridge has 6 stubbed methods (exportToSrt, exportToLrc, search, getUpcomingLines, getContextLines)
- [ ] **#27** Result.hpp custom type should migrate to `std::expected` (has map/andThen but missing orElse)
- [ ] **#28** FileUtils Color::fromHex()/toHex() should move to Types.hpp or dedicated Color.hpp
- [ ] **#29** PresetBridge presetToVariantMap() should become QAbstractListModel for large preset lists
- [ ] **#30** SunoBridge onLibraryUpdated() manual QVariantMap construction should use shared conversion

### Net Impact
- **38 files changed, -894 net LOC removed** (2022 deletions, 1128 insertions)
- 7 commits: Phase 1 quick wins → Phase 2 dedup → Phase 2 lyrics/namespace/lerp → Phase 3 SettingsBridge → Phase 3 CLI table → Phase 4 VisualizerBridge+lyrics → archive

## C++23 Agent Guidelines
- **Standard**: C++23 is the required minimum.
- **I/O**: Prefer `std::println` over `std::cout` or `printf`.
- **Error Handling**: Prefer `std::expected` for error handling (utilize monadic `.and_then()`/`.or_else()`).
- **Optionals**: Use monadic operations for `std::optional`.
- **Target**: Arch Linux (latest GCC/Clang) is the primary development target.

---
*Date: 2026-04-28 by AGENT (audit phases 1-4)*
