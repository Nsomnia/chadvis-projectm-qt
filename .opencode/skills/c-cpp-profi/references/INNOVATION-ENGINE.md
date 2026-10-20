# Innovation Engine

## Purpose

This is the idea-generation layer for `c-cpp-profi`. The rest of the skill verifies *implementations*; this reference governs *proposals*. It teaches an agent to enumerate improvements to a C/C++ codebase, separate low-risk accretive additions from high-upside radical bets, score them adversarially, and land them only behind the same falsifiable evidence gates the implementation ladder already enforces.

Ideas are cheap. Unproven ideas that ship are expensive. The contract here: every idea carries its own evidence before it earns an edit, exactly as a performance claim carries baseline/profile/oracle/after.

No problem-evidence means no idea. No behavior oracle means no radical change.

## Where This Sits

| Step | Action | Gate |
|---|---|---|
| 0 | Comprehend (entry-point, module-map, touched-path callgraph, intent) | `cpp_evidence_check.py --profile comprehension` |
| 1 | Enumerate the backlog from inventory + risk scan | `cpp_backlog.sh <repo>`, every row cites an evidence anchor |
| 2 | Author Idea Cards | `cpp_idea_check.py` / `--profile idea` |
| 3 | Score and select adversarially | accretive score + radical expected-value, portfolio rule |
| 4 | Land behind the matching implementation profile | `cpp_evidence_check.py --profile {port,modernize,rearchitect,...}` |

> **Tooling status (loop-tracked).** `cpp_inventory.sh`, `cpp_risk_scan.sh`, `cpp_backlog.sh`,
> `cpp_idea_check.py`, and
> `cpp_evidence_check.py`
> (profiles `basic/parser/memory/public-abi/concurrency/performance/refactor/native-ui/portability/security/docs-scripts/idea/comprehension/port/modernize/rearchitect`,
> the `comprehension` profile with `--require-comprehension-proof` and backed by [REPO-COMPREHENSION.md](REPO-COMPREHENSION.md),
> the `port`/`modernize`/`rearchitect` profiles with `--require-transform-proof` and backed by [CODE-TRANSFORM.md](CODE-TRANSFORM.md),
> and `--derive-profiles` (derives the required profile set from the report's `## Change Scope` answers and
> constrains those answers to `yes`/`no`) plus `--strict-numeric` (alias for `--require-performance-proof`))
> exist today. There is no remaining planned proposal-specific tool: the profile-derivation and strict-numeric
> steps named below are now run by `cpp_evidence_check.py` directly, not by hand. Still, never claim a gate you
> did not actually run, and where a step lacks a dedicated tool, perform it **manually** against the Idea Card
> below and record the result honestly.

Step 0 is mandatory. You may not propose a change to code you cannot model. "I understood it" is falsifiable: cite a symbol name or `file:line` for each field, the same way "I optimized it" cites a benchmark.

## Idea-Generation Lenses

Run every lens. Each is a question that surfaces non-obvious candidates; record the evidence anchor that triggered it.

| Lens | Prompt that surfaces non-obvious ideas | Typical anchor |
|---|---|---|
| Performance | Where does the profile spend time the source does not explain? What is allocated per call that could be amortized, batched, or made zero-copy? Which branch is mispredicted on hot data? | flamegraph node, `perf stat` IPC, allocation count |
| Safety | Which untrusted byte path has no fuzz harness? Which `malloc`+multiply has no overflow guard? Which raw owning pointer crosses a boundary that could throw? | `cpp_risk_scan.sh` hit, missing harness, ASan finding |
| API ergonomics | Which function takes a pointer+length where a `span` would remove a class of misuse? Which lifetime contract lives only in a comment, not the type? Which error is returned by sentinel that could be a typed result? | header signature, doc-only invariant |
| Portability | Which target/compiler/standard is never exercised in CI? Which `long`/`time_t`/endian/packing assumption is load-bearing and untested? | CI matrix gap, `sizeof`/`offsetof` assumption |
| Testability | Which module has no seam to inject failure (OOM, short read, EINTR)? Which output is checked by eyeball, not golden? | fault-path with no test, ungolded artifact |
| Domain-fit | Given the reconstructed intent, what does an elite peer in this domain do that this repo does not (dispatch, sharding, VFS, multi-handle)? | domain pack, peer-repo pattern |

