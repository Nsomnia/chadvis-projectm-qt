# AGENTS.md

Repository-wide operating instructions for autonomous development agents.

**Primary objective:** Continuously improve, complete, test, and maintain this project without requiring the user to repeatedly prompt the agent to continue.

**Authoritative project records:**
- Backlog and next actions: [`TODO.md`](TODO.md)
- Change history: [`CHANGELOG.md`](CHANGELOG.md)
- Documentation index: [`docs/README.md`](docs/README.md)
- Task leases and auditing: [`scripts/task.sh`](scripts/task.sh)
- Dependency policy: [`docs/dev/DEPENDENCIES.md`](docs/dev/DEPENDENCIES.md)
- Release policy: [`docs/dev/RELEASING.md`](docs/dev/RELEASING.md)

Read the relevant owning document before changing the behavior it governs. Keep this file focused on durable instructions; put detailed implementation plans, API findings, architecture decisions, and task state in their appropriate documents.

---

# 1. Autonomous Development: Highest-Priority Directive

## 1.1 Work continuously without prompting

**Never treat completion of a single task as completion of a development session.**

The default operating mode is a continuous, self-directed development loop.

After completing an individual TODO item, immediately identify and begin the next worthwhile task. Do not wait for the user to say "continue", "next", "keep going", or anything equivalent.

Do not ask the user what to work on next when an actionable backlog already exists.

Do not finish a response merely because:
- One bug was fixed.
- One feature was implemented.
- One test passed.
- A build completed.
- A documentation section was updated.
- A commit was created.
- A particularly difficult task was completed.

Those are checkpoints, not reasons to stop.

**The agent is responsible for maintaining its own momentum.**

## 1.2 Mandatory development loop

Repeat this sequence for each task:

1. **Orient.** Inspect the current Git status, active branch, recent commits, relevant source files, and applicable documentation.
2. **Select.** Read `TODO.md` and identify the highest-priority actionable task whose dependencies are satisfied.
3. **Plan.** Establish the smallest useful implementation and its observable completion criteria. Avoid spending excessive tokens planning straightforward work.
4. **Record.** Add newly discovered work to `TODO.md` immediately. Update task status, dependencies, and acceptance criteria as appropriate.
5. **Implement.** Complete a cohesive vertical slice. Do not leave obvious integration work behind merely because the first part compiles.
6. **Verify.** Run appropriate builds, tests, static analysis, or runtime checks. Investigate failures rather than declaring success prematurely.
7. **Review.** Inspect the actual diff for bugs, incomplete wiring, unrelated changes, security problems, and missing documentation.
8. **Document.** Update the owning documentation and `CHANGELOG.md` where appropriate.
9. **Commit.** Create a meaningful Git commit when the changes form a verified checkpoint.
10. **Repeat.** Return immediately to step 1 and select the next TODO item.

A progress update to the user is not a stopping point. Provide concise milestone updates when useful, then continue development.

## 1.3 Stop conditions

Stop autonomous implementation only when one of these conditions applies:

- No worthwhile actionable backlog remains after a reasonable repository audit.
- An operation requires a genuine owner-only decision or additional authorization.
- An external dependency, unavailable credential, or infrastructure failure makes further progress impossible.
- A potentially destructive or irreversible operation requires explicit authorization.
- The execution environment or available context cannot safely support further work.

When blocked, record the blocker, what was attempted, the evidence, and a concrete next action in `TODO.md`.

When approaching a hard context or execution limit, leave a recoverable checkpoint:
- Update task status.
- Record unfinished work and the exact next step.
- Record verification already performed.
- Commit completed work where appropriate.

**Never manufacture busywork merely to keep the loop running.** When the meaningful backlog is exhausted, perform a focused audit and stop.

## 1.4 Work autonomously within the existing authorization

The user has authorized broad discretion over routine software development.

Do not interrupt development to request permission for ordinary decisions such as:

- Refactoring poorly structured code.
- Splitting oversized classes or files.
- Improving architecture and maintainability.
- Adding missing tests.
- Fixing discovered defects.
- Updating documentation.
- Improving the build system.
- Installing reasonable development dependencies.
- Improving CI workflows.
- Reorganizing source directories.
- Adding useful configuration options.
- Maintaining `TODO.md` and `CHANGELOG.md`.
- Making ordinary local Git commits.

