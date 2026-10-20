# Example: Embedded C Or RT Change

Use for firmware, embedded C, RT constraints, MMIO, ISR code, or fixed-resource targets.

## Starting Point

```text
Task: change driver or ISR-adjacent code.
Boundary: hardware register access, interrupt context, memory budget, timing budget.
Primary risks: volatile semantics, atomics/critical sections, stack use, undefined behavior, timing drift.
```

## Required Skill Path

1. Read `MEMORY-SAFETY.md`, `CONCURRENCY-DEADLOCKS.md`, and `PERFORMANCE.md`.
2. Record target constraints before editing:

```text
MCU/board:
compiler:
optimization flags:
stack/heap budget:
ISR or thread context:
timing deadline:
```

3. Prefer host-side unit tests for pure logic and target/simulator tests for hardware behavior.
4. Audit every `volatile`, MMIO access, interrupt mask, atomic, and shared buffer manually.
5. Treat timing and binary-size changes as behavior.

## Evidence Packet

```text
Embedded C evidence:
- Target and flags:
- Memory budget:
- Timing budget:
- ISR/thread context:
- Volatile/MMIO/atomic proof:
- Stack/heap impact:
- Host tests:
- Target/simulator evidence:
- Residual risk:
```

## Refusal Conditions

- Allocating memory, logging, or taking locks inside an ISR/signal-like context.
- Timing-sensitive change without before/after timing or cycle-budget reasoning.
- Replacing register/macro access without proving evaluation order and generated code impact.
