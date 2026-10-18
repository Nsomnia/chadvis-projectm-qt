# Repo Comprehension

## Purpose

This is the comprehension layer for `c-cpp-profi`. Before any non-trivial edit you must build a working mental model of the C/C++ you are about to touch and prove it. The rest of the skill verifies that a change is *correct*; this reference verifies that you understood the code *before* changing it. Comprehension is a gate, not a vibe: an agent may not claim to understand code it has not modeled.

Read this when the task is "understand / onboard", when you land in an unfamiliar repo, or before Step 0 of [INNOVATION-ENGINE.md](INNOVATION-ENGINE.md). The four layers below are a ladder: do not skip a rung, and do not edit above a rung you have not climbed.

## Comprehension is falsifiable

Every comprehension claim cites a symbol or `file:line`, exactly as a performance claim cites a benchmark and an audit finding cites evidence. "I read it" is not comprehension. "`parse_header` at `src/proto.c:212` consumes `buf,len` and trusts the 2-byte length field at offset 4 without bounding it against `len`" is comprehension.

- A claim with no anchor is a guess. Replace it with a `file:line`/symbol or mark it `unknown — blocks editing`.
- A layer is complete only when its falsifiable artifact exists and every field in it is anchored.
- The artifact is reproducible: another agent re-running the same commands lands on the same entry points, the same module map, the same callgraph edges.

## No editing code you cannot model (stop condition)

Stop and do not edit when any holds. File a bead, ask the maintainer the [DOMAIN-AGNOSTIC-MASTERY.md](DOMAIN-AGNOSTIC-MASTERY.md) Section-1 questions, or build the missing artifact first.

- You cannot name the binary/library your change is compiled into, or its per-TU build flags.
- You cannot name the entry points that reach the line you intend to change.
- You cannot draw the callgraph of the touched path, or you cannot state who owns the memory it moves.
- You cannot state the invariant the code assumes at the edit site, so you cannot tell whether your change preserves or breaks it.
- You cannot state, in one sentence, what the code is *for* — and no doc, test, or signal lets you reconstruct it.

A green generic gate over code you did not model is not evidence. Model first.

## L1 Build graph & ground

Pin down where the code actually runs and what compiles it. Guessing the build is the most expensive early mistake in C/C++: the wrong TU, the wrong flags, or a vendored copy makes every later layer wrong.

What to pin down:

- Target triple(s): host vs cross, word size, endianness, OS/bare-metal. (`cc -dumpmachine`, `--target`, the linker script for bare-metal.)
- Toolchain + exact versions: compiler, std level (`-std=`), linker, libc/libc++ vs libstdc++.
- Build system: CMake / Meson / Make / Bazel / configure; presets; debug vs release; LTO; sanitizer builds.
- Per-TU flags: the *real* command line for the file you will edit, not the project's prose.
- Generated files, link order, visibility, what is **built from source vs vendored/third-party** (do not "fix" a vendored copy).

Commands/tools:

```bash
cc --version; cc -dumpmachine
cmake --version; meson --version; make --version
# Get a real per-TU flag database:
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON   # or: bear -- make
jq '.[] | select(.file|endswith("proto.c")) | .command' build/compile_commands.json
ldd ./build/app            # runtime link order; otool -L on macOS
nm -D --defined-only ./build/libfoo.so | head   # what this lib actually exports
git ls-files | grep -Ei 'third_party|vendor|external|deps'   # vendored vs built
```

Fast path: `bash .opencode/skills/c-cpp-profi/scripts/cpp_comprehension_map.sh <repo>` produces the L1 build-graph map automatically — detected build system(s), `compile_commands.json` presence, a language breakdown, and the `-std=`/`CMAKE_CXX_STANDARD`/`c_std`/`cpp_std` hints found in build files, each line anchored to a repo-relative `file:line` or path. The commands above remain the manual fallback when you need the *real* per-TU command line (only `compile_commands.json` has that).

Cross-link: this is the Universal Core **Build graph**, **Toolchain**, and **ABI** rows in [DOMAIN-AGNOSTIC-MASTERY.md](DOMAIN-AGNOSTIC-MASTERY.md); for ABI/visibility/link detail read [BUILD-PORTABILITY.md](BUILD-PORTABILITY.md).

Artifact (cite anchors): the build graph plus a one-line **"what binary/lib am I changing"** statement — target triple, build system + preset, the exact compile command for the touched TU (from `compile_commands.json`), and whether the file is first-party or vendored.

## L2 Structure

Map how the code is shaped before you trace how it runs. Without `compile_commands.json` clangd and most index tools are blind, so L1 precedes L2.

What to pin down:

- Entry points: `main`, exported library symbols, `LLVMFuzzerTestOneInput`, plugin/registration hooks, `__attribute__((constructor))`, test mains.
- Module map: the directories/translation units and the 3-5 core types everything revolves around.
- Touched-path callgraph: callers and callees of the function(s) you will change, across headers and TUs — not the whole repo, just the path you touch.