If a repository instruction, development script, harness, or existing implementation is demonstrably defective, improve it when the correction is clearly within the project's scope. Document the decision and verify the result.

Do not blindly preserve bad historical decisions merely because older agents implemented them.

Reserve questions for actual owner decisions, irreversible external actions, security-sensitive changes, or genuinely ambiguous product requirements that cannot be resolved safely through the existing project direction.

---

# 2. TODO.md: The Continuous Development Queue

`TODO.md` is the authoritative source of work and the primary mechanism for preserving development continuity across sessions.

Read its grammar and existing conventions before modifying it.

## Mandatory rules

- Read `TODO.md` at the beginning of every session.
- Select the next task independently.
- Maintain it throughout development, not just at the end.
- Add discovered defects, technical debt, missing tests, incomplete integrations, and worthwhile improvements as they are discovered.
- Update task status as work progresses.
- Record dependencies and acceptance criteria where useful.
- Mark a task complete only after its actual completion criteria have been satisfied.
- Preserve existing identifiers and formatting conventions.
- Remove duplicates and consolidate obsolete or redundant entries.
- Keep the highest-value actionable work easy to find.
- Do not replace the existing granular backlog with a vague high-level roadmap.

### Continuous discovery

Whenever implementation reveals another necessary task, record it immediately.

Examples:
- Discovering an API field that needs a database migration.
- Finding that an existing UI control is not connected to application state.
- Finding that a successful build does not exercise a particular runtime path.
- Discovering a platform-specific dependency issue.
- Finding an incomplete error-handling path.
- Discovering an architectural improvement needed to support a planned feature.

Do not rely on conversational memory to preserve these discoveries.

### Task prioritization

Use the existing priority conventions. When a priority must be assigned, generally follow:

- **P0:** Security, data integrity, serious regressions, broken builds, or release blockers.
- **P1:** Important user-facing functionality, major defects, and critical missing integration.
- **P2:** Feature completeness, usability, architectural improvements, and substantial technical debt.
- **P3:** Refinement, optimization, convenience, and lower-impact improvements.

Prefer completing cohesive tasks rather than repeatedly starting unrelated work.

### Task leases

`./scripts/task.sh` supports claim, heartbeat, release, and audit operations.

Use leases when work is genuinely shared or multiple agents may modify the same task.

Avoid unnecessary leasing for ordinary single-agent development.

The task system should facilitate development, not become administrative overhead.

Expired `.claim` files must be released by the claimer or swept by `task.sh audit` before a re-claim; never leave an expired claim on disk to block `O_EXCL` re-claim.

---

# 3. Project Direction and Product Vision

The long-term objective is a sophisticated desktop music-production and Suno companion application.

The application should provide substantial functionality beyond what the Suno remote interface offers, using an authenticated remote account and a persistent local database where appropriate.

The planned product includes:

1. **Suno desktop client:** Library browsing, account synchronization, song management, generation controls, metadata, and remote functionality.
2. **Local music database:** SQLite-backed storage, indexing, search, organization, history, and enhanced local workflows.
3. **Song composition:** A complete textual lyrics and style editor with a genuinely useful WYSIWYG editing experience. Support creating, saving, loading, and revisiting compositions.
4. **Music video creation:** A visual composition interface with a sequencer, timelines, media, effects, and text overlays suitable for promotional content.
5. **Audio workstation:** An eventual modest but useful audio-production environment.

These are project goals, not a mandate to implement everything simultaneously.

Use `TODO.md` to turn the vision into manageable, incremental tasks.

Preserve existing working functionality while expanding it.

Prefer delivering useful, complete features over perpetually restructuring the architecture.

---

# 4. Software Architecture and Code Quality

## 4.1 General architecture

Use modern, cohesive, purpose-specific components.

Favor:
- Single responsibility.
- High cohesion.
- Loose coupling.
- Explicit interfaces.
- Dependency injection where useful.
- Small, independently testable units.
- Clear ownership of application state.
- Predictable error handling.
- Strong separation between UI, domain logic, infrastructure, and external services.

A class should have one clearly identifiable primary responsibility.

A filename should communicate what its contents do without requiring someone to open it.

Organize source using nested directories that reflect architectural responsibilities rather than accumulating unrelated files in large directories.

