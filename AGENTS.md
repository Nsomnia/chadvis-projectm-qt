# AGENTS.md

Operating rules for this repository. **Rules, not work items** — the ranked backlog is
[`TODO.md`](TODO.md); the claim mechanism is [`scripts/task.sh`](scripts/task.sh). If a
rule and a task disagree, the task is wrong and should be fixed.

## Build & test

Profiles: `--fast` `--debug` `--release` `--tsan` `--asan` `--ubsan`.
Actions: `--tests` (build test targets only) `--test` `--safe` (headless-safe entries)
`--run <ctest-regex>` `--rebuild` `--clean`. Also `-j/--jobs N`, `--no-ccache`, `--qt PATH`.

```bash
./build.sh --fast --tests                  # build tests; the app is usually unnecessary
ctest --test-dir build-fast/tests --output-on-failure -j"$(sysctl -n hw.ncpu)"
./build.sh --tsan --tests && ./build.sh --tsan --safe    # before merging recorder/session work
./build.sh --asan --tests && ./build.sh --asan --safe
./scripts/task.sh audit                    # marks, ids, deps, VERIFY evidence, size cap
```

* Every profile gets **its own build directory** (`build/`, `build-fast/`, `build-tsan/`).
  Never point one profile's flags at another's directory.
* `--test-dir build` finds zero tests and still exits 0. Always name the profile directory.
* The binary is a `MACOSX_BUNDLE`: `build/chadvis-projectm-qt.app/Contents/MacOS/chadvis-projectm-qt`
  on macOS, `build/chadvis-projectm-qt` elsewhere.
* **A green run is not evidence for GL, burn-in, or audio.** Those suites skip on a headless
  runner and ctest counts a skip as success. Only a real runtime observation counts there.
* Verify the artifact's mtime before quoting a number from it — this tree has produced a
  measurement from a stale pre-bundle binary.

## Work coordination

`TODO.md` is the ranked backlog. `STATUS/` holds volatile claim state. Read both before
starting anything.

1. Pick the highest-priority `[ ]` task whose `(after: …)` deps are all `[x]` and whose
   `(<files>)` list intersects no live claim. `./scripts/task.sh next` does this for you.
2. **Claim before you work:**
   ```sh
   ./scripts/task.sh claim T0042 "one-line plan"
   ```
   The claim is `open(O_CREAT|O_EXCL)` — atomic. Exit code 3 means someone holds it; take the
   next task instead of forcing.
3. **Heartbeat about every 30 minutes:** `./scripts/task.sh heartbeat T0042`. Leases last 4 h
   and expire by comparison at read time, so a crashed session never blocks the backlog.
4. `./scripts/task.sh peers` — who is on what, and when it expires.
5. `./scripts/task.sh release T0042` when done. Claims are logged, so releasing is the trail.

**Ownership.** You may edit only your own task's mark and your own `.claim`/`.log`. Never
delete or rewrite another agent's `.claim`; if it is stale, claim it and the script logs a
`STALE-STOLEN` line for you. Never reformat `TODO.md` to "tidy" it. `TODO.md` holds only
terminal states (`[x]`, `[-]`); `STATUS/` holds only in-flight state. Never write findings
into `TODO.md` prose — put them in `STATUS/<id>.log` so the board stays diffable.

**Same-file conflicts are the failure mode to fear.** The `(<files>)` list exists so you can
check statically. If two claims would touch the same file, pick a different task, or serialize
behind the live claim. `git worktree add` per task when a genuine overlap is unavoidable.

## Task state

`[ ]` todo · `[~]` claimed · `[?]` code written, **not verified** · `[x]` done **and verified**
· `[!]` blocked (reason inline) · `[-]` dropped.

**Only `[x]` means finished.** Code that compiles is `[?]`. Flip to `[x]` only after the
verification command passed *and* its output is recorded as a `VERIFY` line in
`STATUS/<id>.log`; `task.sh audit` fails on an `[x]` without one. Never mark `[x]` to make the
board look tidy. Use `[!]` for genuinely blocked, not merely parked — that distinction is what
lets another agent know whether a task is available.

