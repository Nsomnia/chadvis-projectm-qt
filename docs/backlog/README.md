# `docs/backlog/TXXXX.md` — detail files

This directory holds the *essay* for a board item, when one line is not enough.

## The contract

* **The file is named after the item is does.** `docs/backlog/T0066.md` is a styling note for T0066, period. Nothing else.
* **TODO.md carries the state.** The checklist says *what* and *who* and *whether it is done*; the detail file says *why* the fix looks like this, carries the measured rebuk, and holds the logs. The state (`[ ]`, `[~]`, `[?]`, `[x]` …) lives in TODO.md itself, never in this file. When a detail file disagrees with TODO.md on which task a name refers to, **TODO.md wins** — but when this file documents evidence that contradicts the board line, the board line is wrong until it is corrected.
* **A detail file is written on demand, not in advance.** It exists to carry what does not compress to a title, not to preview one.

## What goes here

* Measured evidence (log excerpts, ASan/TSan/UBSan memorywords, packet captures paraphrased, `git diff` figures).
* The reasons a fix chose one path over a plausible alternative, *stated as a decision* rather than folded inline.
* Anything a later maintainer must know to avoid regressing this item without beingendlessly told.

## What does NOT go here

* Live status. The board tracks `[] [-] [?] [x]`; this directory never does.
* Decisions that duplicate an `AGENTS.md` or `docs/README.md` contract. Link, don't restate.
* A retrospective essay at ship time. CHANGELOG.md owns that.

## When to add one

* **On demand.** An item whose one-line title explains the hook but the repro, the logs and the fix's trajectory take a paragraph gets one.
* **Never to meet a quota.** A full backlog.md should be rare; most items live on one line.