Prefer one primary class per source/header pair when appropriate. Related declarations, templates, lightweight helpers, and QML components may reasonably share files when that makes the design clearer.

## 4.2 File and class size

Use approximately 200–300 lines of implementation as a soft design target for ordinary classes.

At around 300–400 lines, consider whether responsibilities should be separated.

Larger files are acceptable when their contents are genuinely cohesive.

These are guidelines, not artificial hard limits.

Do not split code into an excessive number of tiny classes merely to satisfy a line count.

Prefer maintainable architecture over arbitrary metrics.

## 4.3 Language and framework

- C++23, enforced at configure time.
- Qt6 and Qt Quick/QML for the desktop application.
- CMake and Ninja for project configuration and builds.
- CPM/FetchContent for suitable CMake-managed source dependencies.
- SQLite for persistent application data.
- Modern C++ resource and ownership management.
- `std::expected` and its monadic operations, or `vc::Result<T>`, for expected failures.
- Exceptions should be reserved for genuinely exceptional paths.

Use the project's existing architectural conventions where they are sound.

Do not introduce an entirely new framework or major dependency without a concrete benefit.

## 4.4 Error handling

Expected failures should be represented explicitly.

Never silently swallow:
- Remote API errors.
- Database errors.
- Failed file operations.
- Invalid application state transitions.
- FFmpeg failures.
- Audio-device failures.
- Threading and synchronization errors.

Provide actionable error information without exposing credentials or private account data.

---

# 5. User Interface and Customization

The interface is a major product feature, not an afterthought.

## Design objectives

Build visually distinctive, polished Qt/QML interfaces.

Use modern desktop design with mobile-inspired interaction patterns where appropriate.

Consider:
- Reusable widgets and components.
- Expandable accordion sections.
- Responsive layouts.
- Reactive controls.
- Animated transitions.
- Progress indicators and spinners.
- Tooltips and useful contextual assistance.
- Sliders, toggles, segmented controls, and advanced selectors.
- Timeline and sequencer interfaces.
- Dockable or reorganizable panels where beneficial.
- Context menus and keyboard shortcuts.
- Consistent spacing, typography, icons, and visual hierarchy.
- Configurable themes and appearance.
- High-DPI support and scalable text.

Avoid generic, unfinished-looking interfaces.

Build reusable QML components rather than repeatedly duplicating UI implementations.

Separate visual presentation from application logic.

Do not block the UI thread with network, database, audio, or long-running operations.

## Configuration is a first-class feature

**Any behavior, appearance, or workflow choice that a user could reasonably expect to customize should have an appropriate configuration mechanism.**

Support customization through:

1. The application's TOML configuration file or files.
2. Command-line options.
3. The Settings interface.

Use sensible defaults while exposing meaningful control.

Examples include:
- Theme and accent colors.
- Layout and panel preferences.
- Font sizes and interface scaling.
- Window dimensions and startup behavior.
- Audio and recording parameters.
- Video output settings.
- Default export locations.
- Sequencer behavior.
- Playback preferences.
- Remote synchronization behavior.
- Generation defaults.
- Editor behavior.
- Performance settings.

Use a documented configuration precedence. A sensible default is:

`CLI overrides > persistent user settings > TOML configuration > built-in defaults`

Avoid duplicated configuration state and hardcoded values that should be user-adjustable.

Credentials are an exception. Store them through `CredentialStore`, not ordinary TOML configuration.

Every setting added should have an appropriate default, validation, and a clear description.

---

# 6. SQLite and Local Data Architecture

The local database is a major part of the application's value.

The remote Suno account should remain the source of truth for remote-owned objects, while the application provides additional local functionality and persistent organization.

Maintain a clear distinction between:
- Remote-owned data.
- Locally created data.
- User annotations and preferences.
- Cached remote data.
- Synchronization state.

Use proper SQLite transactions, indexes, foreign keys, schema migrations, and error handling.

Important requirements:

- Migrations must be repeatable or safely tracked.
- Failed migrations must not leave partially updated schemas.
- Synchronization must avoid accidental duplication.
- Remote updates must not overwrite unrelated local data.
- Preserve unknown remote fields where useful for forward compatibility.
- Do not silently lose locally created metadata.
- Design synchronization to tolerate interruptions.
- Keep local search and organization useful when offline.

Create database tests for migration, transaction rollback, synchronization, and data-integrity behavior where appropriate.

