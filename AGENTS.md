# AGENTS.md

Operating instructions for AI agents in this repository. Read the owning document
before changing the behaviour it governs, and keep plans, architecture decisions and
task state in those documents rather than here.

## What this is

ChadVis is a C++23 / Qt6 desktop frontend for [suno.com](https://suno.com) with a
bundled **projectM v4** (Milkdrop) visualizer, word-synced karaoke lyrics, FFmpeg
recording, and an offline SQLite library cache. Suno publishes no supported API:
behaviour comes from capture-derived evidence in `docs/suno_api/`, and the client
fails closed where evidence is missing. The Suno client is the product; projectM is
second.

Layout: `src/` (`audio`, `core`, `lyrics`, `qml`, `qml_bridge`, `recorder`, `suno`,
`ui`, `util`, `visualizer`), `tests/` (`unit`, `integration`, `support`), `cmake/`
(all build logic), `scripts/` (dev tooling), `docs/`, `config/default.toml`.

## Authoritative documents

| Topic | Owner |
| --- | --- |
| Work queue, priorities | `TODO.md`; leases and marks via `./scripts/task.sh help` |
| Change history | `CHANGELOG.md` (`[Unreleased]`), `docs/CHANGELOG_LEGACY.md` |
| Docs index | `docs/README.md` |
| Architecture, invariants | `docs/dev/ARCHITECTURE.md`, `docs/dev/INVARIANTS.md` |
| Testing, manual QA | `docs/dev/TESTING.md`, `docs/dev/manual-qa.md` |
| Dependencies | `docs/dev/DEPENDENCIES.md` |
| Release | `docs/dev/RELEASING.md` |
| Agent skills | `docs/dev/SKILLS.md` |
| Suno API research | `docs/suno_api/README.md`; capture kit `docs/suno_api/capture/README.md` |

## Working mode

- Keep going. After finishing a TODO item, update `TODO.md` and start the next
  worthwhile one. Do not ask whether to continue while actionable work remains.
- Stop only when the backlog is drained, an owner-only decision is required, or an
  external blocker is recorded in `TODO.md` with what was tried and the next step.
- Record newly discovered defects, missing tests and technical debt in `TODO.md`
  immediately; do not rely on conversation memory.
- `TODO.md` marks: `[ ]` todo, `[~]` claimed, `[?]` written but unverified, `[x]`
  done, `[!]` blocked, `[-]` dropped. Only `[x]` means finished, and it needs a
  `VERIFY` (or `CHANGE`) line.
- For shared work, claim before starting (`./scripts/task.sh claim/heartbeat/release`)
  and do not leave an expired claim on disk. Skip leasing for ordinary single-agent
  work.
- Ordinary engineering decisions — refactors, tests, docs, build/CI fixes, commits —
  need no permission. Ask before destructive, irreversible or security-sensitive
  actions. Never manufacture busywork.
- Prefer delivering integrated, verified features over gold-plating or perpetual
  restructuring.

## Build, test, run

```bash
./build.sh                          # default fast build -> build-fast/
./build.sh --tests --test           # build test binaries, run headless-safe suites
./build.sh --run <TestFunction>     # one QTest function, no ctest
./build.sh --release --test         # optimized build + headless-safe ctest
ctest --test-dir build/tests --output-on-failure
./build.sh --budget                 # resource budget, builds nothing
```

Profiles: `--fast` (`build-fast/`), `--debug` (`build-debug/`), `--release`
(`build/`), `--tsan` (`build-tsan/`), `--asan` (`build-asan/`), `--ubsan`
(`build-ubsan/`). Each keeps its own directory; never mix one profile's flags into
another's tree. See `./build.sh --help` for `--target`, `--clean`, `--rebuild` and
tuning options.

Run the app: `build*/chadvis-projectm-qt.app/Contents/MacOS/chadvis-projectm-qt` on
macOS (the target is a `MACOSX_BUNDLE`), `build*/chadvis-projectm-qt` elsewhere.

For current Qt API questions use the `qt-documentation` MCP configured in
`.opencode/opencode.jsonc`; `.clangd` provides live C/C++ diagnostics.

Sanitizers:

```bash
./build.sh --tsan --tests && ./build.sh --tsan --safe
./build.sh --asan --tests && ./build.sh --asan --safe
./build.sh --ubsan --tests
```

Lint and guards:

```bash
./scripts/lint-qml.sh                    # needs a configured build; uses its qmllint target
python3 scripts/check-theme-contrast.py  # no build required
clang-format -i <files>                  # root .clang-format
./scripts/check-workflows.py             # after editing .github/workflows/
./scripts/check-safe-tests.py
./scripts/check-thread-handoffs.py
./scripts/check-endpoints.py --baseline 97
./scripts/check-test-asserts.py
./scripts/check-integration-fresh.py
```

## Testing truths

- `ctest --test-dir build` discovers **zero** tests and still exits 0: the generated
  `CTestTestfile.cmake` lives in `<profile>/tests`, not `<profile>`. Use
  `ctest --test-dir build/tests` and read the test count, not just the exit code.
- `SAFE_TESTS` in `build.sh` is both the `--tests` build list and the `--safe` run
  list, and must contain every entry in `scripts/safe-tests.txt`. A new headless-safe
  test needs an `add_test()` entry and a `SAFE_TESTS` entry, or `--tests` never builds
  it and `--test` never runs it.
- `integration_tests` is deliberately outside the `--tests` list. Before trusting an
  integration result run `./scripts/check-integration-fresh.py`; rebuild it with
  `./build.sh --target integration_tests`.
- `integration_gl_tests` is the only automated proof of the GL framebuffer and
  recording path. A skip is not a pass; a green headless run proves nothing about
  OpenGL, audio, recording, or long-duration stability.
- Sanitizer findings exit 66 (TSan) or 67 (ASan); any other non-zero exit means the
  lane itself broke, not that it was clean. `detect_leaks` is unsupported on macOS.
- A build directory remembers the Qt prefix it was configured with. Use `--rebuild`
  for a clean tree; build directories are deleted, never archived.

## Code conventions

- C++23, enforced at configure time. Qt6 + Qt Quick/QML. CMake + Ninja. CPM for
  source dependencies; projectM discovery in `cmake/FindProjectM4.cmake`. SQLite for
  persistent local data.
- Expected failures return `vc::Result<T>` or `std::expected` (the tree is migrating
  from the former). Reserve exceptions for genuinely exceptional paths; never swallow
  remote API, database, file, FFmpeg, audio-device or threading errors silently.
- Log through the `Logger` wrapper (`LOG_*` macros, spdlog). Do not add ad-hoc console
  output or new `fmt` uses.
- One primary responsibility per class; roughly 200–300 implementation lines is a soft
  target, not a hard limit. Organise by architectural role, not by file type.
- QML: reusable components, presentation separate from logic, no network/database/
  audio work on the UI thread, consistent spacing and theme tokens.
- Configuration is a first-class feature: `config/default.toml` with snake_case keys
  (unknown keys are silently dropped), sensible defaults, validation, and docs. CLI
  flags override the TOML config. Credentials are the exception and never go in TOML.
- Credentials go through `CredentialStore`; the plaintext backend stays gated.
- SQLite: transactions, indexes, foreign keys, reversible or tracked migrations.
  Synchronisation must not duplicate remote data or overwrite unrelated local data.
- Inherited code is not automatically correct: understand its behaviour, fix nearby
  defects, improve structure where it materially helps, and avoid wholesale rewrites.
- Do not introduce a framework or major dependency without a concrete benefit.

## CI

Workflows: `guards.yml` (fast repository guards, no Qt), `build.yml` (Ubuntu
GCC/Clang and macOS: build, QML lint, headless gate), `windows.yml` (Release +
`windeployqt` artifact), `asan.yml` and `tsan.yml` (sanitizer lanes), `release.yml`
(only through `scripts/release.sh`), `dependabot-automerge.yml`.

- Inspect a workflow before dispatching it; only dispatch ones that support
  `workflow_dispatch`.
- Never hide a failing gate with `continue-on-error`. A dispatched run is not a
  successful run — inspect jobs, logs and artifacts.
- Repeated YAML keys can parse locally and still be rejected by GitHub; run
  `./scripts/check-workflows.py` after editing workflows.

## Suno integration and credentials

- `src/suno/CapturedHosts.cpp` is the trusted-host allowlist. A host enters only after
  direct observation from this client or an authenticated investigation; third-party
  documentation is a lead, never an allowlist entry. Never send authenticated requests
  or automatic remote image requests outside the allowlist.
- Evidence lives in `docs/suno_api/`. Record status, path, request/response shape,
  pagination and error behaviour, graded `[T1]` / `[LEAD]` / `[VERIFY]`. The source is
  authoritative when docs and implementation disagree.
- Sanitise everything: never commit or print cookies, bearer tokens, session
  identifiers, raw response bodies, account or clip identifiers, complete media URLs,
  or private user text. Log `url.path()`, not `url.toString()`.
- Prefer the capture probe in `docs/suno_api/capture/` over a browser. Authenticated
  browser inspection is permitted when a probe cannot establish the behaviour.
- Credit-bearing generation is permitted for API research; prefer one representative
  operation per unknown behaviour, then record the result.
- Require explicit owner approval for account deletion, irreversible library or
  playlist destruction, and destructive bulk operations. Fail closed whenever an
  incorrect operation could modify the wrong object or cause partial writes.

## Git and changelog

- Start a session with `git status --short --branch`, `git log -5 --oneline`,
  `git diff`. Do not discard or silently incorporate unrelated pre-existing changes.
- Commit at verified checkpoints with messages that explain the change. Update docs and
  `CHANGELOG.md` (`[Unreleased]`) in the same commit as the behaviour.
- Never force-push, delete remote branches, rewrite shared history, or merge unrelated
  work without explicit authorization.
- Never permanently delete source as a shortcut: move it to `.backup_graveyard/` with a
  timestamp suffix (gitignored). Never park `build*/` trees; delete them.
- Do not commit build output, caches, or anything containing a credential.

## Release

`./scripts/release.sh` is the only release path; never dispatch `release.yml` by hand.
With no flags it creates a **pre-release** tagged `v<version.txt>-build.<sha>`. A real
release is `./scripts/release.sh --tag vX.Y.Z`; `--dry-run` checks without
dispatching, `--verify` re-checks the last release's assets. It requires a clean tree,
HEAD on `origin/main`, a non-empty `[Unreleased]`, and a green gate at the release
commit. Never weaken release safeguards to make a release pass.

## Agent skills

`.opencode/skills/<name>/SKILL.md` is canonical and the directory name must match the
frontmatter `name`. `.claude/skills` and `.workbuddy-ai/skills` are symlinks to it, so
edit only the canonical copy; Kilo reads `.kilo/rules/*.md`. No loose Markdown at the
skills root, no trailing spaces in directory names. Inventory: `docs/dev/SKILLS.md`.

## Communication

Be concise. Report what changed, what works, what was verified, and what is blocked.
Never claim an untested feature works, and never present skipped tests as evidence.
Do not narrate every command.