Commands/tools:

```bash
rg -n 'int +main *\(|LLVMFuzzerTestOneInput|__attribute__\(\(constructor' .
nm -D --defined-only ./build/libfoo.so          # exported API surface
objdump -T ./build/libfoo.so | rg ' g +DF '     # global defined functions
ctags -R --c-kinds=+p --fields=+iaS .           # symbol index for editors
cscope -bR                                       # callers/callees, or clangd callHierarchy
rg -n '\bparse_header\s*\(' .                    # callsite census for the touched symbol
```

Fast path: the same `bash .opencode/skills/c-cpp-profi/scripts/cpp_comprehension_map.sh <repo>` emits the L2 **exported-API list** (non-static function declarations in public headers — the real entry points of a library, e.g. inih `ini_parse`, logc `log_*`, each at `file:line`), the L2 **entry-point list** (`main(`, `LLVMFuzzerTestOneInput`, exported-symbol hints, and public headers, each at a repo-relative `file:line` or path; a `main()` behind a `#if <NAME>_MAIN` self-test guard is labeled a *conditional test driver*, and doc-comment `main()` in headers is not counted), and a coarse **module map** (top-level source dirs with per-dir file counts). The exported-API extractor handles the macro/paren-wrapped declaration idioms serious C libraries use — a paren-wrapped name (`LUA_API int (lua_absindex)(...)` → `lua_absindex`), a leading export-macro prefix (`UV_EXTERN`/`*_API`/`*_EXPORT`/`*_PUBLIC`), a macro-wrapped return type (`CJSON_PUBLIC(cJSON *) cJSON_Parse(...)`), and a multi-line decl with a trailing macro suffix (`BaseType_t xTaskCreate(...) PRIVILEGED_FUNCTION;`) — while excluding non-functions (`MBEDTLS_PRIVATE(field)` struct-field markers, reserved/asm `__…__` tokens, ALL-CAPS macro-shaped names); C++ `static` member functions in public headers (e.g. `DB::Open`) are kept. The long symbol-hint / exported-API lists are deduped and capped with a `... (+N more; capped)` footer; the exported-API cap ranks likely-public decls first (those under top-level `include/**` or carrying an export macro) and round-robins across source files so no single header monopolizes the cap and internal/per-arch symbols never bury the public surface. Caveat: it is a heuristic, not a C front-end — within a large API a specific late-alphabet headline symbol can still fall past the cap (read the full list, or `nm -D`, when you need an exact name). The same probe now also emits an L3 **touched-path callgraph**: from a few seed entry points (`main`, `LLVMFuzzerTestOneInput`, and leading exported-API symbols) it draws `caller -> callee` edges restricted to functions *defined in this repo*, two levels deep, deduped and capped — verified to recover the real parse path (cJSON `cJSON_Parse -> cJSON_ParseWithOpts`; inih `ini_parse -> ini_parse_file`, and C++ method dispatch `INIReader::GetBoolean -> Get -> MakeKey`). It is a token-scan heuristic, not a compiler callgraph: self-recursion IS surfaced (a self-calling function is shown as `name -> name (recursive)`, e.g. cJSON's `cJSON_Delete`), but function-pointer/vtable calls (e.g. cJSON's `global_hooks.deallocate`), overloaded names, and macro-generated calls are still missed, and a function whose only callee is one of those is honestly printed as `(no in-repo callees)`.

For an **exact** graph, pass `--exact`: `cpp_comprehension_map.sh --exact <repo>` lowers each TU that compiles standalone to LLVM IR (`clang -O0 -g -emit-llvm -S`, best-effort include flags) and reads `call`/`invoke @symbol` edges straight from the IR, so there are no token false positives and the edges are the compiler's own (verified on cJSON: `cJSON_Parse -> cJSON_ParseWithOpts`, `cJSON_Minify -> minify_string, skip_*_comment`; and on C++ tinyxml2 with `c++filt` demangling). It is off by default because it is the one mode that compiles code; it degrades to a one-line note when clang is absent or no TU compiles standalone (then the heuristic graph stands), and a repo that needs a real build yields fewer compiled TUs (point it at one with `compile_commands.json` for full coverage). Even the exact graph omits **indirect** calls (function pointers, C++ virtual dispatch): those carry no symbol in the IR and are not resolvable by any purely static analysis — for those, run the code under a dynamic call tracer (`perf`, `callgrind --dump-instr`, or a ubsan/asan-instrumented run). `cscope -bR` / clangd `callHierarchy` remain alternative exact tools when installed.

Artifact (cite anchors): an **entry-point list** (`file:line` each) + a **module map** (TU/dir -> responsibility, with the core types) + a **callgraph of the touched path** (`caller -> target -> callee`, each edge anchored). This is the falsifiable form of "I know the shape."

## L3 Flow

