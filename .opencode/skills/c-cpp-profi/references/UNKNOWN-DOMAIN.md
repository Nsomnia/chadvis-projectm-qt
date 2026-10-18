# Unknown-Domain Pack Derivation

## Purpose

This is the recipe an agent runs when it meets a C/C++ codebase whose domain matches **none** of the seed packs in [DOMAIN-AGNOSTIC-MASTERY.md](DOMAIN-AGNOSTIC-MASTERY.md). The seed packs are worked examples; this file is the procedure that **derives a new one on the spot** so the agent is never gated against the wrong oracle. The output is a filled ad-hoc Domain Pack in the exact shape the seed packs use (Authorities / Invariants / Oracle / Constraints / Toolchain / Failure modes), plus refusal conditions and an honest list of risks the agent cannot yet gate.

Rule: an ad-hoc pack is provisional. It is a hypothesis about the domain, confirmed by a `file:line`, never by a filename hunch. Until every line is grounded in repo evidence or a maintainer answer, treat empty fields as residual risk that blocks landing, not as background.

## When to run this

Run this when [DOMAIN-AGNOSTIC-MASTERY.md](DOMAIN-AGNOSTIC-MASTERY.md)'s Pack-Selection Procedure and `cpp_domain_detect.sh` print `unknown-domain`, or when they print a pack you cannot confirm against the source. A wrong pack is worse than no pack: it gates the wrong oracle and produces a green report over the wrong invariants. Derive before you edit.

## Derivation Procedure (numbered; produce a filled pack)

### 1. Infer the TRUST BOUNDARY — who supplies the bytes, pointers, privilege, and timing?

The single most load-bearing question: what crosses into this code that the code does not control? Grep and read for it; record each with a `file:line`.

- **Bytes**: network sockets (`recv`, `read` on a fd), files, IPC, shared memory, deserialization, command-line/env, register reads. Any input from outside the process is attacker-influenced until proven otherwise.
- **Pointers / handles**: `__user` pointers, FFI/`extern "C"` callbacks, plugin/vtable entry points, callbacks invoked by a foreign runtime.
- **Privilege**: does this run as root / in kernel / in an enclave / with a capability another component lacks? Privilege asymmetry across the boundary is where escalation lives.
- **Timing**: is this code on someone else's deadline (an ISR, an audio callback, a watchdog window, a request budget)? A caller that owns the clock is a trust boundary too.

Output: one sentence per boundary — "X supplies Y, validated at `file:line` (or NOT validated)".

### 2. Classify the FAILURE-COST — this sets gate strictness

Pick the highest class that applies. The class, not the language, decides how strict the gates are.

| Class | What a defect does | Gate posture |
|---|---|---|
| crash | process dies, restarts, no persistent damage | sanitizers + focused tests; fuzz untrusted parsers |
| corruption | persistent state / data is silently wrong on disk or in a shared store | + crash-consistency oracle, differential vs reference, checksums |
| silent-wrong | output is plausible but incorrect; no crash, no alarm | + a reference/differential oracle is **mandatory** (tests alone cannot catch this) |
| safety-of-life | injury or death (avionics, medical, motion, automotive) | + certification regime, formal/HIL evidence, forbidden-construct proof; refuse to land without the domain's mandated process |
| financial | money is moved/lost/double-spent | + exactness oracle (no float for money), idempotency/replay proof, audit trail, adversarial review |

Record the chosen class and the one-line justification ("a wrong WAL replay corrupts the user's database -> corruption").

### 3. Map the DETERMINISM / TIMING / ABI surface

- **Determinism**: must the same input always produce the same output (replay, golden tests, lockstep)? Or is nondeterminism (threads, FP reassociation, hash-order) allowed — and if so, bounded how?
- **Timing**: is there a deadline (WCET, ISR latency, audio buffer period, request SLA)? Is blocking, allocation, or locking forbidden on any path? Find the hot/real-time path and what it may not do.
- **ABI**: is there a frozen boundary — exported symbols, a stable on-disk/on-wire format, a plugin vtable, a syscall/ioctl surface? Anything a separate build consumes is ABI and is part of behavior.

### 4. Find or BUILD the ORACLE — what "correct" is measured against

This is the field that most often does not exist yet; if so, **building it is the first task**, before any edit. Ranked by strength:

1. **Reference implementation** — a slower/simpler/known-good version to diff against (scalar vs SIMD, interpreter vs JIT, the spec's example code).
2. **Spec test vectors** — published KATs, RFC conformance vectors, format conformance suites.
3. **Differential** — two independent implementations (or two builds, two compilers, two arches) fed the same corpus; divergence is a bug.
4. **Recorded transcripts / golden artifacts** — captured real I/O replayed; exact-match or tolerance-compared.
5. **Hardware-in-the-loop (HIL) / simulator** — for embedded/motion/RF where the only ground truth is a real or simulated device.
6. **Formal model** — a checked model (TLA+, Alloy, a refinement) for protocols/state machines where exhaustive testing is infeasible.

If none exists, state which one you will build and gate against; a green generic unit test over a missing oracle proves nothing.

### 5. Select gates from QUALITY-GATES.md per the failure-cost class

Map the class from step 2 onto the gate ladder in [QUALITY-GATES.md](QUALITY-GATES.md) and the Universal Core in [DOMAIN-AGNOSTIC-MASTERY.md](DOMAIN-AGNOSTIC-MASTERY.md):

- crash -> compile-clean + static + ASan/UBSan + focused tests; fuzz any untrusted-byte parser at its boundary.
- corruption -> add a crash/fault-injection oracle and a differential or checksum check; any persistence path gets a power-fail/replay test.
- silent-wrong -> add the reference/differential oracle as a hard gate; bitwise or tolerance-bounded comparison, never eyeball.
- safety-of-life -> add the certification analyzer (MISRA/CERT/Power-of-Ten), forbidden-construct proof, and HIL/formal evidence; honor the regime's process.
- financial -> add exactness, idempotency/replay, and audit-trail gates.

For each selected gate, also name what is **forbidden or reshaped** for this domain (no post-init heap, no host ASan on a device build, no blocking in the callback) — mirror the Pack-to-gate mapping table's "Forbidden / re-shaped" column.

### 6. Write REFUSAL CONDITIONS and an honest "risks I cannot yet gate" list

State explicitly when NOT to edit:

- The trust boundary is unmapped — an input path with no `file:line` answer.
- The failure-cost is safety-of-life or financial and the mandated oracle/process is absent.
- The oracle does not exist and cannot be built within the task, so a change cannot be proven behavior-preserving.
- The forbidden-construct rules are unknown (you cannot prove you did not add a heap allocation to a no-heap target).

Then list, plainly, the **risks I cannot yet gate**: every invariant you suspect but could not ground, every path the oracle does not cover, every tool the environment lacks. File a bead per item. An empty field in the pack is one of these risks, not silence.

## The Filled Ad-Hoc Pack (write this; same shape as the seed packs)

```text
## Domain Pack: <name> (ad-hoc, provisional)

- Detection signals: <grep hit file:line> + <grep hit file:line>   # what selected this domain
- Trust boundary: <who supplies bytes/pointers/privilege/timing, each file:line, validated? Y/N>
- Failure-cost class: crash | corruption | silent-wrong | safety-of-life | financial — <one-line justification>
- Authorities: <spec/RFC/standard/vendor TRM/coding standard, with where it is cited in-repo>
- Invariants: <resource ceilings, timing/ordering, state-machine legality, numeric semantics — each, and how enforced today>
- Oracle: <reference impl | spec vectors | differential | transcript | HIL | formal — exists? if not, "build first">
- Constraints: <determinism, real-time, ABI/on-disk/on-wire freeze, certification/coding standard>
- Toolchain: <compiler/SDK/simulator/analyzer that augments the generic gates>
- Failure modes: <the domain's signature recurring bugs a generalist would miss — gate each>
- Gates selected (from QUALITY-GATES.md): <list>; forbidden/reshaped: <list>
- Refusal conditions: <when NOT to edit>
- Risks I cannot yet gate: <honest list; one bead each>
```

Then map the pack onto the Universal Core: which UB classes are amplified, which ABI/format surface is frozen, which gate becomes mandatory, which is forbidden — exactly as the seed packs do.

## Worked micro-example (an unbriefed domain: a smart-contract / on-chain bytecode VM written in C++)

Signals (each a real hit, not a hunch): an `interpreter.cpp` with a `dispatch`/opcode `switch`, a `gas`/`fuel` counter decremented per op, a `Stack`/`Memory` struct with hard size limits, a `state_root`/Merkle-trie module, deterministic 256-bit integer arithmetic, no float anywhere on the execution path.

Derived pack:

- **Trust boundary**: untrusted bytecode + untrusted call data supplied by any caller (`interpreter.cpp:exec(bytes code, bytes input)` — fully attacker-controlled, no validation pre-dispatch). Privilege: the VM mediates access to shared world state.
- **Failure-cost class**: **financial + silent-wrong** — a wrong opcode result or a consensus divergence between two nodes moves or destroys funds and forks the chain. Highest strictness.
- **Determinism/ABI**: bit-for-bit determinism is the whole game — every node must compute the identical state root from the same input; no FP, no `unordered_map` iteration order, no UB-dependent behavior; the gas schedule and bytecode encoding are a frozen on-wire/consensus ABI.
- **Oracle**: differential against a reference VM (a second independent implementation) over a shared conformance corpus + the spec's official test vectors; divergence on any input is a consensus bug. Build the differential harness first.
- **Constraints**: gas metering must be exact and overflow-safe; stack/memory bounds enforced before every access; 256-bit math wraps with defined semantics, never UB.
- **Toolchain**: libFuzzer at the bytecode-decode boundary + ASan/UBSan; the reference-VM differential runner; the official conformance vectors.
- **Failure modes**: integer overflow in gas accounting, missing stack-bound check on a crafted opcode sequence, nondeterminism leaking in via iteration order or FP, divergence from the reference VM on an edge-case opcode, a UB the optimizer turns into a consensus split between compilers.
- **Gates**: fuzz + ASan/UBSan + the reference-VM differential as a hard gate (silent-wrong); exactness/overflow review (financial). **Forbidden**: float on the execution path, any container with unspecified iteration order in consensus-visible code, any UB.
- **Refusal conditions**: do not edit the dispatcher without the differential oracle running; do not touch gas accounting without the conformance corpus.
- **Risks I cannot yet gate**: timing/DoS gas-griefing not covered by the differential; cross-compiler determinism unproven until a second toolchain runs the corpus.

None of this comes from a generic C++ checklist. It comes from filling the template against the repo and the consensus spec — and every line above is a hypothesis to confirm with a `file:line`, not a filename hunch.

## Closing rule

The ad-hoc pack is **provisional**. Record the pack and the exact signals that selected it (`file:line`) in the gate report so the choice is auditable, and re-confirm each invariant against the source before it gates a real change. Confirm with a `file:line`, not a filename hunch. See [DOMAIN-AGNOSTIC-MASTERY.md](DOMAIN-AGNOSTIC-MASTERY.md) for the seed packs to copy the shape from and the Universal Core every pack constrains.
