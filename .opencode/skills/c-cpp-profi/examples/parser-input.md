# Example: Parser Or Untrusted Input Change

Use for parsers, decoders, file formats, protocol handlers, compression, serialization, and untrusted byte/string APIs.

## Starting Point

```text
Task: change parser behavior for malformed input.
Boundary: untrusted bytes to internal representation.
Primary risks: bounds, integer overflow, uninitialized reads, recursion/resource limits, differential behavior.
```

## Required Skill Path

1. Read `TESTING-FUZZING.md`, `MEMORY-SAFETY.md`, and `SECURITY-REVIEW.md`.
2. Capture reproducer or corpus seed before editing.
3. Add or update a narrow parser-boundary harness.
4. Run fast regression tests and a sanitizer-backed fuzz/corpus replay:

```bash
./parser_test < minimized-crash
ASAN_OPTIONS=detect_leaks=1 ./fuzz_parser -runs=100000 corpus/
```

5. Promote every crash input into regression or corpus.

## Evidence Packet

```text
Parser evidence:
- Entry point:
- Trusted/untrusted boundary:
- Input size/resource bounds:
- Reproducer/minimized input:
- Harness:
- Corpus changes:
- ASan+UBSan/MSan/Valgrind:
- Differential/reference oracle:
- Residual risk:
```

## Refusal Conditions

- Parser/security crash closed without minimized input.
- Fuzzer run without sanitizer or without corpus/reproducer replay.
- Fix that only handles one crash string but leaves the input class unbounded.
