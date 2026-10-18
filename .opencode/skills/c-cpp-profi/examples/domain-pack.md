# Example: Deriving a Domain Pack for an Unbriefed Domain

Walk this when `cpp_domain_detect.sh` prints `unknown-domain` and none of the 11
Seed Packs in [DOMAIN-AGNOSTIC-MASTERY.md](../references/DOMAIN-AGNOSTIC-MASTERY.md)
fit. It follows the 6-step recipe in
[UNKNOWN-DOMAIN.md](../references/UNKNOWN-DOMAIN.md) end-to-end and ends with a gate
report. Target here: a **medical infusion-pump dosing controller** on a Cortex-M
RTOS — safety-of-life, governed by IEC 62304 / IEC 60601-1-8, matched by no seed
pack (the closest, industrial motion control, is IEC 61508 with a HIL plant model,
not a clinical dosing oracle).

## Starting Point

```text
Repo: firmware/ for a syringe infusion pump (src/dose_engine.c, src/order_rx.c).
Boundary: a clinician-programmed infusion order arrives over a serial clinical bus.
Primary risks: a wrong flow rate over-doses a patient; a dropped alarm hides it.
```

## 1. Trust boundary (who supplies bytes/pointers/timing)

- Bytes: the infusion order frame is parsed at `src/order_rx.c:88` (`parse_order(buf, len)`)
  from the clinical-bus UART — fully external, length **not** validated before the rate/VTBI fields are read (N).
- Timing: the dosing loop runs as a fixed cyclic task; the watchdog window is
  asserted at `src/dose_engine.c:212` — a missed step is on the patient's clock (a trust boundary).
- Privilege: the controller drives the motor and owns the occlusion/air-in-line alarm latch; no other component can override it.

## 2. Failure-cost class

safety-of-life — a wrong programmed rate or a suppressed alarm injures or kills the
patient. This forces the strictest gate posture: certification analyzer, a
forbidden-construct proof, and HIL evidence; refuse to land without the IEC 62304 process.

## 3. Determinism / timing / ABI surface

- Determinism: same order + same state must yield the identical commanded rate; no FP reassociation in the dose math, no `unordered_map` order in alarm dispatch.
- Timing: the cyclic dosing step has a hard deadline; no `malloc`, no blocking, and no unbounded loop on that path.
- ABI: the clinical-bus order frame is a frozen on-wire format; the drug-library blob on flash is a versioned on-disk format the mount path must validate before trusting any field.

## 4. The derived ORACLE

HIL: a pump-test rig with a calibrated flow sensor replays a recorded set of
clinician orders and asserts delivered volume against programmed VTBI within the
labeled accuracy band (tolerance, never bitwise), plus an independent reference
dose-rate model fed the same orders for differential checking. The rig must exist
before any dose-math edit; a generic unit test over a missing flow oracle proves nothing.

## 5. Filled ad-hoc pack (seed-pack shape)

```text
## Domain Pack: medical infusion-pump dosing controller (ad-hoc, provisional)

- Detection signals: parse_order(buf,len) at src/order_rx.c:88 + watchdog kick at src/dose_engine.c:212 (no seed-pack grep matched)
- Trust boundary: clinical-bus order bytes (src/order_rx.c:88, length NOT validated, N); patient/watchdog timing (src/dose_engine.c:212)
- Failure-cost class: safety-of-life — wrong rate or dropped alarm injures the patient
- Authorities: IEC 62304 (SW lifecycle), IEC 60601-1-8 (alarms), IEC 60601-2-24 (infusion accuracy), MISRA C:2012; cited in firmware/docs/safety-case.md
- Invariants: programmed rate within label bounds, saturates never wraps; VTBI countdown monotone; alarm latch reaches safe state within deadline; no post-init heap on the dosing path
- Oracle: HIL flow-sensor rig + reference dose-rate model (differential, tolerance-bounded) — build first
- Constraints: deterministic dose math, hard cyclic deadline, frozen order frame + versioned drug-library format, IEC 62304 traceability
- Toolchain: Cortex-M cross compiler, MISRA analyzer, stack-usage analyzer, the HIL rig + reference model, libFuzzer on parse_order at the host
- Failure modes: unvalidated order length over-reads the rate field; integer overflow in rate*time VTBI math; alarm starved by a missed cyclic step; FP nondeterminism diverging from the reference model; drug-library version bump misread as a valid dose
- Gates selected (from QUALITY-GATES.md): comprehension; static analysis + ASan/UBSan + parse fuzz (security); MISRA/forbidden-construct proof + HIL evidence (safety-of-life)
- Forbidden/reshaped: heap on the dosing path forbidden; host ASan only — never on the device build; trusting order length before validation forbidden
- Refusal conditions: do not edit dose math without the HIL rig running; do not touch parse_order without the fuzz harness and a length check
- Risks I cannot yet gate: WCET of the cyclic step unmeasured (no cycle counter on this build); drug-library forward-compat unproven across versions; bus electrical fault injection not modeled
```