Do not use the database as an excuse to reproduce unnecessary parts of the remote service. Focus on features that improve the desktop experience.

---

# 7. Testing and Verification

Prioritize functionality, but never equate compilation with completion.

Tests should be written when they provide meaningful verification.

Do not create tests simply to increase test counts.

Prioritize tests for:
- Complicated business logic.
- Data transformations.
- Threading and synchronization.
- Database transactions.
- Remote API response handling.
- File and media operations.
- FFmpeg return codes.
- Audio processing.
- State transitions.
- Memory ownership and resource cleanup.
- Previously difficult-to-reproduce defects.

Use automated agents, test frameworks, integration tools, mock servers, browser automation, and third-party tools where they materially improve testing.

Test external integrations without modifying real account data unnecessarily. Credits are available at 10,000 per month.

Use mocks and fixtures for repeatable regression tests. Reserve live tests for behaviors that cannot reasonably be established otherwise.

Prefer tests that catch meaningful regressions over tests that merely duplicate implementation details.

## Build commands

```bash
./build.sh --fast --tests
./build.sh --fast --tests --safe

./build.sh --budget

./build.sh --tsan --tests && ./build.sh --tsan --safe
./build.sh --asan --tests && ./build.sh --asan --safe
```

Build profiles:

`--fast`, `--debug`, `--release`, `--tsan`, `--asan`, and `--ubsan`.

Each profile maintains a separate build directory (`build-fast/`, `build-tsan/`, etc.).

Never apply one profile's compiler flags to another profile's build directory.

See `./build.sh --help` for additional options.

Important build realities:

- `ctest --test-dir build` can find zero tests and still exit successfully. Specify the correct profile directory and inspect the discovered test count.
- The executable is a `MACOSX_BUNDLE`.
- macOS executable: `build*/chadvis-projectm-qt.app/Contents/MacOS/chadvis-projectm-qt`
- Other platforms: `build*/chadvis-projectm-qt`
- Headless skips are not successful runtime verification.
- A green CI run is not proof of working OpenGL, audio, recording, or long-duration stability.
- Run `./build.sh --budget` before diagnosing resource-related build failures.
- `build.sh` intentionally limits local resource utilization.
- `integration_tests` is deliberately outside the `--tests` build list, so `--tests` never rebuilds it. Before trusting an integration result, run `./scripts/check-integration-fresh.py` (one command; it fails with the rebuild command when the binary is older than `tests/integration/` sources). Rebuild it with `./build.sh --target integration_tests` (or `cmake --build <dir> --target integration_tests` for a non-default profile).

Use ThreadSanitizer for applicable recorder/session/threading changes and AddressSanitizer for applicable memory/resource work. Use UndefinedBehaviorSanitizer where it provides value.

Record actual test counts, skipped tests, and meaningful verification results.

---

# 8. Continuous Integration and GitHub

The development machine is a 2018 MacBook Air with a T2 chip.

Local development must account for constrained CPU, RAM, thermal headroom, and storage.

**Use GitHub-hosted CI as an extension of the local development machine.**

Linux and Windows builds and tests should be exercised through the project's existing GitHub Actions workflows whenever useful.

Inspect the actual workflow definitions before invoking them.

Useful commands include:

```bash
gh auth status
gh workflow list
gh workflow view WORKFLOW
gh run list
gh run view RUN_ID
gh run watch RUN_ID
gh run view RUN_ID --log
```

For workflows explicitly configured for manual execution:

```bash
gh workflow run WORKFLOW --ref BRANCH
```

Only dispatch workflows that actually support `workflow_dispatch`.

Use Linux and Windows CI to:
- Find platform-specific compilation problems.
- Verify dependency discovery.
- Validate installation and deployment.
- Exercise automated tests.
- Produce platform artifacts.
- Check that filenames, paths, and build scripts are portable.

GitHub Actions provides hosted Linux, Windows, and macOS runners. Dependabot is the dependency-update service, not the runner infrastructure. Use the actual Actions workflows for cross-platform builds and manual CI execution.

Do not assume a workflow succeeded because it was dispatched. Inspect its completion, jobs, artifacts, and logs.

## Existing CI gates

- `guards.yml`: workflow and repository guards.
- `build.yml`: Ubuntu GCC/Clang and macOS builds.
- `windows.yml`: Windows Release build, headless entries, and `windeployqt` artifact.
- Sanitizer lanes report and count; they are not necessarily gating.

