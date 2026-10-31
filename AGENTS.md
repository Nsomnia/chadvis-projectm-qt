# AGENTS.md

Operating rules for this repository. This file is loaded into **every** session, so it carries
rules and pointers, never the detail — the doc named on each line owns that detail. Backlog:
[`TODO.md`](TODO.md). Docs hub: [`docs/README.md`](docs/README.md).

Prioritize features over tests so long as at least some basic minimal tests are done at a minimum where they fit. Do not write tests just for the sake of writing tests. Do write tests to keep hard to debug or hard logic together. Use your best instinct. If any of the AGENTS.md, TODO.md, scripts/tasks.sh, or any other harness/repo/task logic seems wrong or sub-optimal: inform the user with your better replacement and wait for their respond to approve such as change.

You can keep working for as long as you wish. The TODO.md is very granular with lot's of tests and about half the codebase was written with ancient LLM models.

## Build & test

```bash
./build.sh --fast --tests                  # build tests; the app is usually unnecessary
./build.sh --fast --tests --safe           # build + run the headless-safe entries
./build.sh --budget                        # what this box can afford; builds nothing
./build.sh --tsan --tests && ./build.sh --tsan --safe    # before merging recorder/session work
./build.sh --asan --tests && ./build.sh --asan --safe
```

* Profiles `--fast` `--debug` `--release` `--tsan` `--asan` `--ubsan` each keep their own build
  directory (`build-fast/`, `build-tsan/`, …). Never point one profile's flags at another's.
  Tuning flags and environment overrides: `./build.sh --help`.
* `ctest --test-dir build` finds zero tests and still exits 0. Name the profile directory and
  read the test count, not the exit code.
* The binary is a `MACOSX_BUNDLE`: `build*/chadvis-projectm-qt.app/Contents/MacOS/chadvis-projectm-qt`
  on macOS, `build*/chadvis-projectm-qt` elsewhere.
* **A green run is not evidence for GL, burn-in, or audio.** Those entries skip headless and
  ctest counts a skip as success; only a real runtime observation counts there.
* `build.sh` spends 75% of the box on purpose (`--jobs` = 75% of logical cores, `nice 10`). Run
  `./build.sh --budget` before calling a build misbehaving; every run rewrites `build.log`.

## CI reality

* Gates: `guards.yml` (runs `check-workflows.py` in seconds), `build.yml` (Ubuntu gcc + clang,
  macOS) and `windows.yml` (Release, the same headless entries, a `windeployqt` artifact). The
  sanitizer lanes report and count; they do not gate, and a lane that produced no log is broken,
  not clean. A failing gate gets fixed or reverted, never re-armed with `continue-on-error`.
* `integration_tests`, `integration_gl_tests`, `test_OffscreenRenderSpike` skip headless — see
  the green-is-not-evidence rule above.
* **A workflow file that parses is not one GitHub accepts.** A repeated YAML key is rejected
  outright while `yaml.safe_load` reports nothing; the symptom is a run with zero jobs, not a
  test failure. Run `./scripts/check-workflows.py` after touching `.github/workflows/`.

## Shipping

