# Example: Idea Generation For A JSON Parser

Use for the proposal layer: enumerate improvements, separate accretive additions
from radical bets, score them adversarially, and land each behind its own
evidence gate. Cards here are validated by `cpp_idea_check.py`.

## Starting Point

```text
Repo: a small C++ JSON parser (src/parse.cpp, include/json.hpp).
Boundary: untrusted bytes -> DOM; public header consumed by 3 callers.
Primary risks: bounds on the scanner hot loop, recursion depth, ABI of json::value.
```

## Idea Card

```text
Idea:
- Kind:               accretive
- Lens(es):           Testability, Safety
- Problem-evidence:   src/parse.cpp:140 number scanner has 0% fuzz coverage; no harness exists under tests/fuzz/
- Prior-art-check:    no parser harness in git history; upstream issue #41 asked for one, never landed
- Proposed change:    add a 64 KiB-bounded libFuzzer harness over json::parse with a seed corpus
- Reversibility:      yes; one commit adding tests/fuzz/parse_fuzzer.cc plus CMake target
- Blast-radius:       test-only; no public API/ABI, no build default change (opt-in preset)
- Behavior oracle:    corpus replay must not crash under ASan+UBSan; existing golden DOM tests unchanged
- Score:              impact 5 * confidence 5 / effort 1 = 25 (>= 2.0, implement)
- Kill-criteria:      abandon if harness finds 0 new edges after 10M execs on 3 distinct seeds
```

## Idea Card

```text
Idea:
- Kind:               radical
- Lens(es):           Performance, Safety
- Problem-evidence:   perf shows 41% of parse time in the scalar whitespace skip at src/parse.cpp:88
- Prior-art-check:    SIMD skip tried in branch simd-2022, reverted for portability; no fallback existed
- Proposed change:    add runtime-dispatched SSE2/NEON whitespace skip with a scalar fallback kernel
- Reversibility:      yes; revert the dispatch translation unit, scalar path remains the default
- Blast-radius:       new dispatch layer and one internal symbol; no public ABI change to json::value
- Behavior oracle:    differential test scalar vs SIMD over the existing 12k-file corpus, byte-equal DOM
- Score:              accretive 5*3/3=5.0; radical EV = upside(0.41 cut)*P(0.6) = strong, carry forward
- Kill-criteria:      abandon if differential oracle diverges on any corpus file or speedup < 1.15x at p95
```

## Adversarial Step (compete, steelman, refute, score)

- Compete: against the SIMD skip, hold "branchless scalar skip" as the cheaper rival.
- Steelman SIMD: removes the dominant 41% hotspot; dispatch keeps every target buildable.
- Refute SIMD: maintainer will reject any path with no scalar fallback (the 2022 revert);
  cheapest disproof is the differential oracle finding a single byte-divergent DOM.
- Score: accretive harness wins on raw score (25); the SIMD bet is carried as the mandatory
  radical candidate (portfolio rule) because its EV is high despite middling confidence.

## Evidence Packet

```text
# C/C++ Gate Report

## Change Scope
- Issue/task: enumerate JSON-parser improvements (accretive harness + radical SIMD bet)
- Touched files: tests/fuzz/parse_fuzzer.cc (proposal only)
- Public API/ABI touched: no
- Performance claim: no

## Commands
| Gate | Status | Command | Evidence |
|---|---|---|---|
| idea card | passed | python3 .opencode/skills/c-cpp-profi/scripts/cpp_idea_check.py examples/idea-generation.md | idea-card: validated by cpp_idea_check.py; kind: accretive+radical; score: 25 and EV-carried; cards: 2 |
```

## Refusal Conditions

- Problem-evidence is a feeling (`feels slow`) instead of a cited anchor.
- A radical card with no behavior oracle or no reversible one-lever commit.
- Portfolio carries zero radical candidates and none were honestly rejected with reasons.
