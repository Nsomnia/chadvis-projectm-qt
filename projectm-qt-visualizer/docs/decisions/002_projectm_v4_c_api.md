# ADR 002: projectM v4 C API
## Status
Accepted
## Context
projectM has two major versions available on Arch:
- `projectM` package: v3, C++ API
- `libprojectm` package: v4, C API
## Decision
Use libprojectm (v4) with the C API.
## Rationale
- v4 is actively maintained
- C API is more stable across versions
- Cleaner separation from our C++ code via wrapper
- Better documentation and wiki support
## Consequences
- Positive: Stable API, active development
- Negative: Need wrapper class for C++ convenience
- Implementation: ProjectMWrapper class created
