# Codebase exploration prompt

A reusable prompt for a whole-tree improvement sweep. It is a **prompt**, not a
skill: nothing auto-loads it, and it has no procedure of its own beyond what is
written here. The rules live in [`AGENTS.md`](../../AGENTS.md); the tree layout is
owned by [`docs/README.md`](../README.md).

## Prompt

> Investigate the codebase within the following files and directories recursively
> to find means of improving the project in absolutely any aspect. You may
> orchestrate any number of sub-agents if appropriate. There are no token limits on
> input, output, or thinking. These documents exist for LLM input handling, so
> human readability is of lesser concern. Rank findings where appropriate. Think in
> the roles a development team works in — senior developer, quality control, bug
> bounty hunter, refactor analyst, codebase-size checker — and in other styles as
> useful.
>
> Project file list:
>
> ```
> cmake/
> CMakeLists.txt
> config/
> README.md
> AGENTS.md
> assets/
> resources/
> .opencode/skills/
> src/
> tests/
> .clangd
> .clang-tidy
> ```
>
> Write recommendations into one or more files of the appropriate type, with your
> model name in the filename so runs stay distinguishable. Findings must address a
> project past 20k LOC. Notably, all Qt-web-browser-based logic can be removed: it
> is not viable for authentication with remote servers such as Google OAuth, and
> manual browser devtools extraction or a localhost callback URL have been found.

## Where the output goes

Never the project root. A loose `docs/*ANALYSIS.md` is exactly the litter this
rule exists to prevent, and it disagrees with the one-owning-document rule in
[`docs/README.md`](../README.md).

- A finding that needs action becomes an item in [`TODO.md`](../../TODO.md) in its
  line grammar, with the detail at `docs/backlog/<ID>.md` if one line will not do.
- A finding about a subsystem belongs in that subsystem's owning doc under
  `docs/`.
- A finding that changes a hard constraint belongs in
  [`INVARIANTS.md`](INVARIANTS.md), not in a new file.
