# Agent skills

Which skill covers what, where each one comes from, and where the gaps are. This
file owns the *inventory*. A skill's own `SKILL.md` owns its procedure;
[`AGENTS.md`](../../AGENTS.md) owns the operating rules; [`INVARIANTS.md`](INVARIANTS.md)
owns *why* a constraint exists. A skill must not restate a fact those files already
own — it routes to them.

## Harness layout

The same `SKILL.md` works in every harness; only the directory differs. Frontmatter
is `name` + `description`, and the description is what the model matches on, so it
must say **when** to use the skill, not just what it does.

| Harness | Project scope | User scope |
| --- | --- | --- |
| OpenCode | `.opencode/skills/<name>/SKILL.md` | `~/.config/opencode/skills/` |
| WorkBuddy | `.workbuddy-ai/skills/<name>/SKILL.md` | `~/.workbuddy-ai/skills/` |
| Claude Code | `.claude/skills/<name>/SKILL.md` | `~/.claude/skills/` |
| Kilo | `.kilo/rules/*.md` (rules; no skill loader observed) | not established |

`.opencode/skills/` is the canonical copy. The WorkBuddy and Claude Code project
paths are symlinks to it, so there is one source of truth and no second copy to
drift:

```bash
ln -s ../.opencode/skills .workbuddy-ai/skills
ln -s ../.opencode/skills .claude/skills
```

Claude Code reads `CLAUDE.md`, not `AGENTS.md`. The root `CLAUDE.md` is a one-line
import (`@AGENTS.md`) precisely so the operating rules are not invisible in that
harness. Do not put rules in it — that would be a second copy.

Note that the `find-skills` helper installed in this environment documents
`~/.workbuddy/skills/` as the user-level target. On this machine that directory does
not exist; the live one is `~/.workbuddy-ai/skills/`. Trust the directory that
exists, not the helper's prose.

## Vendored: the official Qt set

All **13** skills from
[`TheQtCompanyRnD/agent-skills`](https://github.com/TheQtCompanyRnD/agent-skills)
are present and match upstream's `skills/` listing exactly — nothing is missing and
nothing is hand-edited:

`qt-cpp-review`, `qt-qml-review`, `qt-qml`, `qt-canvas2d`, `qt-ui-design`,
`qt-cmake-project`, `qt-cpp-docs`, `qt-qml-docs`, `qt-qml-test`, `qt-qml-test-run`,
`qt-qml-profiler`, `qt-figma-token-extraction`, `qt-figma-component-generation`.

Refresh from upstream rather than editing the vendored copies. QGroundControl's
`tools/skills/` carries only 10 of these, so it is a subset, not a better source.

The Figma pair needs Figma API access this project does not use; they are inert but
harmless.

## Vendored: C++

Four skills overlap substantially, which is a consolidation candidate rather than a
feature:

| Skill | Actually distinct contribution |
| --- | --- |
| `c-cpp-profi` | The only one with scripts, domain detection, fuzz/CI assets, and a gate-selection procedure. The most useful of the four. |
| `modern-cpp` | C++20/23/26 idiom reference, incl. `std::expected` — matches this tree's error-handling convention. |
| `cpp-coding-standards` | Core Guidelines as a checklist. |
| `cpp-pro` | Prose persona; no unique reference material. |

`cpp-pro` is the weakest of the four and duplicates the other three's ground. It is
left in place rather than deleted unilaterally. `cpp-pro` and `c-cpp-profi` carry no
license file upstream: treat both as unlicensed third-party content, not to be
redistributed beyond this repository.

## Custom: this repository

| Skill | Owns | Added |
| --- | --- | --- |
| `chadvis-verify` | Build profiles, the ctest zero-tests trap, sanitizer lanes, "green is not GL evidence". | pre-existing |
| `suno-capture-client` | Evidence grades, fail-closed host policy, the one-owner HTTP policy, credential/log hygiene. | 2026-10-20 |
| `projectm-gl-render` | Framebuffer-0 rule, leak-instead-of-wrong-namespace-delete, the GL test fixture traps. | 2026-10-20 |
| `chadvis-audio-recorder` | Total PCM conversion, split format-context ownership, PTS, the sanitizer gate. | 2026-10-20 |

`chadvis-verify` and the three above are deliberately complementary: verify owns
*how to build and test*, the others own *what is true in their subsystem*. Where a
fact is already owned by `INVARIANTS.md`, the skill links to it instead of copying.

## Gaps that no public skill fills

Searched, and recorded so nobody repeats the search:

- **Suno.** No agent skill exists anywhere. SkillHub returns only generic
  programming skills (relevance ≈ 0.05); the Vercel and ClawHub registries return
  nothing. What does exist is reverse-engineering *clients* —
  `gcui-art/suno-api` (~3.3k★), `SunoAI-API/Suno-API` (~1.8k★),
  `Malith-Rukshan/Suno-API` — and they are the wrong model for this tree: they call
  a hosted proxy, whereas ChadVis is capture-derived with a fail-closed host
  allowlist. Useful as background reading only; **never** as a spec, and never as an
  allowlist source. `docs/suno_api/THIRD-PARTY-IMPLEMENTATIONS.md` already owns that
  judgement.
- **projectM.** Only bindings and ports exist upstream (`projectm-rs`, `pym`, Kodi
  and Apple Music plugins). No agent skill, and none of them know this tree's
  framebuffer-0 constraint. Hence the custom skill.
- **Audio / FFmpeg.** No skill found. The rules that matter here (the two
  format-context deleters, total PCM conversion) are project-specific anyway.
- **OpenCode itself.** No skill documents this repo's harness layout. That is now
  this file's job rather than a skill's, because it is a fact, not a procedure.

## Authoring rules

1. **Route, do not duplicate.** If `INVARIANTS.md`, `ENDPOINT-INVENTORY.md`,
   `TESTING.md` or a header comment already owns the fact, link to it. A second copy
   is a bug — and a skill that restates a rule will be believed after the rule
   changes.
2. **The description is the trigger.** Name the directories and file paths a task
   would touch, plus the "use when" clause. A vague description means the skill
   never loads.
3. **State the gate.** Every skill that can produce code should name the exact
   build/test command and what a *false* green looks like in its area.
4. **Say "unverified" plainly.** GL, burn-in and audio skip headless, and ctest
   counts a skip as success. A skill that implies a green run proves pixels is worse
   than no skill.