Additional important rules:

- `integration_tests`, `integration_gl_tests`, and `test_OffscreenRenderSpike` skip headless.
- Sanitizer lanes that produce no logs are broken, not clean.
- Never hide a failing gate with `continue-on-error`.
- Run `./scripts/check-workflows.py` after modifying `.github/workflows/`.

A workflow that parses locally is not necessarily accepted by GitHub.

In particular, repeated YAML keys can result in rejected workflows or runs with zero jobs. Validate workflow structure and actual GitHub execution.

---

# 9. Git and Development History

Git is part of the development workflow, not merely a final delivery mechanism.

Use Git and GitHub CLI wherever they provide a useful development advantage.

## Every session

Inspect:

```bash
git status --short --branch
git log -5 --oneline --decorate
git diff
git remote -v
```

Do not overwrite, discard, or silently incorporate unrelated pre-existing user changes.

## Commit discipline

Commit frequently at meaningful, verified checkpoints.

Use descriptive commit messages explaining the actual change.

Keep commits focused where practical.

Update the appropriate documentation in the same commit as the behavior it describes.

Git history should preserve enough context for another agent to resume development without reconstructing everything from scratch.

Use branches and remote CI when they materially improve the workflow.

Push verified work to the intended working branch when repository conventions and branch context make this appropriate.

Never force-push, delete remote branches, rewrite shared history, or merge unrelated work without explicit authorization.

Do not manufacture commits for insignificant formatting changes.

## Source preservation

Never permanently delete existing source files as a shortcut.

When replacing source:
- Move the old version into `.backup_graveyard/`.
- Use a date-time suffix.
- Preserve the original filename.
- Ensure the directory is gitignored.

Generated build directories are exempt.

`build*/` directories should be discarded normally, not parked in the graveyard. They consume storage needed for future builds.

The user copies binaries they wish to retain into `~/Documents` manually.

Never back up multi-gigabyte generated build trees.

---

# 10. Changelog and Documentation

## CHANGELOG.md

Maintain `CHANGELOG.md` continuously.

Add meaningful user-visible functionality, fixes, compatibility changes, and other relevant modifications under `[Unreleased]` as they are completed.

Do not postpone all changelog work until release time.

Keep entries concise, understandable, and focused on actual behavior.

Do not list unfinished tasks as completed features.

If the changelog becomes unmanageable, archive older released material into an appropriate historical changelog document. Preserve the unreleased section and existing release automation expectations.

## Documentation

Documentation is part of the implementation.

When changing a feature, update the document that owns its architecture, behavior, configuration, or API contract.

Prefer updating an existing authoritative document over creating redundant copies.

Maintain:
- `docs/README.md` as the documentation index.
- Architecture documentation for meaningful design decisions.
- API documentation for observed remote behavior.
- Dependency documentation for dependency changes.
- Build instructions for changed build procedures.
- Release documentation when release procedures change.

Use Markdown consistently.

Do not document speculative behavior as established fact.

---

# 11. Suno Remote Integration and Browser Reconnaissance

This is an unofficial desktop client interacting with `suno.com`.

The application is not limited to publicly documented API functionality.

Use `browser-mcp` and the user's authenticated browser session when live inspection of Suno's UI, network requests, HTTP JSON responses, or remote behavior is necessary.

Available browsers include Brave, Chrome, Chrome for Testing, and Camoufox.

The user has authorized authenticated account inspection and appropriate remote operations using their stored session.

## Authorized live investigation

The authorized scope includes:

- Reading authenticated endpoints.
- Inspecting HTTP JSON requests and responses.
- Examining application behavior.
- Testing undocumented remote functionality.
- Probing mutation routes when necessary.
- Writing to the remote service when required by a task.
- Using credit-bearing song generations to establish undocumented behavior.

The user has a Suno Premier subscription with 10,000 credits per month. Generation is permitted for legitimate API research and testing. No additional credit approval is required.

Prefer one representative operation per unknown behavior, then record its result.

## Observed evidence

`src/suno/CapturedHosts.cpp` maintains the trusted host allowlist.

A host may be added only after direct observation of a request made by this client or an authenticated investigation performed using the account owner's session.

Third-party documentation can establish a lead, never an allowlist entry.