Non-obvious prompts that beat the obvious list: "What would the maintainer reject and why?"; "What is the cheapest experiment that would *disprove* this is worth doing?"; "What does removing this enable that is currently impossible?"; "If this were 10x the size/load, what breaks first?"

## Enumerate Before Inventing

Do not free-associate. Derive the backlog mechanically so ideas are grounded, not hallucinated.

1. Run `cpp_inventory.sh` and `cpp_risk_scan.sh` (read-only) on the repo.
2. Feed both to `cpp_backlog.sh <repo>`. It emits a deduplicated capability-gap list across fixed lanes that hold for any C/C++ repo: **hardening** (missing `-D_FORTIFY_SOURCE`, sanitizer presets, CFI, stack protector), **API ergonomics** (missing const-correct / `span` / owner-annotated surfaces), **portability** (compilers/arches/standards not exercised), **test/fuzz coverage** (parsers with no harness, untested error paths).
3. Every emitted row must carry a derived-from-evidence pointer (the risk-scan `file:line` or the inventory key that produced it). A row with no anchor is a bug in the backlog, not a candidate.
4. Backlog is reproducible: two runs on the same tree byte-match. Falsification test for the engine itself: inject a known gap (delete a fuzz harness, add a `strcpy`) and the matching row must appear; remove it and the row must disappear.

For a domain with no pack, derive one with `references/UNKNOWN-DOMAIN.md`: infer trust boundary (who supplies bytes/pointers/privilege), failure-cost class (crash vs corruption vs silent-wrong vs safety-of-life), determinism/timing/ABI surface, and map each to the gate ladder. Produce a filled ad-hoc pack with detection signals, gates, oracles, refusal conditions, and an honest "risks I cannot yet gate" list.

## Idea Card

Author one per surviving candidate. `cpp_idea_check.py` validates it exactly as `cpp_evidence_check.py` validates a gate report: blank or placeholder fields (`feels slow`, `tbd`, `maybe`) fail with a field-level reason.

```text
Idea:
- Kind:                 accretive | radical
- Lens(es):
- Problem-evidence:     profile %, crash, CVE class, or coverage gap (cited anchor) — NOT "feels slow"
- Prior-art-check:      tried/rejected in this repo's history, issues, or upstream?
- Proposed change:      one sentence; the single lever
- Reversibility:        can it be reverted as one clean commit?
- Blast-radius:         ABI/API/build/ownership/concurrency surfaces touched
- Behavior oracle:      what proves behavior is unchanged (or intentionally changed)
- Score:                impact * confidence / effort  (and EV for radical)
- Kill-criteria:        measurable condition under which the idea is abandoned
```

## Adversarial Scoring

Never commit the first idea. Compete them, steelman, refute, then score.

1. **Compete**: for each problem, hold at least two distinct candidates (e.g. amortize allocation vs change the data structure; scoped `#ifdef` vs a HAL).
2. **Steelman each**: state the strongest case for the candidate, including the upside if it fully succeeds.
3. **Refute each**: name the failure mode, the maintainer's likely objection, and the cheapest disproof. A candidate that survives its own refutation earns a score.
4. **Score**:

   ```text
   accretive_score = impact * confidence / effort      # implement when >= 2.0
   radical_EV      = upside * P(success)  ± uncertainty # do NOT penalize low confidence
   ```

   The accretive score deliberately punishes low confidence; the radical EV deliberately does not, so a high-upside/high-uncertainty bet is not auto-killed by the perf-style scorer. Score every backlog row with **both**.

5. **Portfolio rule**: each ideation pass must carry at least one `kind=radical` candidate forward even when accretive items dominate by raw score. This is the mechanism that stops the engine from only ever proposing safe cleanups. Machine-enforced: `cpp_idea_check.py <cards.md> --require-radical` fails unless at least one card is `Kind: radical`.

