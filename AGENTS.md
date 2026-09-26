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
  `SunoClient` code, ensure tokens are always captured of the unique *end-users*.

### 2. Secret handling (non-negotiable)

- Never commit raw network captures, credentials, cookies, tokens, account or
  clip identifiers, pasted private text, or complete media URLs.
- Raw captures stay **outside** the repository. The only retained raw artifact is
  the sanitized recon, governed by `docs/suno_api/raw/README.md`.
- User secrets go to the OS keychain, as appropriate on Windows, Mac OSx, and 
  GNU/Linux (well actually... gnu is hehe) alike, via `CredentialStore`, never
  to logs and if needed for some strange reason in a TOMl config file, then good
  evidence as to why must be formed.

### 3. Verification bar

A task is **not** complete from a stale binary, a declaration, a scan string, or
a historical commit. Verify the current source with a fresh configure/build
(only do a clean re-build when absolutely nessecary due to the users limited
compute power), the relevant tests, focused lint/format checks, and a real
runtime path. Record observed results, if/when deemed needed, in `CHANGELOG.md`.

For Qt/QML checking/linting tools available but not limnited to are: qmlformat,
qmllint, qmlls, qmlpreview, qmlprofiler, and qmltc. Any tooling may be agent
installed if/when needed via brew, from source, or downloading a binary.
 
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
history is the primary memory for future sessions, when exceeding context limits.

Anything deemed extremly unsafe should be confirmed with user approval despite
being a novice git user.

### 6. Documentation

One fact, one owning document. If two files claim the same thing, that is a bug.
Every pointer to a document is a **link**, not backticked text. Keep the docs
hub table of contents in `docs/README.md` and nowhere else of which the root
README.md may link to it or any other docs for most front-facing information.

### 7. Code style

- **Standard:** C++23 is the minimum, enforced at configure time.
- **I/O:** prefer `std::println`; not `std::cout`, not `printf`, not `fmt`.
- **Errors:** prefer `std::expected` with monadic `.and_then()` / `.or_else()`.
  Expected failures return `vc::Result<T>`; `catch` is for genuinely exceptional
  paths.
- **Targets:** Arch Linux (latest GCC/Clang) is the primary development target;
  macOS is the platform this is run and verified on by the user currently
  however, and Windows (10/11) is also helpful being the major userbase.
- ~500 LOC is a soft ceiling for a C++23 class. Use best judgment.
- Production-ready, maintainable, no slop. Follow industry standards.
- Use appropriate coding paradignms as they best fit: object, functional, etc.

### 8. TODO.md Task state

Marks: `[ ]` todo · `[~]` in progress · `[x]` done, awaiting verification ·
`[?]` blocked or requires human input · `[!]` needs immediate user attention.

Only the user removes tasks. Agents may freely refactor, add, and reorganize.
Record new tasks when instructed or found during operations in `TODO.md`
rather than in this file.

### 9. Working style

- Unlimited tool calls and full access to user-system packages (`gh`, `git`,
  `ddgr`, `pacman -Qq`). If a needed tool is missing, they may be installed via
  brew or from source unless major or requiring user work, then say so.
- Prefer parallel bounded lanes and decisive commits over gold-plating.
- Keep the "Chad" and "Arch, BTW" register: Linus Torvalds and Linus Tech Tips
  orchestrating while Richard Stallman dispenses GNU kung-fu in the background.
  Humor is the project's unique "easter egg" voice, but not a substitute for
  fact or information. Don't be tacky.
- Self-prioritize. Unless otherwise stated, you are given free rein. Improve
  code when your context shows room for improvment, add found tasks or ideas to
  the TODO.md, and otherwise keep development moving rapidly as-if it were a
  vertically oriented and aligned company starting out from venture seed funds.
  Write tests where they genuinely let logic be verified programmatically or
  may avoid future headaches.

### 10. Self-driven loop

You have freedom to keep working on `TODO.md` while tasks remain or improvements
are evident. Use a simple self-harness for repetition, and end with an explicit
statement of what is done, compiled, tested, verified, and committed — or the
step that is fatally blocked and cannot be amended. If operating on any model
with `free` in it's name then you are free to go above and beyond in unlimited
token allowance usage.

## Environment notes

- Lint with `cline`; `clangd` is also available, and any other tools mentioned,
  found on the users system, or installable whether from brew, sourfe, or
  otherwise.
- Some more tools installed with brew and thus available: include-what-you-use
  shellcheck pre-commit cmake-lint ccache catch2 nlohmann-json yaml-cpp eigen
  boost (newer version env var set in bash/zsh/fish rc) cli11 flatbuffers
  msgpack vcpkg glfw samply hyperfine gitui eza zoxide git-delta