`./scripts/release.sh` (`--dry-run`, `--tag vX.Y.Z`) is the only correct way — it refuses a dirty
tree, a HEAD that is not `origin/main`, an empty `[Unreleased]` section, a red gate, or a second
run, then dispatches `release.yml` pinned to that commit. An agent may start one itself only
when `[Unreleased]` holds a release's worth of finished changes, the gate is green at that exact
commit, and the tag kind is deliberate — and it still goes through `release.sh`, never a manual
workflow dispatch. Full rule: [`docs/dev/RELEASING.md`](docs/dev/RELEASING.md#when-an-agent-may-start-a-release).

## Do not

* **Never commit credentials.** No raw captures, cookies, tokens, account or clip identifiers,
  private text, or complete media URLs in anything that leaves this machine — commit, push,
  `docs/`, `TODO.md`, `STATUS/`, commit message, release asset, issue. User secrets go through
  `CredentialStore`; the plaintext file backend stays gated.
* **A secret may be printed in the session transcript** — it is local and ephemeral. That
  permission stops at the repository and at any remote. The app's runtime log is durable and gets
  attached to issues, so it stays free of credentials: log `url.path()`, not `url.toString()`.
* **Never send a cookie, bearer, or automatic remote image request off the allowlist**
  (`src/suno/CapturedHosts.cpp`). This is an unofficial suno.com client: a host becomes an entry
  after **our own observation** — a request this client made, or an authenticated probe an agent
  ran with the account owner's session — never on a third-party document. A lead is never a host
  entry. `docs/suno_api/` is the record — if a doc and `src/` disagree, `src/` wins.

### Authorized live work (owner grant, 2026-10-30)

The account owner has authorized this agent to **use their stored session directly** — reads and
writes, against `suno.com` and its captured hosts — instead of waiting for a separate capture
pass. The grant is deliberate and covers:

* **Live probing of any authenticated route**, including mutations, with the credentials already
  in `CredentialStore`; results may be recorded as evidence at `[T1]` on the same footing as a
  Burp/HAR capture, because they are our own observation of the live service.
* **Wiring a route from that evidence without a second capture.** A successful live response from
  this client is the proof; a `[LEAD]` third-party document is still just a lead.
* **Spending credits.** Generation and other credit-bearing routes are authorized. Reference
  scale: ~10 credits per song against ~7 500 available. Do not run a spend loop to "explore";
  probe one call, record what it answered, then decide.

What the grant does **not** lift, and nothing in it should be read as lifting:

* **The credential rule above.** Nothing captured, no cookie, token, or account identifier is
  ever committed, quoted in `TODO.md`/`STATUS/`/`docs/`, or put in a commit message. Evidence is
  recorded as status code, top-level keys and counts — never the body, never the header.
* **Fail closed.** A wired route whose failure mode is a wrong file, a wrong object, or a silent
  partial write stays refused until it has a runtime check. The grant frees the *evidence*
  requirement, not the *correctness* requirement.
* **Anything irreversibly destructive** (account deletion, trashing a library, spending a
  metered download allowance) still gets the owner's explicit go-ahead per action.

Historical capture procedure, still valid when a route needs a reproducible artifact:
[`docs/suno_api/capture/README.md`](docs/suno_api/capture/README.md).
* **Never `rm` source.** Move it to `.backup_graveyard/` with a date-time suffix (gitignored).
  **Generated build directories are exempt:** `build*/` are discarded, never parked — the user
  copies a binary they want to keep into `~/Documents` by hand, and a parked multi-GB tree is
  disk the next build needs. No script backs up a build.
* Anything unusually destructive gets user approval first.

## Working style

* Done means the tests ran, not that it compiles. Prefer decisive commits over gold-plating.
* Commit often with a real message — git history is the memory for the next session — and update
  the owning doc in the same commit. One fact, one owning document.
* C++23, enforced at configure time. `std::expected` (`.and_then()` / `.or_else()`) or
  `vc::Result<T>` for expected failures; `catch` only for genuinely exceptional paths.
* Test what only a test can verify — threads, raw memory, FFmpeg return codes — and run the
  sanitizers before merging recorder or session work.
* `TODO.md` explains its own grammar. `./scripts/task.sh` offers claim/heartbeat/release leases
  and an audit; use them when work is genuinely shared, skip them when it is not.
* Keep the "Chad / Arch, BTW" register in prose. Never a substitute for fact.

## Environment

* Qt API questions: use the Qt Documentation MCP before trusting training recall. OpenCode
  auto-detects `clangd`, which gives live C/C++ diagnostics mid-task.
* Packages: MacPorts (`port`, `/opt/local`) first, Homebrew second, vcpkg for C/C++ neither
  provides; `sudo port install` needs sudo — ask first. Check `command -v` before trusting a
  tool: a build directory remembers the absolute paths it configured with, so a moved
  `ccache`/`cmake` yields a misleading `[code=127]` that `build.sh` repairs by reconfiguring.
  Policy: [`docs/dev/DEPENDENCIES.md`](docs/dev/DEPENDENCIES.md).
* Suno recon: recent captures live in `~/Documents/suno-recon-active-investigation/`. When a
  schema is still unknown, ask before reaching for a proxy capture.

## Agent skills

Every skill is a directory with `SKILL.md` at `.opencode/skills/<name>/SKILL.md`, and `<name>`
must equal the frontmatter `name`. A loose `.md` at the skills root is not discovered, and a
trailing space in the directory name makes `git checkout` fail on Windows — which kills the
Windows job while `continue-on-error` still reports green. Both have happened here. Prefer a Qt
skill over a generic one when both apply. Inventory, provenance and per-harness paths:
[`docs/dev/SKILLS.md`](docs/dev/SKILLS.md).

## Tools
browser-devtools mcp and Playwright mcp both via the user's Brave browser, logged in via Facebook, should be available with dev
utils as well via MCP. Suno recon lives at `~/Documents/suno-recon-active-investigation/`; live
recon may be done with the available tools.
Recon and logic (huge amounts plus tests) at `/Users/derekvanee/Documents/suno-master-utility-browser-extension/`.

## Token Efficiency
Invoke a faux/psuedo form of the popular `caveman` speak skill where possible to be as token efficient as possible.
The user is also hard-of-seeing and thus has a hard time reading so when speaking directly to the user via textual output messages shorter sentances that are more direct and split up with brief bullet points aides in the chance of your messages being seen *and* read or resonded to. You may use the ask a user multiple-choice questions tool call, along with any other tool calls, at any time as much as required. 