| Dimension | 5 | 3 | 1 |
|---|---|---|---|
| Impact | removes a defect class or unlocks a target/algorithm | measurable local win | cosmetic |
| Confidence | oracle exists, census done, peer precedent | tests cover main path, ABI partial | hunch |
| Effort | one file, one lever | one module | cross-cutting, multi-commit |

## Taxonomy: Accretive vs Radical

| | Accretive | Radical |
|---|---|---|
| Definition | incremental, low-risk, high-certainty addition | re-architecture, new algorithm/data structure, SIMD/lock-free/zero-copy redesign, new verification mechanism |
| Reversibility | trivially revertible | revertible only with planning; demands a migration ledger |
| Gate floor | matching implementation profile + oracle | behavior oracle + baseline + ABI/API check + reversible one-lever commit, all four, no exceptions |
| Failure mode if rushed | small regression | corruption, ABI break, silent-wrong across the whole consumer set |

### Accretive examples (drawn from elite repos)

| Idea | Elite precedent | Anchor pattern |
|---|---|---|
| Add a narrow fuzz harness to an untrusted parser | SQLite `test/dbfuzz2.c` (bounded size + progress callback) | parser entry point with no harness |
| Add a scalar reference path alongside the fast path for differential testing | simdjson reference vs SIMD implementations | optimized backend with no oracle |
| Add `static_assert` on `sizeof`/`offsetof` at an ABI boundary | curl version/ABI sync tests (`tests/data/test1119`) | layout assumption in a comment |
| Add `alignas(hardware_destructive_interference_size)` padding to a hot shared struct | false-sharing recipe | TSan/perf contention on a counter |
| Bounded-copy migration (`strcpy` -> `snprintf`/`strlcpy` with truncation semantics) | hot-path review failure modes (curl `CODE_REVIEW.md`) | `cpp_risk_scan.sh` `strcpy` hit |

### Radical examples (drawn from elite repos)

| Idea | Elite precedent | Why radical |
|---|---|---|
| Runtime ISA dispatch with per-implementation kernels | simdjson `src/implementation.cpp` dispatch | new dispatch layer, ABI surface, must keep portable fallback + differential oracle |
| Sharded / thread-local free lists in the allocator | mimalloc free-list sharding | concurrency invariant change, allocator family ownership, needs TSan + Valgrind/ASan-aware validation |
| Pluggable backend behind a virtual interface | SQLite VFS | new extension boundary, vtable ABI, fault-injection contract |
| Single-driver multiplexing many handles | curl multi-handle | re-architecture of the I/O loop, ordering and lifetime semantics change |

## Mandatory Evidence Gates for Radical Change

No radical change lands without **all four**. These are checked by the implementation profiles, not by promises.

1. **Behavior oracle** — golden outputs, corpus replay, differential test against the reference/origin, or recorded traces. For a `port`, this is a *differential oracle* naming origin triple, target triple, emulator/hardware path, and corpus size (`--profile port --require-transform-proof` verifies the named substructure, not a bare grep). See [CODE-TRANSFORM.md](CODE-TRANSFORM.md).
2. **Baseline measurement** — captured before the change with command, inputs, CPU, compiler, flags, commit. A perf claim must pass `--strict-numeric` (today an alias for `--require-performance-proof`, so the performance gate must carry baseline/before, profile/hotspot, score/opportunity, oracle/golden/isomorphism, and after/result). The richer numeric oracle — `cpp_perf_proof.py` parsing real `hyperfine`/Google-Benchmark/`perf stat` JSON, requiring after to beat before by more than `k*stddev` over the same command/input, and FAILing a regression or within-noise export — is the next planned increment, not yet shipped; until it exists, capture and read that JSON by hand.
3. **ABI/API check** — `cpp_abi_snapshot.sh` before/after; no unintended symbol/layout delta on any public boundary. A `modernize` carries a per-transform isomorphism field plus the ABI snapshot; a `rearchitect` carries a rearchitecture ledger plus tests plus ABI.
4. **Reversible one-lever commit** — one lever per commit; the change reverts cleanly. A `rearchitect`/`port` carries a migration ledger with a per-commit caller census (`--profile rearchitect --require-transform-proof` verifies it). See [CODE-TRANSFORM.md](CODE-TRANSFORM.md).