Ranking is **positional** within `## P0`–`## P3`. Append; never renumber. Keep `TODO.md` under
15 KB: move finished work to `CHANGELOG.md`, delete the line, and let `git log -p TODO.md` be
the archive.

Commit with trailers so the next session can see what a session produced:

```
Refs: T0042
Agent-Signature: <model> (2026-10-07)
Verified-By: ctest --test-dir build-fast/tests -R test_DownloadQueue
Baseline-Revision: 3d0e77f
Final-Revision: 9f2c1ab
```

`git log <baseline>..<final>` must list exactly your commits. Equal SHAs mean you changed
nothing, which is a valid outcome only if you said so.

## Evidence discipline (Suno client)

This is an **unofficial** client for suno.com. Suno publishes no supported API, so the client
works from capture-derived evidence and **fails closed rather than guessing**.

* `docs/suno_api/ENDPOINT-INVENTORY.md` is the sole API-spec master.
  `docs/suno_api/OBSERVED-LEADS.md` is non-contractual — research only, never wire from it.
  `docs/suno_api/OAUTH_REDIRECT_ANALYSIS.md` owns the native-callback gate and nothing else
  restates it.
* `src/suno/SunoEndpoints.hpp` is an implementation mirror, **not evidence**.
* `[T1]` means directly captured. It does **not** mean shipped — the inventory's `Implemented`
  column (`wired` / `declared-unused` / `not-in-code`) is an independent axis.
* **Never promote `[LEAD]` to `[T1]`** without a direct human capture.
* **Fail closed on unverified hosts.** Never send a cookie, bearer, or automatic remote image
  request to a host absent from the captured allowlist. New host leads need a capture first;
  record them as deliberately excluded instead of wiring them.
* The OAuth sign-in lane is **deliberately gated** and unreachable in production. Do not
  "fix" it. A 2xx on a mutation route means "accepted, shape unverified" — parse nothing.
* **If a doc and `src/` disagree, `src/` wins and the doc is the bug worth fixing.**

## Secrets

* Never commit raw captures, credentials, cookies, tokens, account or clip identifiers, pasted
  private text, or complete media URLs. Raw captures stay outside the repository.
* User secrets go to the OS keychain through `CredentialStore` — macOS, Windows and Linux
  Secret Service. The plaintext file backend is a last resort that must stay loudly gated.
* **Never put a bearer, cookie, `Authorization` header, or a full URL with a query string into
  a log, a refusal message, or an error string.** Log `url.path()`, not `url.toString()`.
* If a TOML config ever holds a secret, that needs formed evidence — not a default.

## House rules

* **Never `rm`.** Append a date-time suffix and move it into `.backup_graveyard/` (gitignored).
* **Git history is the primary memory** for future sessions. Commit frequently; use real
  messages for session completions. Anything unusually destructive gets user approval first.
* **One fact, one owning document.** Two files claiming the same thing is a bug. Pointers to
  documents are links, never backticked text. The docs hub is `docs/README.md`.
* **Update docs in the same commit as the code they describe.**
* **C++23 minimum**, enforced at configure time. Prefer `std::println`; prefer
  `std::expected` with `.and_then()`/`.or_else()`, or `vc::Result<T>` for expected failures.
  `catch` is for genuinely exceptional paths.
* ~500 LOC is a soft ceiling for a class. Choose the paradigm that fits rather than defaulting.
* **Add tests where they let logic be verified** or prevent future breakage — especially for
  anything touching threads, raw memory, or FFmpeg return codes. A seam that cannot inject a
  libav error is why a whole class of bug recurs silently.
* **Sanitizers before merging** work that touches the recorder or session threads.
* Keep the "Chad / Arch, BTW" register in prose — Linus orchestrating. It is the project's
  voice, never a substitute for fact. Don't be tacky.

## Self-driven loop

Keep working while tasks remain or improvements are evident. Prefer parallel bounded lanes and
decisive commits over gold-plating. Record new tasks in `TODO.md` with evidence, not here.
End every session by stating what was built, tested, verified and committed — or the exact step
that is blocked and cannot be amended.