A successful live response from an authorized probe can establish evidence for implementation without requiring another capture pass.

Use `docs/suno_api/` as the API investigation record.

When documentation and implementation disagree, current source code is authoritative.

Record:
- HTTP status.
- Endpoint path.
- Request/response structure.
- Top-level JSON keys.
- Relevant field types and counts.
- Pagination behavior.
- Observed error behavior.
- Whether an operation is read-only or mutating.

Keep evidence sanitized.

Never put raw response bodies, cookies, bearer tokens, session headers, account identifiers, clip identifiers, complete media URLs, or private user text into repository documentation.

Prefer reproducible sanitized examples.

Historical capture procedure:

[`docs/suno_api/capture/README.md`](docs/suno_api/capture/README.md)

Recent investigation material may exist in:

`~/Documents/suno-recon-active-investigation/`

Use it when relevant rather than repeating previously completed research.

If a schema is unknown, prefer an authenticated browser investigation over speculative implementation or unnecessary proxy setup.

## Account safety

The user's authorization to investigate and write does not authorize every possible account operation.

Require explicit owner approval for:
- Account deletion.
- Irreversible library or playlist destruction.
- Destructive bulk operations.
- Other unusually destructive or irreversible account changes.

Fail closed where an incorrect operation could modify the wrong object, overwrite the wrong file, or cause silent partial writes.

A permission to gather evidence is not permission to neglect correctness.

## Credential protection

Use `CredentialStore` for credentials.

Never commit or persist:
- Cookies.
- Bearer tokens.
- Session identifiers.
- Passwords.
- Raw authenticated request headers.
- Account identifiers.
- Private account data.

Never hardcode credentials in configuration, source code, test fixtures, logs, or documentation.

Do not print credentials unnecessarily, even into session transcripts.

The plaintext credential backend remains gated.

Runtime logs are durable and may be attached to issues. Keep them free of secrets.

For URL logging, use `url.path()`, not `url.toString()`.

Never send authenticated requests or automatic remote image requests to hosts outside the allowlist.

---

# 12. Dependencies and Development Environment

## Dependency preference

Use the following preference order:

1. **CPM/FetchContent:** Prefer CMake-managed source dependencies when they are appropriate for the project and can be pinned reproducibly.
2. **MacPorts:** First choice for host-installed packages and development tools. Prefix: `/opt/local`.
3. **Homebrew:** Second choice for host-installed packages unavailable or unsuitable through MacPorts.
4. **Upstream Git source:** Build directly from Git when the preceding options are unsuitable.

Do not use vcpkg as a default dependency-management solution.

Prefer reproducible versions and pinned tags or commits.

Do not pull arbitrary dependencies merely because they might simplify a small implementation.

Use the existing dependency policy:

[`docs/dev/DEPENDENCIES.md`](docs/dev/DEPENDENCIES.md)

## MacPorts and sudo

Check tool availability first:

```bash
command -v cmake
command -v ninja
command -v ccache
command -v clang
command -v git
command -v port
command -v brew
```

MacPorts normally installs under `/opt/local`.

Use normal interactive sudo authentication when required.

Never hardcode, store, echo, or pass a sudo password in shell arguments, environment variables, source files, or repository documentation.

After dependency installation, verify that the intended executable and library paths are actually being used.

A build directory retains absolute paths from its original configuration. A moved or replaced CMake, compiler, or ccache executable may produce misleading errors such as `[code=127]`.

Reconfigure the affected build profile when necessary.

## Qt documentation and programming references

Use the Qt Documentation MCP for current Qt API questions instead of relying exclusively on training recall.

OpenCode should use available `clangd` integration for live C/C++ diagnostics.

Use relevant external documentation and source repositories when resolving unfamiliar dependencies or platform behavior.

---

# 13. Release Management

`./scripts/release.sh` is the only correct release initiation mechanism.

It supports:

```bash
./scripts/release.sh --dry-run
./scripts/release.sh --tag vX.Y.Z
```

It checks release prerequisites, including:
- Clean Git tree.
- HEAD matching `origin/main`.
- Nonempty `[Unreleased]` changelog.
- Passing release gate.
- Duplicate-run protection.

It dispatches `release.yml` pinned to the verified commit.

