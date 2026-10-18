# Documentation

## Purpose

Use this file to author C/C++ documentation as a first-class artifact, not as an afterthought. This skill already treats documentation as a test gate (docs-as-tests): the canon's elite-project study records that curl checks manpage syntax, feature-name sync, and `CURL_VERSION_*` consistency in its test suite. This file adds the authoring methodology that produces the documents those gates then check.

Each procedure below produces one concrete artifact with a fixed required shape. Documentation is not "done" because prose exists; it is done when the README usage compiles, every public symbol carries its contract, the changelog reflects the release including ABI status, and no slop tokens remain. See the Completion Standard.

Do not write generic documentation boilerplate. A C/C++ architecture or API doc reuses this skill's ownership/ABI vocabulary (RAII, owner/release, SONAME, symbol visibility, `extern "C"` linkage), as required by [REFACTOR-ISOMORPHISM.md](REFACTOR-ISOMORPHISM.md) and [BUILD-PORTABILITY.md](BUILD-PORTABILITY.md). Before publishing any documentation surface, run the slop pass below and cross-link the sibling skills named in [INNOVATION-ENGINE.md](INNOVATION-ENGINE.md) "Documentation Of Ideas".

## Authoring Order

Author from facts, not aspiration. The build and ABI facts are inputs to the prose, never invented for it.

1. Inventory first: run `bash .opencode/skills/c-cpp-profi/scripts/cpp_inventory.sh .` to recover the real build system, standards, compilers, public headers, and test runners. The README build section copies these; it does not guess them.
2. Recover the public surface from the headers and the symbol table (`nm -D --defined-only`), not from intent. Every exported symbol must end up documented.
3. Recover history from tags and commit ranges before writing the changelog.
4. Write the artifact, then run its docs-as-test (compile the snippet, build Doxygen, run the sync check), then run the slop pass.

| Artifact | Required shape | Docs-as-test |
|---|---|---|
| README | what+who, build-from-source, install, compiling quickstart, capability table, versioning/ABI pointer, license, contributing | usage snippet compiles and runs |
| Architecture doc | module map, core data/control flow, ownership/lifetime, threading, error model, ABI/versioning | reviewer can trace the core path |
| API docs | per-symbol ownership, lifetime, thread-safety, error contract, preconditions, units | Doxygen warning-clean; header self-contained |
| Changelog | Keep-a-Changelog + SemVer; ABI status per release | entries match real tags/commit ranges |
| Docs site | Diátaxis tutorial/how-to/reference/explanation | site build succeeds; links resolve |

## README (library or tool)

A C/C++ README must let a reader build the project from source and run a working snippet inside 60 seconds of reading. Required sections, in order:

1. One-line what and who: the problem it solves and the intended consumer (application author, library integrator, plugin writer).
2. Build from source: the actual build system (CMake/Meson/Make/Autotools), exact configure/build/test commands, runtime and build dependencies with minimum versions, and the supported toolchain and standard matrix (compilers and C/C++ standard, for example C23 / C++23 with documented exceptions). Do not invent commands; copy them from the build files.
3. Install: package-manager paths, `cmake --install` / `meson install` prefix, pkg-config or CMake package-config consumption, and the produced artifacts (headers, static/shared library, `SONAME`).
4. Minimal usage / quickstart that compiles: the smallest program that links against the library and produces visible output, with the exact compile/link line. This snippet is itself a docs-as-test and must compile and run.
5. Feature / capability table: one row per capability with status (stable, experimental, deprecated) and the entry point.
6. Versioning / ABI policy pointer: link to the architecture doc's ABI section and the changelog; state SemVer and the ABI-break = MAJOR + SONAME-bump rule.
7. License and Contributing.

Anti-patterns: installation-first ordering that buries the value; a usage snippet that does not compile; a build section that lists a build system the repo does not use; claiming portability without naming the tested compiler and platform matrix.

README capability-table shape:

```markdown
| Capability | Status | Entry point | Notes |
|---|---|---|---|
| Streaming decode | stable | foo_decode_stream() | thread-safe per handle |
| SIMD backend | experimental | FOO_ENABLE_SIMD | scalar fallback always built |
```

## Architecture / Design Doc

The architecture doc explains the core path well enough that a maintainer could reason about a change before reading the code. Required sections, each phrased in this skill's vocabulary, not generic prose:

- Module map: translation units / components and their dependency direction; which symbols are public (exported, default visibility) versus internal (`-fvisibility=hidden`, anonymous namespace, `static`).
- Data and control flow of the core path: the request/parse/render/IO loop, entry point to exit, with the buffers and ownership that cross each boundary.
- Ownership and lifetime model: who allocates, who frees, owner/release contracts in C, RAII handles and Rule of 0/3/5 in C++, non-owning views (`span`/`string_view`) and their validity windows, custom deleters, allocator family.
- Threading / concurrency model: which functions are thread-safe, which require external synchronization, lock-order graph for nested locks, atomic memory-order contracts, reentrancy and signal-safety boundaries. Defer detail to [CONCURRENCY-DEADLOCKS.md](CONCURRENCY-DEADLOCKS.md).
- Error model: return-code versus `errno` versus exceptions, the FFI exception boundary (no exception may cross `extern "C"`), partial-initialization and cleanup-idempotence rules.
- ABI / versioning policy: C ABI versus C++ ABI boundary, symbol visibility and version scripts, `SONAME` policy, what is layout-frozen, what may move. Reuse [BUILD-PORTABILITY.md](BUILD-PORTABILITY.md) "ABI/API Checklist" and "C++ ABI Rules" rather than restating them loosely.

Each section states what is proven and what is assumed. An ownership claim with no owner/release or RAII evidence is a wish, not a model. A thread-safety claim with no lock-order graph or atomic-order contract is unverified. When the architecture doc and the code disagree, the doc is the defect: fix it or file the gap in the handoff.

## API Docs (Doxygen / header contracts)

Every public symbol (exported function, type, macro, global) documents its full contract. A header is not contract-complete until each public symbol states:

- Ownership: who frees returned pointers and out-parameters, and whether the callee takes ownership of inputs (owner/release).
- Lifetime / validity: how long a returned pointer, view, or handle stays valid, and what invalidates it.
- Thread-safety: safe for concurrent calls, safe only on distinct objects, or requires external locking.
- Error / return contract: success and failure values, `errno`/error-code semantics, and whether it can throw across the boundary.
- Preconditions: non-null, alignment, buffer length, valid-state, and ordering requirements.
- Units where relevant: bytes versus elements, seconds versus milliseconds, base of numeric inputs.

Doxygen comment shape for a public C function:

```c
/**
 * Decode @p len bytes of @p src into a freshly allocated buffer.
 *
 * @param src  Source bytes; must not be NULL; read-only; not retained.
 * @param len  Length of @p src in bytes (not elements).
 * @param[out] out_len  Receives the decoded length in bytes; must not be NULL.
 * @return Newly allocated buffer the caller must free with foo_free(),
 *         or NULL on allocation failure or malformed input (errno set).
 * @retval NULL  errno == EINVAL for malformed input, ENOMEM on allocation failure.
 * @threadsafety Safe to call concurrently; touches no shared state.
 * @pre  len <= FOO_MAX_INPUT.
 */
unsigned char *foo_decode(const unsigned char *src, size_t len, size_t *out_len);
```

Contract-complete header checklist: every public symbol has ownership, lifetime, thread-safety, error contract, preconditions, and units; no public declaration is documentation-free; macros document side effects and evaluation count; the header is self-contained and `extern "C"`-guarded where it is consumed from both languages.

For C++ public APIs, additionally document: exception guarantee per function (no-throw / strong / basic), which destructors and `noexcept` paths are relied on, value-category cost (does it move or copy), iterator/view invalidation, and any STL type or allocator that crosses the library boundary. State whether the function may be called across an ABI boundary at all.

