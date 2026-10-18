# Portable CI + pre-commit drop-in

This directory ships a copy-in CI workflow and a git pre-commit hook so a
consumer repo that vendors `c-cpp-profi` gets the skill's evidence gates running
in CI and at commit time. Both files are templates: copy them, set one path, and
adjust if you vendor the skill somewhere other than `.opencode/skills/c-cpp-profi/`.

## Files

- `github-actions-c-cpp-profi.yml` — a GitHub Actions workflow template.
- `pre-commit-c-cpp-profi.sh` — a POSIX-bash git pre-commit hook.

## How to vendor the skill

Put the skill at `.opencode/skills/c-cpp-profi/` in your repo. Any of these work:

- Git submodule: `git submodule add <skill-repo-url> .opencode/skills/c-cpp-profi`
- Git subtree, or
- A plain copy of the `c-cpp-profi/` directory.

All paths in both templates assume that location. If you vendor it elsewhere,
edit the workflow paths and set `CPP_PROFI_DIR` for the hook (see below).

## How to install the CI workflow

1. Copy `github-actions-c-cpp-profi.yml` to `.github/workflows/c-cpp-profi.yml`
   in your repo.
2. Edit the `GATE_REPORT` env var at the top to point at your committed,
   filled-in gate report (default `docs/gate-report.md`). Generate a skeleton
   with `bash .opencode/skills/c-cpp-profi/scripts/cpp_gate_report.sh . > docs/gate-report.md`,
   fill in every gate row plus the `## Change Scope` answers, and commit it.
3. Commit and push. The workflow runs on `push` and `pull_request`.

The runner installs `ripgrep`, which the `--self-test` steps need.

## How to install the pre-commit hook

From the repo root, with the skill vendored at `.opencode/skills/c-cpp-profi/`:

```sh
ln -s ../../.opencode/skills/c-cpp-profi/assets/ci/pre-commit-c-cpp-profi.sh .git/hooks/pre-commit
```

Or copy it and make it executable:

```sh
cp .opencode/skills/c-cpp-profi/assets/ci/pre-commit-c-cpp-profi.sh .git/hooks/pre-commit
chmod +x .git/hooks/pre-commit
```

If the skill lives elsewhere, set `CPP_PROFI_DIR` to its path, e.g.
`CPP_PROFI_DIR=vendor/c-cpp-profi`.

## What each gate enforces

CI workflow:

- **Skill contract** (`validate_skill_contract.py`): the vendored skill is
  structurally intact — required references, examples, assets, and clean script
  syntax.
- **Helper self-tests** (`cpp_backlog.sh`, `cpp_comprehension_map.sh`,
  `cpp_domain_detect.sh`, `cpp_docs_check.py`, each `--self-test`): every helper
  proves its own behavior on a throwaway fixture, so a broken helper fails CI
  rather than passing silently.
- **Evidence gate** (`cpp_evidence_check.py … --derive-profiles`): the committed
  gate report is validated. `--derive-profiles` reads the enforced risk profiles
  from the report's own `## Change Scope` yes/no answers (parser-touched derives
  `parser`+`security`, ABI-touched `public-abi`, threads-touched `concurrency`, a
  performance claim adds `performance` plus the performance-proof requirement),
  so the gate set cannot be under-claimed on the command line.

Pre-commit hook:

- **Risk scan** (`cpp_risk_scan.sh`) over the staged `*.c/*.cc/*.cpp/*.cxx/*.h/*.hpp`
  files: prints unsafe-API, raw-allocation, cast, memory-move, process-exec,
  assert-only, and threading call sites for the changed paths, then reminds you
  to fill and `--derive-profiles`-validate a gate report before handoff.

  The hook is advisory: scan findings are triage prompts, not commit blockers, so
  a clean scan and a scan with findings both exit 0. It exits non-zero only on a
  hard error — the skill directory or risk-scan script is missing, or `rg` is not
  installed.