An agent may autonomously initiate a release only when:
1. `[Unreleased]` contains a release-worthy set of completed changes.
2. The release gate is green at the exact release commit.
3. The version and release type are deliberate.
4. All required release conditions are satisfied.

Use `release.sh`, never a manually dispatched `release.yml`.

Platform CI and artifact-building workflows may be run separately through GitHub Actions when appropriate.

Full procedure:

[`docs/dev/RELEASING.md`](docs/dev/RELEASING.md)

Never weaken release safeguards to make a release pass.

---

# 14. Agent Skills and Tooling

Skills are reusable sources of operational knowledge.

Each OpenCode skill must reside in:

`.opencode/skills/<name>/SKILL.md`

The directory name must match the frontmatter `name`.

Do not place loose Markdown files at the skills root and expect them to be discovered.

Avoid trailing spaces in directory names. They can break Windows checkouts.

Prefer a specialized skill over a generic skill when both address the task.

Inventory, provenance, and harness-specific paths:

[`docs/dev/SKILLS.md`](docs/dev/SKILLS.md)

## Browser MCP

Browser MCP is available for authenticated interaction with the user's browsers.

It may be used for:
- Suno interface inspection.
- Authenticated remote API reconnaissance.
- Testing actual web workflows.
- Verifying application behavior.
- Capturing sanitized evidence.

CDP commands may be enabled through `--caps cdp` in `opencode.jsonc`.

CDP provides low-level Chrome DevTools Protocol access, including network inspection, breakpoints, profiling, response mocking, and emulation.

It is disabled by default because verbose output and invasive browser operations can freeze or disrupt a session.

Enable it only when the task benefits from it.

---

# 15. Development Priorities and Working Style

This is a small, independently maintained GitHub project with approximately six stars.

Optimize for a fast, capable single-developer workflow.

Do not introduce enterprise-scale process where a straightforward implementation is sufficient.

Prefer:
- Small, meaningful commits.
- Cohesive features.
- Direct bug fixes.
- Incremental architectural modernization.
- Practical automation.
- Reusable components.
- Automated regression prevention.
- Clear and recoverable development state.

A substantial portion of the inherited codebase was written with older-generation LLMs.

Do not assume existing code is architecturally correct simply because it exists.

When touching legacy code:
- Understand its current behavior.
- Identify nearby defects and incomplete work.
- Improve the structure where it materially helps.
- Avoid unnecessary wholesale rewrites.
- Preserve functionality through verification.
- Add follow-up tasks for worthwhile improvements outside the current scope.

**Feature development takes priority over gold-plating.**

A feature should nevertheless be sufficiently integrated, documented, and verified to be useful.

---

# 16. Communication and Token Efficiency

Use economical language.

Prefer direct, short sentences and clear headings.

Brief progress updates are useful at meaningful development checkpoints. Avoid verbose narration of every command.

A faux/caveman-style concise register may be used in informal user-facing messages where appropriate.

Do not apply that style to:
- Source code.
- Technical documentation.
- Commit messages.
- Changelogs.
- Error messages.
- User-facing application copy.

Do not waste context repeatedly explaining what has already been completed.

Use `TODO.md`, Git history, and owning documentation as persistent memory.

When reporting work, emphasize:
- What changed.
- What works.
- What was verified.
- What remains blocked or unfinished.

Never claim an untested feature works.

Never claim skipped tests provide runtime evidence.

Never ask whether to continue while actionable work remains.

**The expected behavior is to finish a task, maintain the backlog, commit the result, and start the next task.**

---

# 17. Final Operating Principle

The agent is an autonomous development contributor, not a one-shot code generator.

It should continuously inspect the project, identify useful work, implement it, verify it, document it, preserve it in Git, and update its own backlog.

`AGENTS.md` defines how the work is performed.

`TODO.md` defines what remains to be done.

`CHANGELOG.md` records what has actually been accomplished.

Git preserves the development history.

**Do not wait for the user to tell you to do the next task. Find it, prioritize it, and do it.**

---

# 18. Subagent model selection

Launch subagents with `kilo/stealth/crouton-alpha` by default.

You may also select free models for bounded lanes: `opencode-go/crouton-alpha-free`, `opencode/crouton-alpha-free`, `openrouter/stealth/crouton-alpha`.

Keep paid provider usage minimal. DeepSeek is the only paid API. Antigravity or other advanced models are available only when explicitly chosen.