## Documentation Risk Map

Documentation defects map onto the same failure classes the code gates already chase. The most damaging are the silent contract lies:

| Doc defect | Consequence | Catch with |
|---|---|---|
| Ownership unstated or wrong | caller double-frees or leaks | per-symbol ownership contract; cross-check against the cleanup path |
| Thread-safety overclaimed | data race in consumer code | thread-safety contract backed by the lock/atomic model |
| Error contract incomplete | unchecked failure path | `@return`/`@retval` for every exit, including `errno` |
| Stale ABI/version note | consumer links an incompatible `SONAME` | changelog ABI status synced to the ABI gate |
| Uncompiled usage snippet | quickstart misleads on day one | docs-as-test: compile and run it |
| Drifted option/feature names | docs name flags the binary does not accept | sync check against source (the curl `CURL_VERSION_*` pattern) |

## Changelog

Use Keep-a-Changelog format with SemVer. Reconstruct from real tags and commit ranges (`git tag --sort=-v:refname`, `git log <prev>..<tag>`), not from memory. An ABI break is a MAJOR version bump and a `SONAME` bump; record the ABI status of every release (compatible, source-compatible only, or breaking) with the evidence from the ABI gate in [BUILD-PORTABILITY.md](BUILD-PORTABILITY.md).

```markdown
## [2.0.0] - 2026-05-29
### Changed
- BREAKING ABI: foo_handle layout changed; SONAME 1 -> 2. Recompile required.
### Added
- foo_decode_stream() for incremental input.
### Fixed
- Use-after-free in foo_reset() on the error path (regression test added).
```

Cross-link the `changelog-md-workmanship` sibling skill for full release-history reconstruction from tags, releases, and issue trackers.

## Docs Site

Pick one pipeline and document its build/deploy command:

- Doxygen to Breathe/Sphinx for API-heavy C/C++ libraries (Doxygen XML output feeds Breathe; Sphinx renders).
- mdBook for narrative-heavy tools.

Structure the content with Diátaxis: tutorial (learning-oriented, the quickstart end to end), how-to (task-oriented recipes), reference (the generated API docs and the contract-complete headers), and explanation (the architecture doc). Do not mix the four modes in one page. Cross-link the `documentation-website-for-software-project` sibling skill for the site build and deploy.

## Slop-Free Prose

Before publishing any documentation surface, run the `de-slopify` sibling skill over the prose. Ban these tokens and patterns: "seamless", "robust", "leverage", "in today's world", "comprehensive solution", hollow superlatives, and marketing em-dashes used for dramatic pause. Prefer concrete nouns and measured claims: name the compiler, the version, the benchmark number, the platform. A claim without an anchor is slop. "Blazingly fast" is slop; "1.8x faster on the simdjson twitter.json benchmark, GCC 14, -O2, recorded p50" is documentation.

## Docs-As-Tests Rule

Documentation that can be mechanically checked must be mechanically checked. The README usage snippet must compile and run in the build matrix. Public option names, feature flags, and version metadata must be verified against the source (curl checks `CURL_VERSION_*` sync across docs/header/source). The Doxygen build must be warning-clean for documented symbols. A docs change that does not run through a check is unverified, the same way a passed compile gate without `--require-warning-clean` only means an artifact was produced.

## Completion Standard

Documentation is not done until:

- The README usage snippet compiles and runs against the current build.
- Every public symbol has at minimum an ownership, thread-safety, and error contract.
- The changelog reflects the release, including its ABI status (compatible / source-only / breaking + SONAME).
- No slop tokens remain (slop pass clean).

Record the documentation evidence in the handoff: which snippet was compiled and with which toolchain, the contract-complete header check result, the changelog ABI entry, and the slop-pass result. See the [Handoff Contract](../SKILL.md).
