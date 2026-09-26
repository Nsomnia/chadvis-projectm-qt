# AGENTS.md — Operating Rules for ChadVis

> **Read this first. It is short on purpose.** Every chat session in this
> repository operates on this document, so it contains *rules*, not work items.
> **All tracked work lives in [`TODO.md`](TODO.md).** The docs hub is
> [`docs/README.md`](docs/README.md). Release history is
> [`CHANGELOG.md`](CHANGELOG.md).

## Product identity

**Product pivot ADOPTED 2026-08-26.** This repo ships the **Suno.com desktop
frontend** (library, generation, downloads, playlists, account) with **projectM
as a secondary** visualizer / music-video engine (keyframe scene composition,
karaoke, batch automation, future lightweight DAW). Plan: `docs/PIVOT_PLAN.md`.

Do not import API shapes from external rewrite repositories, frontend bundle
strings, or historical topic prose. Suno facts are capture-driven and carry only
the repository's `[T1]` / `[LEAD]` / `[VERIFY]` labels.

## The rules

### 1. Evidence discipline (non-negotiable)

- `docs/suno_api/ENDPOINT-INVENTORY.md` is the **sole API-spec master**.
- `docs/suno_api/README.md` is the **navigation boundary**.
- `docs/suno_api/OBSERVED-LEADS.md` is **non-contractual** — research and capture
  planning only. Never wire a route from it.
- `docs/suno_api/OAUTH_REDIRECT_ANALYSIS.md` is the **native-callback gate**.
- `docs/suno_api/raw/README.md` is **provenance only**.
- `src/suno/SunoEndpoints.hpp` is an **implementation mirror, not evidence**.
- **Never promote `[LEAD]` to `[T1]`** without a direct human capture.
- `[T1]` means "directly captured". It does **not** mean "shipped" — the
  inventory's `Implemented` column (`wired` / `declared-unused` / `not-in-code`)
  is an independent axis.
- **Fail closed on unverified hosts.** Never send cookies, bearers, or automatic
  remote image requests to an absolute host absent from a captured allowlist.
- Never hand-roll the Clerk handshake. Never put a Google ID token into
  `SunoClient`.

### 2. Secret handling (non-negotiable)

- Never commit raw network captures, credentials, cookies, tokens, account or
  clip identifiers, pasted private text, or complete media URLs.
- Raw captures stay **outside** the repository. The only retained raw artifact is
  the sanitized recon, governed by `docs/suno_api/raw/README.md`.
- User secrets go to the OS keychain via `CredentialStore`, never to TOML or logs.
- When a file is removed, report the fact. If a secret ever reaches a commit,
  treat the account as compromised and rotate it — expiry is not remediation for
  permanent identifiers.

### 3. Verification bar

A task is **not** complete from a stale binary, a declaration, a scan string, or
a historical commit. Verify the current source with a fresh configure/build, the
relevant tests, focused lint/format checks, and a real runtime path. Record
observed results in `CHANGELOG.md`.

- Tests: `ctest --test-dir build/tests --output-on-failure`.
  **Not** `--test-dir build` — that directory has no `CTestTestfile.cmake`,
  discovers zero tests, and still exits 0.
- Binary: `build/chadvis-projectm-qt`. **Not** `build/src/...`.
- If a doc and `src/` disagree, `src/` wins and the doc is a bug worth fixing.

### 4. Never `rm`

Append a date-time string to the filename and move it into
`.backup_graveyard/`, which is gitignored. Committing the move is optional.
Prune the graveyard when it is large and holds nothing of value — git history
already preserves the source.

### 5. Git

Commit frequently so `git log --oneline` gives future agents a parseable history.
Use detailed messages for session completions and other major changes. Git
history is the primary memory for future sessions, which always exceed context
limits and lose it.

Never force-push rewritten history, delete a remote branch, or rewrite a tag
without explicit per-instance user authorization.

### 6. Documentation

One fact, one owning document. If two files claim the same thing, that is a bug.
Every pointer to a document is a **link**, not backticked text. Keep the hub
table of contents in `docs/README.md` and nowhere else.

### 7. Code style

- **Standard:** C++23 is the minimum, enforced at configure time.
- **I/O:** prefer `std::println`; not `std::cout`, not `printf`, not `fmt`.
- **Errors:** prefer `std::expected` with monadic `.and_then()` / `.or_else()`.
  Expected failures return `vc::Result<T>`; `catch` is for genuinely exceptional
  paths.
- **Targets:** Arch Linux (latest GCC/Clang) is the primary development target;
  macOS is the platform this is run and verified on.
- ~500 LOC is a soft ceiling for a C++23 class. Use judgment.
- Production-ready, maintainable, no slop. Follow industry standards.

### 8. Task state

Marks: `[ ]` todo · `[~]` in progress · `[x]` done, awaiting verification ·
`[?]` blocked on a human or a capture · `[!]` needs user attention now.

Only the user removes tasks. Agents may freely refactor, add, and reorganize.
Record new findings in `TODO.md` rather than in this file.

### 9. Working style

- Unlimited tool calls and full access to user-system packages (`gh`, `git`,
  `ddgr`, `pacman -Qq`). If a needed tool is missing, say so immediately.
- Prefer parallel bounded lanes and decisive commits over gold-plating.
- Route a task to a specific model with `opencode --models`, then
  `opencode --model provider/name run "..."`. Free models are fair game.
- Keep the "Chad" and "Arch, BTW" register: Linus Torvalds and Linus Tech Tips
  orchestrating while Richard Stallman dispenses GNU kung-fu in the background.
  Humor is the project's voice, not a substitute for information.
- Self-prioritize when given free rein. Write tests where they genuinely let
  logic be verified programmatically.

### 10. Self-driven loop

You have freedom to keep working on `TODO.md` while tasks remain or improvements
are evident. Use a simple self-harness for repetition, and end with an explicit
statement of what is done, compiled, tested, verified, and committed — or the
step that is fatally blocked and cannot be amended.

## Environment notes

- Lint with `cline`; `clangd` is also available.
- `oh-my-opencode-slim` is used on the user's system, not in cloud sessions.
- The caveman skill was removed because the current model has unlimited
  inference. Re-add it (`npx skills add JuliusBrussee/caveman`) if running on a
  quota-limited model.
- `GENERAL_LLM_STARTING_PROMPT.md` is optional background. It has drifted from
  this document and from the code; where they disagree, this file and the source
  win.
