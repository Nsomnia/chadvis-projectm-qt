# Security Review

## First Questions

- What input is attacker-controlled?
- What memory, file, network, process, or device boundary does the code cross?
- What secrets can be exposed through memory disclosure, logs, crashes, timing, or files?
- What must remain stable at ABI, protocol, or storage boundaries?
- What assumptions are enforced by code rather than comments?

## Standards Lens

Use SEI CERT C and C++ as the default security taxonomy. The rules are necessary but not sufficient: safe design, threat modeling, fuzzing, and hardening still matter.

Also use C++ Core Guidelines profiles:

- `type`: avoid invalid type reinterpretation.
- `bounds`: avoid array/range violations.
- `lifetime`: avoid leaks and invalid object access.

## High-Risk C/C++ Surfaces

- Parsers, decoders, protocol handlers, file formats, compression, serialization.
- Crypto wrappers and random generation.
- FFI, plugins, scripting embeddings, callbacks from foreign runtimes.
- Custom allocators, arenas, intrusive containers, lock-free data structures.
- Privileged helpers, sandbox escapes, path handling, temp files.
- Signal handlers, process spawning, shell command construction.
- Deserialization into object graphs.

## Required Evidence For Security-Sensitive Changes

At minimum:

1. Threat model note for the touched boundary.
2. Static analysis or compiler diagnostic pass.
3. ASan+UBSan test run where supported.
4. Fuzzing or property tests for input-processing code.
5. Regression test for every defect fixed.
6. Hardening flag review for release build impact.

## Hardening Flags To Evaluate

Check support before use:

```text
-D_FORTIFY_SOURCE=3
-fstack-protector-strong
-fPIE -pie
-fvisibility=hidden
-Wl,-z,relro,-z,now
-flto=thin
-fsanitize=cfi
```

CFI needs LTO and visibility planning. Cross-DSO behavior and target support vary.

## Refusal Patterns

Do not silently accept:

- Unchecked copy into fixed buffers.
- Integer arithmetic used for allocation sizes without overflow checks.
- User-controlled format strings.
- Deserialization that can instantiate arbitrary types or execute callbacks.
- Secret material in ordinary logs, core dumps, or exceptions.
- `system()` or shell invocation with interpolated input.
- Ad hoc crypto, custom PRNGs for security, or unauthenticated encryption.
