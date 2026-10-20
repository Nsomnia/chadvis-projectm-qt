---
name: chadvis-verify
description: Build, test, and verify changes in the ChadVis repo the way this tree actually works. Use before claiming any task complete, before merging recorder/session work, or when a build/test result looks green but suspicious. Encodes the profile-per-directory rule, the ctest zero-tests trap, the SAFE_TESTS drift check, the sanitizer lanes, the "green is not GL evidence" rule, and the workflow-validation check that catches duplicate YAML keys GitHub refuses to load.
---

# ChadVis verification

This repo's build has three traps that make a naive `ctest` lie. Run the checks below in order.

## 1. Build the profile you mean

Every profile has its **own** build directory. Never reuse one profile's flags in another's.

```bash
./build.sh --fast --tests          # edit loop; app usually unnecessary
./build.sh --fast --tests --safe   # build + run the headless-safe entries
./build.sh --budget                # what this box can afford; builds nothing
```

Profiles: `--fast` `--debug` `--release` `--tsan` `--asan` `--ubsan`.
`--jobs` counts **physical** cores; ctest is budgeted separately. Everything is `nice 10`
because the box is shared. If a build dir was configured with a tool path that has since
moved, `build.sh` reconfigures it — do not "fix" the `FAILED: [code=127]` by hand.

## 2. The ctest zero-tests trap

`ctest --test-dir build` discovers **zero** tests and still exits 0, because
`enable_testing()` lives in `tests/CMakeLists.txt`, not the root.

```bash
ctest --test-dir build-fast/tests --output-on-failure -j"$(sysctl -n hw.ncpu)"
```

Always name the profile's `tests/` directory, and **read the test count**, not the exit code.

## 3. SAFE_TESTS drift (the T0040 class)

`build.sh`'s `SAFE_TESTS` array is an allow-list. A registered `add_test` that is missing
from it is built but never run under `--safe`, and a missing CI regex entry is never run
there either. After adding or removing any test, run:

```bash
comm -3 \
  <(ctest --test-dir build-fast/tests -N | sed -n 's/.*: //p' | grep -v '^Total') \
  <(printf '%s\n' unit_tests "${SAFE_TESTS[@]}")
```

It must print nothing. Then confirm the same name appears in the discovery list and the
`-R` regex of **every** workflow that gates (`build.yml`, `windows.yml`, `release.yml`,
`tsan.yml`).

## 4. Sanitizers before merging recorder or session work

```bash
./build.sh --tsan --tests && ./build.sh --tsan --safe
./build.sh --asan --tests && ./build.sh --asan --safe
```

A sanitizer lane that produced no log is **not** clean — it is a lane that never ran.
Read the verdict line, not the exit code.

## 5. A green run is not evidence for GL, burn-in, or audio

`integration_tests`, `integration_gl_tests`, and `test_OffscreenRenderSpike` need a
drawable (and `integration_tests` a real audio device). They skip headless, and ctest
counts a skip as success. Only a real runtime observation counts there.

## 6. A workflow file that parses is not a workflow GitHub accepts

`yaml.safe_load` silently keeps the **last** of two duplicate keys in a mapping. GitHub
rejects the file outright, and the run it produces has **zero jobs** with the summary
"This run likely failed because of a workflow file issue" — which reads like an
infrastructure hiccup and is not one. `gh run rerun` refuses such a run, which is the tell.

```bash
./scripts/check-workflows.py                       # duplicate keys, tabs, missing jobs:
gh workflow run build.yml --ref main                # GitHub's own parser, free, with a line number
```

`gh workflow run` is the fastest honest validator: a `HTTP 422 ... failed to parse workflow:
(Line: 153, Col: 11)` names the defect and the line. It also starts a real run, so only use
it when a run is wanted. This exact class took `build.yml`, `tsan.yml` and `asan.yml` dark
for three commits (T0308) after a second `modules:` key was appended to a Qt step that
already had one.

## 7. Record the proof

Write the exact command and its result as a `VERIFY` line in `STATUS/<id>.log`, then flip
`[?]` → `[x]` in `TODO.md`. `./scripts/task.sh audit` fails the repo if an `[x]` has no
`VERIFY` line. Update `CHANGELOG.md` for user-visible behaviour.

## 8. Branch policy

`main` and `dev/bleeding-edge` are kept **identical**; `recover/development` was retired on
2026-10-20 once it matched `main` exactly. Work on `main`; when syncing, bring work *into*
main and then fast-forward the other — do not let them drift. Backup tags
(`backup/pre-sync-*`) are pushed before any force-update.