## 6. Pack -> gate mapping (this domain)

| Gate | This domain | Forbidden / reshaped |
|---|---|---|
| comprehension | mandatory — model the order -> dose -> motor path before editing | editing dose math you cannot model is forbidden |
| static analysis + ASan/UBSan + parse fuzz | mandatory (untrusted order bytes) | device build runs no host ASan |
| MISRA / forbidden-construct + HIL | mandatory (safety-of-life) | no post-init heap; no blocking on the cyclic step |

## 7. Refusal conditions + risks I cannot yet gate

Refuse to edit when: the trust boundary is unmapped (an order field read with no
`file:line` validation answer); the HIL flow oracle does not exist and cannot be
built in the task, so a dose change cannot be proven correct; or the
forbidden-construct rules are unconfirmed (cannot prove no heap reached the dosing
path). Risks no gate here covers — one bead each: cyclic-step WCET unmeasured (no
cycle counter on this build), drug-library forward-compatibility unproven across
versions, clinical-bus electrical fault injection unmodeled.

## Evidence Packet

```text
# C/C++ Gate Report

## Change Scope
- Issue/task: derive an ad-hoc pack for the infusion-pump dosing controller and harden parse_order's order-length validation
- Touched files: src/order_rx.c
- Public API/ABI touched: no (internal parser; clinical-bus frame format unchanged)
- User-visible rendering/artifacts touched: no
- Parser/input/security boundary touched: yes (clinical-bus order frame at src/order_rx.c:88)
- Threads/locks/atomics/signals touched: no
- Refactor/simplification claim: no
- Performance claim: no

## Commands
| Gate | Status | Command | Evidence |
|---|---|---|---|
| inventory | passed | bash scripts/cpp_inventory.sh . | build: make+arm-none-eabi-gcc; std: c11 -ffreestanding; domain-detect: unknown-domain -> ad-hoc pack |
| compile | passed | make CFLAGS=-Werror | warning-clean: yes |
| tests | passed | make test-host | 96 host tests pass; dose-math vectors green against the reference model |
| comprehension | passed | bash scripts/cpp_comprehension_map.sh src/order_rx.c | entry-point: parse_order src/order_rx.c:88; module-map: order_rx -> dose_engine -> motor/alarm; callgraph: parse_order -> validate_len -> set_rate (src/dose_engine.c:140); intent: turn a clinician order into a bounded, alarm-guarded flow rate |
| static analysis | passed | cppcheck --addon=misra --enable=all src/order_rx.c | findings: 0 after triage; MISRA C:2012 length-before-read rule satisfied at src/order_rx.c:88 |
| ASan+UBSan | passed | clang -fsanitize=address,undefined -DHOST_BUILD test/order_rx_host && ./order_rx_host corpus/ | host-only ASan/UBSan (device build excluded); 0 over-reads; crafted short frames now rejected, not parsed |
| fuzz/corpus | passed | clang -fsanitize=fuzzer,address test/fuzz_order.c && ./a.out -runs=10000000 corpus/ | 10M execs clean under ASan; 3 crash inputs from the length bug frozen as regression seeds |

## Residual Risk
- Missing gates: HIL flow-rate run and on-device WCET of the cyclic step (no rig or cycle counter in this environment).
- Why missing gates are acceptable or follow-up issue: this change is the parser length fix only; dose-math behavior is unchanged and host-differential-green, so the HIL run is gating for the dose-math edit, tracked as follow-up before that edit lands.
- Follow-up issues: run the infusion-pump HIL rig against the recorded order set and measure cyclic-step WCET on the target board before touching dose math.
```

Verify the packet (the pack selected comprehension + the security gates; safety-of-life keeps comprehension strict):

```bash
python3 .opencode/skills/c-cpp-profi/scripts/cpp_evidence_check.py <this-packet>.md \
  --profile comprehension --profile security --require-comprehension-proof
```
