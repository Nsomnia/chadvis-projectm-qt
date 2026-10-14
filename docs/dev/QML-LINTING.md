# QML linting

ChadVis's UI is written in QML, and QML is a language where most mistakes are
invisible until runtime. A typo in a property name is not a compile error; a
`color` assigned to a `string` field is not a type error; a missing `id` means a
signal handler silently never runs. This is how you catch those before a user does.

## The command

```bash
./scripts/lint-qml.sh
```

That is the whole of what you need to remember. It prints either
`no blocking problems` — meaning no defect was found — or a short list of file,
line and column for each thing that is wrong.

Exit code is `0` when clean and `1` when a blocking problem was found, so it
works as a pre-commit check or a CI gate. CI runs it in `.github/workflows/build.yml`,
right after the build step.

Three modes:

```bash
./scripts/lint-qml.sh          # what matters (the default, and what CI runs)
./scripts/lint-qml.sh --strict # the full raw list, as a gate; fails on anything
./scripts/lint-qml.sh --list   # counts only, one line
```

## What it checks, in plain language

A linter that reports everything is a linter nobody runs. The output is split
into three buckets, and only the first one is something you have to act on:

| Bucket | Count today | Meaning |
| :--- | ---: | :--- |
| **blocking** | 0 | Something is wrong. Fix it before committing. |
| **unqualified** | 144 | `Theme.accent` written as `accent` inside a scope that has no local of that name — it resolves today by accident of scope. A style preference, kept as board item **T0048**. |
| **AOT diagnostics** | ~920 | The ahead-of-time compiler pass being conservative about types it cannot see through (`Cannot load property tab from QVariant`). Also T0048. |

The default mode prints blocking findings and *summarises* the other two, so
`./scripts/lint-qml.sh` is one screen you can read. `--strict` shows everything.

`.qmllint.ini` is what turns a raw `qmllint` run — about 1,100 lines, 900 of them
the same "can be shadowed" warning — into that 3-line summary. It sets the checks
the tree treats as errors and leaves the rest as noise. The file's own comments
record which is which and why, including what is *deliberately* disabled.

## Why the script wraps the build instead of calling `qmllint` directly

You might expect `qmllint src/qml/**/*.qml` to work. It does not, and not because
of anything you did — this tree has three conflicting requirements:

- `main.qml` uses relative local imports (`import "views"`), which resolve
  against the **source** layout.
- `Theme.qml` imports its own `ChadVis` module for `SettingsBridge`, which needs
  the **built** module.
- The built `ChadVis/qmldir` names its files relative to *itself*.

No single working directory satisfies all three, and running it wrong produces
"cannot access value for name Theme" on every file — 144 phantom defects that are
actually a resolution problem. So `scripts/lint-qml.sh` runs the build's own
`project_lib_qmllint` target, which CMake already wires correctly, and classifies
its output.

That is also why it prints a clear error telling you to run `./build.sh --fast
--tests` first: the lint target needs the generated `qmldir` and `qmltypes`.

## Fixing something it reports

```bash
$ ./scripts/lint-qml.sh
qmllint: 1 problem(s) that must be fixed:

Error: src/qml/panels/LyricsPanel.qml:4:1: Unused import [unused-imports]
```

Open that file, go to that line, delete the import. Re-run. That is the whole
loop — there is no build step between editing and checking, so it is fast.

## Reading the output without knowing QML

Each finding is `path:line:column: <what> [category]`, then the offending source
line, then a caret under the exact token. The `<what>` is a full sentence, not a
code:

- `Cannot assign binding of type QColor to QString` — a colour is being written
  into a text field. This exact finding caught T0047, where the theme's colour
  was going into a text field and coming back as garbage.
- `Unused import` — an import line nothing uses. Harmless, but it misleads a
  reader into thinking that module is available here.
- `Member X of Y can be shadowed` — a name that could resolve to something else
  depending on scope. Low priority, part of the T0048 backlog.

## Reducing the backlog

The 144 `unqualified` findings are the one bucket worth grinding down, and they
are mechanical: qualify the name (`Theme.accent` instead of `accent`) at each
site. Each fix makes a genuine accident less likely, because an unqualified name
resolves to whatever the nearest enclosing scope happens to define.

Do **not** reduce the backlog by turning the check off in `.qmllint.ini`. That
converts "there are 144 known work-items" into "the tree says there are none",
which is how a backlog disappears without being cleared — and this repo's
`AGENTS.md` treats that as the more serious of the two states.
