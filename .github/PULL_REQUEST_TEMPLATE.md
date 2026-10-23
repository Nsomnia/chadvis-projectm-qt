## What this changes

<!-- One paragraph. What was wrong or missing, and what it does now. -->

Closes `T####` (or "no backlog item").

## Verification

A PR is not complete because it compiled — see
[`docs/dev/CONTRIBUTING.md`](../docs/dev/CONTRIBUTING.md) and [`AGENTS.md`](../AGENTS.md). Fill this
in with what you actually ran, pasted, not paraphrased.

- **Profile and directory:** <!-- e.g. ./build.sh --fast --tests → build-fast/ -->
- **Command:** <!-- e.g. ctest --test-dir build-fast/tests --output-on-failure -->
- **Test count read:** <!-- e.g. 44 passed / 0 failed / 0 skipped — NOT just the exit code -->
- **VERIFY line:** <!-- STATUS/T####.log: the exact command and its result -->

```text
<!-- paste the summary lines here -->
```

## Not verified

<!--
Say what you did not check, and why. This is required, not optional, for anything touching:

  * GL, projectM, burn-in, or audio — those entries skip headless and ctest counts a skip as success,
    so a green run proves nothing there. Name the manual check (docs/dev/manual-qa.md) instead.
  * hardware encode paths, which most machines cannot exercise at all.
  * a workflow change: `./scripts/check-workflows.py` passing does not prove GitHub accepts the file.
-->

## Checklist

- [ ] Docs describing the change are in **this** commit, not a follow-up (`CHANGELOG.md` for
      user-visible behaviour, the owning doc for the rule, `TODO.md` mark flipped only if verified).
- [ ] New non-trivial logic has a test, or the reason it cannot be tested is stated above.
- [ ] Sanitizers were run if this touches the recorder or session threads (`--tsan`, `--asan`).
- [ ] `./scripts/check-workflows.py` passes if `.github/workflows/` changed.
- [ ] `./scripts/lint-qml.sh` reports no blocking problems if `src/qml/` changed.
- [ ] Destructive file operations went to `.backup_graveyard/` with a timestamp suffix — nothing was
      `rm`'d.
- [ ] **No credentials anywhere in this branch**: no token, cookie, bearer, `Authorization`/`Cookie`
      header value, content key, account or clip identifier, complete media URL, raw capture, or pasted
      private text — in the diff, the commit messages, the docs, or this description. A credential may be
      visible in a local transcript; it may not reach the repository or a remote. See `SECURITY.md`.