### Profiles that enforce this

`cpp_evidence_check.py` `PROFILE_REQUIRED` extends beyond `refactor`/`portability`/`performance` with real proposal-aware profiles:

| Profile | Required gates |
|---|---|
| `comprehension` | filled, cited `entry-point:`, `module-map:`, `touched-path-callgraph:`, `intent:` |
| `idea` | filled Idea Card (problem-evidence, prior-art, reversibility, blast-radius, kill-criteria) |
| `port` | differential oracle (named origin+target triple, emulator path, corpus size) |
| `modernize` | modernization isomorphism (per-transform clang-tidy check) + ABI/API |
| `rearchitect` | rearchitecture ledger + tests + ABI/API |

Use `--derive-profiles` so the checker reads the `## Change Scope` yes/no answers and computes the minimum required profile set (parser-touched -> `parser`+`security`; ABI-touched -> `public-abi`; threads-touched -> `concurrency`; perf-claim -> `performance`+`--require-performance-proof`). Scope answers are constrained to exactly `yes`/`no`; `maybe` and `sort of` are rejected so the answers are machine-usable. This closes the hole where an all-surfaces-yes report passed on `--profile basic` alone.

## Anti-Patterns

| Anti-pattern | Why it fails | Stop signal |
|---|---|---|
| Novelty for its own sake | SIMD/lock-free/template metaprogramming because it is impressive, not because the profile demands it | radical idea with no profile-backed problem-evidence -> reject at `cpp_idea_check.py` |
| Unproven micro-opt | "this should be faster" with no baseline; cache effects unmeasured | perf claim without `--strict-numeric` proof -> reject |
| Scope creep | one idea drags formatting, behavior, and three modules into one diff | more than one lever in a commit -> split or reject |
| Abstraction for two callsites | generalizing before the rule of 3, closing an extension set prematurely | < 3 callsites and no evidence the open abstraction is needed |
| "Cleaner/modern/tool-suggested" rationale | not evidence; clang-tidy suggesting a transform is a candidate, not a justification | rationale with no oracle and no measured complexity removed |
| Hiding intentional behavior change inside a "refactor" | breaks the isomorphism contract silently | golden/ABI delta presented as incidental |
| Prior-art amnesia | re-proposing something this repo or upstream already tried and rejected | empty `prior-art-check` field |

## Stop Conditions

Stop and do not commit when any holds:

- Problem-evidence is a feeling, not a cited anchor (profile %, crash, CVE class, coverage gap).
- A radical change is missing any of the four mandatory gates.
- The behavior oracle does not exist yet, or its corpus is too small to bind the change.
- ABI/API snapshot shows an unintended delta on a public boundary and the task did not authorize an ABI break.
- The change cannot be expressed as one reversible lever.
- The kill-criteria are unmeasurable, so the idea can never be falsified.
- The portfolio carries zero radical candidates *and* zero radical candidates were honestly considered and rejected with reasons.

## Documentation Of Ideas

Before publishing any README, architecture, or API doc describing a landed idea, run `de-slopify` over the prose. Delegate documentation surfaces to the sibling skills rather than inventing parallel guidance: `readme-writing` (structure, badges, install/usage), `changelog-md-workmanship` (changelog + release notes reconstructed from real tags/commit ranges, ABI-break = MAJOR), `documentation-website-for-software-project` (docs-site build/deploy), and `de-slopify` (remove AI tells — no "seamless/robust/leverage/in today's world", no marketing em-dashes, no empty "comprehensive solution" framing). A README still has to pass `readme-writing`'s required-section check after de-slopification; an architecture writeup reuses this skill's own ownership/ABI vocabulary, not generic boilerplate.
