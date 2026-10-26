# Releasing ChadVis

One command ships executables:

```bash
./scripts/release.sh            # pre-release of the pushed HEAD, waits, verifies
./scripts/release.sh --dry-run  # run every check, dispatch nothing
./scripts/release.sh --tag v1.2.0   # a real release, marked "Latest"
```

The script is the procedure. This document explains what it enforces and why,
so the next person can change it deliberately rather than work around it.

## Dependabot does not build executables

Worth stating because it looks like it should: Dependabot only opens pull
requests to bump dependencies. Even here, where it watches exactly one
ecosystem (`github-actions`), it has no relationship to compilation, packaging,
or publishing. The thing that produces binaries is GitHub Actions, driven by
`.github/workflows/release.yml`, and the agent-facing trigger is
`scripts/release.sh`.

## The shape of a release

```
release.sh
  ├── refuses a dirty tree or a HEAD that is not origin/main
  ├── refuses an empty CHANGELOG [Unreleased] section
  ├── waits for the `build` and `windows` gate runs at that exact SHA
  ├── dispatches release.yml with tag + ref (the commit is pinned)
  ├── waits for the run
  └── verifies all three assets are attached
```

`release.yml` then builds **Release** on Ubuntu, macOS and Windows, runs the 11
headless ctest entries on each, packages (`cpack` TGZ/`dmg`, `windeployqt` zip
for Windows), and creates the GitHub release **at the commit it built**. The
script never creates a tag locally. That is the point: a tag made on a laptop
and a binary built in CI can disagree, and there is no field in a release page
where that disagreement is visible.

`GITHUB_TOKEN` cannot trigger a workflow from a tag it creates, so the dispatch
path does the build and the publish itself. A tag push (`on: push: tags: v*`)
still works and is what an ordinary maintainer uses.

## When an agent may start a release

The default is a human decision. An agent may dispatch one itself when **all**
of the following hold — and it still goes through `./scripts/release.sh`, never
a hand-written `gh workflow run release.yml`, because the script is what applies
the refusals below:

1. **The CHANGELOG has a release worth of `[Unreleased]` entries.** A backlog of
   finished, user-visible changes — not one half-done item and not an empty
   section. The script refuses an empty section for exactly this reason.
2. **The gate is green at that exact commit.** `build` and `windows` passed on
   the pushed `HEAD`, and `guards` passed. "It compiled on my machine" is not
   this; a local build has no gate record.
3. **The tag kind is deliberate.** `vX.Y.Z` only for a real release that is
   allowed to be "Latest"; anything that might still move is
   `vX.Y.Z-build.<short-sha>`.

Run `./scripts/release.sh --dry-run` first, then the real command, and report
the run URL. If either path refuses — dirty tree, wrong HEAD, empty section, red
gate, second run — stop and say which refusal fired. The refusal is the answer,
not an obstacle to route around.

## Tags decide what kind of release it is

| Tag | Kind | GitHub "Latest" |
| :--- | :--- | :--- |
| `v1.2.0` | release | yes |
| `v1.2.0-build.1a2b3c4` | pre-release | no |

The hyphen after the version is the only signal, and both the script and the
workflow derive the flag from it. `version.txt` is the base version and must
agree with the tag before the hyphen; the workflow refuses a mismatch on both
paths.

Re-dispatching the same tag is safe and is the recovery path for a flaky
runner: the assets are uploaded with `--clobber` and the notes are rewritten
from what actually arrived.

## What a release is allowed to lack

- **Windows**: a *pre-release* may ship without the Windows zip, and the notes
  say so. A real release (`v1.2.0`) refuses to publish without it. Windows is a
  reusable workflow called by `release.yml`, so the zip comes from the same
  run, not from a second build that could differ.
- **macOS**: never. `MACOSX_BUNDLE` is set and the bundle check stops the
  packaging leg if it ever stops being set. A release with no `.dmg` is a
  Linux-only release pretending not to be.

## The macOS artifact is ad-hoc signed, never notarised

There is no Developer ID certificate in this project. `TargetSetup.cmake` runs
`codesign --force --deep --sign "-"`, which binds the bytes to the bundle and
identifies no developer. Gatekeeper blocks the app on any machine that did not
build it; the release notes say so at the top, in every release, because it is
the first thing a user hits. The `.tar.gz` and the Windows zip have no
equivalent problem.

## Before you change the release path

- `scripts/check-workflows.py` after any workflow edit. GitHub rejects a
  mapping with a repeated key and refuses the whole file; a YAML library keeps
  the last one and reports nothing. The symptom is a run with **zero jobs** and
  "This run likely failed because of a workflow file issue", which reads like
  an infrastructure hiccup.
- `./scripts/release.sh --dry-run` after any change to the script. It exercises
  every refusal and prints the tag it would use.
- A release that publishes is not evidence that the app runs. The runtime
  entries (`integration_tests`, `integration_gl_tests`,
  `test_OffscreenRenderSpike`) need a drawable or an audio device and skip
  headless; ctest counts a skip as success. See
  [TESTING.md](TESTING.md) and the MT-001 checklist.

## Manual fallback

If Actions is unavailable, the same three artifacts can be produced locally:

```bash
./build.sh --release
cmake --build build --target package   # --release uses build/
```

This is a fallback, not the procedure: a locally built artifact has no gate
record and no pinned commit, so it should be attached to a release only with a
note saying so.