Trace data and control through the *touched* path only. This is where ownership, error paths, and the invariants the code silently assumes live — the things a sanitizer cannot tell you you broke until it is too late.

What to pin down:

- Data flow: where the bytes/values come from, how they transform, where they leave. Trust boundary (who supplies untrusted input).
- Control flow + state machine: legal transitions, early returns, the error/cleanup paths, partial-initialization cleanup.
- Ownership/lifetime: allocation source, transfer, who frees, RAII handle, non-owning view lifetime, allocator family.
- The invariants the code assumes: bounds it relies on, null/termination assumptions, ordering, locks held, atomic order.

Commands/tools:

```bash
rg -n 'return |goto |free\(|delete |throw |errno' src/proto.c   # error/cleanup/ownership edges
rg -n 'malloc|calloc|new |unique_ptr|shared_ptr|span|string_view' src/proto.c
# read the function end-to-end; clangd "go to definition" across the edges from L2
```

Cross-link: the Universal Core **Memory/object model**, **Ownership**, and **UB surface** rows in [DOMAIN-AGNOSTIC-MASTERY.md](DOMAIN-AGNOSTIC-MASTERY.md); deepen with [MEMORY-SAFETY.md](MEMORY-SAFETY.md) and, for shared state, [CONCURRENCY-DEADLOCKS.md](CONCURRENCY-DEADLOCKS.md).

Artifact (cite anchors): a **flow note** for the touched path citing `file:line` — the data path, the error/cleanup path, the ownership transfer, and the named invariant the edit must preserve (e.g. "`dst` is caller-owned and at least `cap` bytes; `proto.c:230` assumes it").

## L4 Domain intent

Reconstruct WHAT the code is for and WHY when docs are absent. This is not optional flavor: a correct change to the wrong intent is still wrong. Run a fixed inference procedure rather than guessing.

Inference procedure (harvest signals, then commit to a statement):

1. **Names**: directory/file/type/function names encode intent (`crypto/`, `vfs`, `parse_`, `_unsafe`, `htons`). Record the signal, not the hunch.
2. **Tests**: the test names and assertions are the executable spec of intended behavior — read them before the prose.
3. **Build/deps**: linked libraries and feature flags say what world the code lives in (`-lcrypto`, `cuda`, `FreeRTOS`, packed structs).
4. **Commit history**: `git log -p`/`git blame` on the touched lines reveals why the shape is what it is and what bugs it already absorbed.
5. **Public surface**: the exported API and its docs/manpages say who the consumer is and what contract they were promised.

```bash
rg -n 'parse_|encode_|decode_|_unsafe|htons|ntohl|EVP_|__global__' src/   # name signals
ls test*/ tests/; rg -n 'TEST\(|ASSERT|EXPECT|assert\(' tests/            # the executable spec
git log --oneline -n 20 -- src/proto.c; git blame -L 200,240 src/proto.c  # why it is shaped so
```

Cross-link: feed these signals into the **Pack-Selection Procedure** of [DOMAIN-AGNOSTIC-MASTERY.md](DOMAIN-AGNOSTIC-MASTERY.md) and load (or, for an unbriefed domain, build from the template) the matching domain pack. The selected pack's oracle and forbidden-construct list constrain every later gate.

Artifact (cite anchors): a **one-paragraph intent statement** ("this TU parses untrusted X-protocol frames off the socket for the library's public `x_read()`; consumers are downstream apps; getting it wrong is a heap overflow on attacker bytes") + the **selected domain pack** and the file:line signals that selected it.

## Comprehension order

L1 -> L2 -> L3 -> L4 is the default. L1 first because the build database powers L2's index tools; L2 before L3 because you cannot trace a path you cannot locate; L4 can be sketched early from names but is only *confirmed* once L2/L3 reveal the real flow. Climb the whole ladder for the touched path before you edit; do not climb it for the whole repo.

## Evidence

Fill these fields in the gate report's comprehension row, each anchored to a symbol or `file:line`. The `comprehension` profile in `cpp_evidence_check.py` (run with `--require-comprehension-proof`) fails a `passed` row whose evidence is missing any of them.

```text
- entry-point:            main / exported symbol / LLVMFuzzerTestOneInput reaching the touched path (file:line)
- module-map:             TUs/dirs touched + the core types, each anchored
- callgraph:              touched-path callgraph (caller -> target -> callee); `touched-path-callgraph:` is accepted as a synonym
- intent:                 one-sentence reconstructed purpose + selected DOMAIN-AGNOSTIC-MASTERY pack and the signals that selected it
```

Gate row to fill (see `cpp_gate_report.sh`):

```text
| comprehension | passed | <commands run> | entry-point: ...; module-map: ...; callgraph: ...; intent: ... |
```

Validate:

```bash
python3 .opencode/skills/c-cpp-profi/scripts/cpp_evidence_check.py <report.md> --profile comprehension --require-comprehension-proof
```
