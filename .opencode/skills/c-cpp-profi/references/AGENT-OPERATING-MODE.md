# Agent Operating Mode

## Audience

`c-cpp-profi` is for Hermes, Codex, Claude, and any other coding agent that must work in C/C++ without hiding risk behind confident prose.

## Shared Protocol

1. Read local instructions and issue state.
2. Reserve files or otherwise coordinate before editing in shared workspaces.
3. Inventory build and test surfaces.
4. State the intended gate set before substantial edits.
5. Keep the patch small and reviewable.
6. Run gates and report exact commands.
7. Generate or fill a gate report for non-trivial changes.
8. File follow-up issues for unhandled risks.

## Prompt Kernel For Subagents

Use this shape when dispatching a specialized agent:

```text
Use $c-cpp-profi on <repo>.
Task: <specific task>.
Scope: <files/directories>.
Do not edit outside scope.
Before edits, identify build system, standard, ownership/lifetime risks, ABI impact, and gates.
After edits, report exact commands run, findings, and residual risks.
```

## Review Output

Lead with defects:

```text
Findings:
- Severity:
- File:line:
- Evidence:
- Why it matters:
- Fix:

Gates:
- Run:
- Missing:

ABI/API:
- Impact:

Artifacts:
- Golden/pixel/perf evidence:

Residual risk:
```

## Coordination Rules

- One agent owns a file at a time.
- Separate agents by non-overlapping modules, test surfaces, or analysis mode.
- Do API/type renames in a single coordinated wave before downstream implementation work.
- Do not clean build artifacts in shared repos without coordination.
- Keep generated evidence in the repo's accepted workspace, not in random temporary paths, when the evidence is part of the deliverable.

## Honesty Rules

- "No sanitizer failures in this run" is not "memory safe."
- "No findings from static analysis" is not "secure."
- "Benchmark improved on one machine" is not "faster everywhere."
- "Compiles on Linux" is not "portable."
- State the actual evidence and its limits.
