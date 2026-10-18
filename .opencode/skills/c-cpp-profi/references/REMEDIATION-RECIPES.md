# Remediation Recipes

## Principle

The taxonomies in [MEMORY-SAFETY.md](MEMORY-SAFETY.md), [SECURITY-REVIEW.md](SECURITY-REVIEW.md), [PERFORMANCE.md](PERFORMANCE.md), and [CONCURRENCY-DEADLOCKS.md](CONCURRENCY-DEADLOCKS.md) tell you which defect classes to look for. This file is the layer beneath them: the corrected code. Each card is a copy-ready rewrite, not a hint.

Every "After" snippet is behavior-preserving except for the removed defect. If the rewrite also changes ordering, errors, ABI, ownership, or numerics, that is a separate change and must be proven under [REFACTOR-ISOMORPHISM.md](REFACTOR-ISOMORPHISM.md). A fix is not done until the named gate from [QUALITY-GATES.md](QUALITY-GATES.md) passes on the touched path and the matching `cpp_evidence_check.py` profile validates the packet. A green sanitizer is one input, not proof; re-read the path with fresh eyes per the multi-pass loop.

Each card reads as: **Bug class** (taxonomy entry) → **Before** (unsafe pattern) → **After** (minimal correct rewrite) → **Invariant restored** → **Proving gate** ([QUALITY-GATES.md](QUALITY-GATES.md) gate + `cpp_evidence_check.py --profile`) → **Precedent** (elite repo from [C-CPP-EXPERT-CANON.md](C-CPP-EXPERT-CANON.md)).

# Part A — Fix Recipes

## Recipe 1 — Unchecked allocation-size multiply

Bug class: integer/string — allocation-size overflow ([SECURITY-REVIEW.md](SECURITY-REVIEW.md) "Integer arithmetic used for allocation sizes without overflow checks").

```c
/* Before: n * sz wraps; tiny allocation, then heap overflow on write. */
void *buf = malloc(n * sz);
```

```c
/* After: overflow-checked allocation; fail closed before the multiply lands. */
size_t bytes;
if (__builtin_mul_overflow(n, sz, &bytes)) { errno = ENOMEM; return NULL; }
void *buf = malloc(bytes);
/* Portable fallback when no builtin: if (n && sz > SIZE_MAX / n) fail; */
```

Invariant restored: the byte count handed to the allocator equals the mathematical product, or the path fails before allocating. No truncated size feeds a later write.
Proving gate: Integer/bounds gate — UBSan (`-fsanitize=unsigned-integer-overflow` plus `signed-integer-overflow`) on the multiply, plus a fuzz/regression seed at `SIZE_MAX`-class inputs. Validate `--profile security` (and `--profile parser` if the count is attacker-supplied). Add a regression test for the overflow input.
Precedent: SQLite size arithmetic and mimalloc's allocation paths guard size math; curl `docs/CODE_REVIEW.md` lists integer overflow before size multiplication as a recurring hot-path failure mode.

## Recipe 2 — Unbounded copy into a fixed buffer

Bug class: integer/string — unbounded string APIs without truncation/error semantics ([MEMORY-SAFETY.md](MEMORY-SAFETY.md) C Defaults; [SECURITY-REVIEW.md](SECURITY-REVIEW.md) "Unchecked copy into fixed buffers").

```c
/* Before: no bound; strcpy/strcat/sprintf overrun dst on long src. */
char dst[64];
strcpy(dst, src);
sprintf(dst, "%s/%s", dir, name);
```

```c
/* After: bounded copy with explicit truncation detection. */
char dst[64];
int n = snprintf(dst, sizeof dst, "%s/%s", dir, name);
if (n < 0 || (size_t)n >= sizeof dst) { /* truncated: reject, do not use dst */ return -1; }
/* strlcpy(dst, src, sizeof dst) where available; check return >= sizeof dst for truncation. */
```

Invariant restored: writes never exceed `sizeof dst`, `dst` stays NUL-terminated, and truncation is a detected, handled outcome rather than silent corruption.
Proving gate: Memory/lifetime gate — ASan+UBSan over the copy path, `-D_FORTIFY_SOURCE=3` build to catch fortifiable calls, plus a regression test with an over-length input asserting the rejection branch. Validate `--profile memory`.
Precedent: curl prefers bounded operations and treats zero-termination assumptions as review items in `docs/CODE_REVIEW.md`; SQLite uses bounded `sqlite3_snprintf`-style helpers throughout.

## Recipe 3 — Raw owning allocation across a throwing/early-return scope

Bug class: memory/lifetime — leak/use-after-free on the error path ([MEMORY-SAFETY.md](MEMORY-SAFETY.md) RAII; C-CPP-EXPERT-CANON RAII row).

```cpp
// Before (C++): any throw or early return between new and delete leaks.
Widget *w = new Widget(cfg);
if (!w->ready()) return false;   // leak
process(*w);                     // process() may throw -> leak
delete w;
```

```cpp
// After (C++): RAII owns the lifetime on every exit, including exceptions.
auto w = std::make_unique<Widget>(cfg);
if (!w->ready()) return false;   // freed by unique_ptr dtor
process(*w);                     // freed even if process() throws
```

```c
/* After (C): single cleanup path via goto; one owner, one release. */
int rc = -1;
Widget *w = widget_create(cfg);
if (!w) goto out;
if (!widget_ready(w)) goto cleanup;
rc = widget_process(w);
cleanup: widget_destroy(w);
out:     return rc;
```

Invariant restored: every control-flow exit (normal return, early return, thrown exception) releases the resource exactly once; no path leaks or double-frees.
Proving gate: Memory/lifetime gate — ASan+LSan to confirm no leak on the error path, plus a test that drives the early-return and throwing branches. Validate `--profile memory`.
Precedent: mimalloc and SQLite use disciplined single-cleanup ownership in C; the canon forbids raw owning `new`/`delete` in C++.

## Recipe 4 — False sharing on a hot shared counter

Bug class: concurrency/performance — false sharing ([CONCURRENCY-DEADLOCKS.md](CONCURRENCY-DEADLOCKS.md) data-race table; [PERFORMANCE.md](PERFORMANCE.md) Threads row).

```cpp
// Before: two hot counters share one cache line; cores ping-pong the line.
struct Stats { std::atomic<uint64_t> hits; std::atomic<uint64_t> misses; };
```

```cpp
// After: pad each hot counter to its own destructive-interference unit.
struct Stats {
    alignas(std::hardware_destructive_interference_size) std::atomic<uint64_t> hits;
    alignas(std::hardware_destructive_interference_size) std::atomic<uint64_t> misses;
};
// If the constant is unavailable, use a documented 64-byte fallback.
```

Invariant restored: independently mutated atomics no longer share a cache line, so a write by one thread does not invalidate another thread's line. Observable values and memory order are unchanged.
Proving gate: Performance gate — baseline with `perf stat -d` (cache misses, HITM) before, single padding lever, remeasure with the same workload; `perf c2c` or TSan to confirm no new race. This changes struct layout, so record ABI impact. Validate `--profile performance --require-performance-proof`.
Precedent: PERFORMANCE.md's Threads row calls out false sharing and tail latency; mimalloc aligns per-thread heap state to avoid cross-thread contention.

## Recipe 5 — Non-exception-safe mutator (basic vs strong guarantee)

Bug class: exception safety — mutation observed half-applied after a throw ([REFACTOR-ISOMORPHISM.md](REFACTOR-ISOMORPHISM.md) exception-safety axis; C-CPP-EXPERT-CANON exception-safety row).

```cpp
// Before: throw mid-update leaves the object partially mutated (no guarantee).
void Set::replace(std::vector<Key> ks) {
    keys_.clear();                 // state already gone
    for (auto &k : ks) index_.insert(k);   // may throw -> torn state
    keys_ = std::move(ks);
}
```

```cpp
// After: copy-and-swap gives the strong guarantee — commit only on success.
void Set::replace(std::vector<Key> ks) {
    Index next;                    // build the new state off to the side
    for (auto &k : ks) next.insert(k);     // a throw here touches nothing live
    index_.swap(next);             // noexcept commit point
    keys_ = std::move(ks);
}
```

Invariant restored: if the mutator throws, the object is unchanged (strong guarantee); the commit (`swap`) is `noexcept`. Where strong is too costly, a scope-guard rollback restores the basic guarantee with no leaks and a valid invariant.
Proving gate: Refactor isomorphism gate — exception-injection test (throwing comparator/allocator) asserting the object equals its pre-call state, plus ASan to confirm no leak on the throwing path. Validate `--profile refactor` (or `--profile memory` for the leak check).
Precedent: REFACTOR-ISOMORPHISM.md treats the no-throw/basic/strong guarantee as an isomorphism axis; the swap commit point mirrors standard-library strong-guarantee idioms.

## Recipe 6 — Narrowing / signed-overflow / implicit conversion at a boundary

Bug class: integer/string — width/sign conversion at a trust boundary ([SECURITY-REVIEW.md](SECURITY-REVIEW.md); C-CPP-EXPERT-CANON Integers row).

```cpp
// Before: silent narrowing; negative or >INT_MAX len becomes a bogus int/index.
int len = recv_length();          // 64-bit, attacker-influenced
char *p = buf + len;              // signed overflow / wrap is UB
```

```cpp
// After: explicit guarded conversion; reject out-of-range before use.
auto narrow = [](std::size_t v) -> int {
    if (v > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        throw std::out_of_range("narrow");   // gsl::narrow has the same contract
    return static_cast<int>(v);
};
int len = narrow(recv_length());
```

Invariant restored: a value crossing the boundary is range-checked, so no silent truncation or signed-overflow UB reaches indexing or pointer arithmetic. `-fwrapv` only defines the wrap; it does not make the wrapped value correct, so it is not a fix.
Proving gate: Integer/bounds gate — `-Wconversion -Wsign-conversion` warning-clean on the file, UBSan `signed-integer-overflow`, and a regression test at `INT_MAX+1` and negative inputs. Validate `--profile security` with `--require-warning-clean`.
Precedent: curl `docs/CODE_REVIEW.md` flags integer overflow and signedness/narrowing as recurring native hot-path defects; the C++ Core Guidelines `bounds` profile underlies `gsl::narrow`.

## Recipe 7 — Use-after-move / dangling return of local / use-after-free on error path

Bug class: memory/lifetime — moved-from use, dangling reference, error-path UAF ([MEMORY-SAFETY.md](MEMORY-SAFETY.md) UB review; C-CPP-EXPERT-CANON Value categories / Object lifetime rows).

```cpp
// Before: read after move; return a view into a local that dies at return.
std::string s = build();
sink(std::move(s));
log(s.size());                    // use-after-move: s is valid-but-unspecified
std::string_view name() { std::string t = compute(); return t; } // dangling
```

```cpp
// After: do not read moved-from state; return an owning value, not a view.
std::string s = build();
auto n = s.size();                // observe before the move
sink(std::move(s));
log(n);
std::string name() { return compute(); }   // caller owns; no dangling view
```

Invariant restored: no read of a moved-from object's value is relied upon, and no returned reference/view outlives its storage. Lifetimes are owned by the value, not aliased.
Proving gate: Memory/lifetime gate — ASan (heap-use-after-free, stack-use-after-return with `detect_stack_use_after_return=1`), `clang-tidy bugprone-use-after-move`, and Clang lifetime-safety warnings. Validate `--profile memory`. Add the minimized reproducer as a regression test.
Precedent: simdjson ships compile-fail tests for dangling parser/`string_view` misuse (canon "Unsafe API states should fail at compile time"); MEMORY-SAFETY.md treats `string_view`/`span` invalidation as a lifetime hazard.

## Recipe 8 — Double-free / use-after-free on cleanup, and TOCTOU

Bug class: memory/lifetime — double free ([MEMORY-SAFETY.md](MEMORY-SAFETY.md)); and concurrency/security — TOCTOU ([CONCURRENCY-DEADLOCKS.md](CONCURRENCY-DEADLOCKS.md) deadlock-class table).

```c
/* Before: free path can run twice; check-then-use races a mutation/unlink. */
free(p);                          /* later error path frees p again */
if (access(path, W_OK) == 0) { fd = open(path, O_WRONLY); }   /* TOCTOU */
```

```c
/* After: null after free (one-owner discipline); open-then-check, no recheck window. */
free(p); p = NULL;                /* free(NULL) is a no-op; idempotent cleanup */
fd = open(path, O_WRONLY | O_NOFOLLOW);                       /* act on the handle */
if (fd >= 0 && fstat(fd, &st) == 0 && /* validate via the open fd, not the name */ 1) { /* use fd */ }
```

Invariant restored: each allocation is freed exactly once (the owner clears the pointer; idempotent cleanup is safe). The privileged action operates on a single resolved handle, eliminating the check-to-use mutation window.
Proving gate: Memory gate — ASan (double-free, heap-use-after-free); for TOCTOU, a security review note plus a stress/`rr record --chaos` repro of the race window. Validate `--profile memory` (and `--profile security` for the path/privilege boundary).
Precedent: SQLite's fault-injection and corruption corpus exercise error-path cleanup; SECURITY-REVIEW.md lists path handling, temp files, and privileged helpers as high-risk surfaces.

## Recipe 9 — Type-punning / strict-aliasing + over-read through a wider pointer cast

Bug class: memory/UB — strict-aliasing violation AND out-of-bounds over-read when a pointer is cast to a **wider** type and dereferenced ([MEMORY-SAFETY.md](MEMORY-SAFETY.md); [SECURITY-REVIEW.md](SECURITY-REVIEW.md)). This is the real klib `knetfile.c:173` defect the gauntlet surfaced: `*((unsigned long*)hp->h_addr)` reads **8 bytes** (an `unsigned long` on LP64) through a pointer to a `struct in_addr` that is only **4 bytes** — both a strict-aliasing UB (reading an object through an incompatible lvalue type the compiler may assume cannot alias) and a 4-byte over-read past the object. `cpp_risk_scan.sh` flags this in its dedicated **aliasing / cast-width over-read hazard** lane (separate from the bulk pointer-retype cast lane).

```c
/* Before: 8-byte read of a 4-byte in_addr; strict-aliasing UB + over-read (klib knetfile.c:173). */
server.sin_addr.s_addr = *((unsigned long*)hp->h_addr);
/* Before (C++): the same hazard via reinterpret_cast to a wider type. */
uint64_t v = *reinterpret_cast<uint64_t*>(p4);   /* p4 points at 4 bytes */
```

```c
/* After: copy exactly the source width through unsigned char / memcpy — no aliasing, no over-read. */
struct in_addr a;                       /* the real, correctly-sized destination type */
memcpy(&a, hp->h_addr, sizeof a);       /* exactly 4 bytes; well-defined, no aliasing assumption */
server.sin_addr = a;
/* General rule: read through the OBJECT's type (or memcpy into a value of it), never a wider one.
   memcpy through an unsigned char view is the standard, optimizer-recognized type-pun escape hatch. */
```

Invariant restored: a load reads exactly the bytes the source object owns, through a type compatible with that object (or via `memcpy`/`unsigned char`), so there is no strict-aliasing UB and no read past the object's storage. Width changes become explicit, sized copies.
Proving gate: Memory/UB gate — ASan + UBSan (`-fsanitize=alignment` catches the misaligned wide load; ASan catches the over-read when the source is heap/stack-bounded), `-fstrict-aliasing -Wstrict-aliasing=2` clean, plus a regression test on the exact-width read. Validate `--profile memory`.
Precedent: the Linux kernel and BearSSL/libsodium read multi-byte fields via `get_unaligned_*`/byte-wise loads rather than wide pointer casts; MEMORY-SAFETY.md lists type-punning through incompatible pointers as UB; this is the klib `knetfile.c:173` find recorded in the gauntlet FINDINGS.

## Recipe 10 — Unbounded recursion on attacker-controlled nesting (stack-overflow DoS, CWE-674)

Bug class: a recursive-descent parser (or any recursion driven by input structure) recurses once per nesting level with no depth limit, so untrusted input like `[[[[…`, `{{{{…`, or deeply parenthesized expressions exhausts the stack and crashes the process ([TESTING-FUZZING.md](TESTING-FUZZING.md) deep-nesting blind spot; [MEMORY-SAFETY.md](MEMORY-SAFETY.md)). It is a denial-of-service, not memory corruption, but it is a real, reachable crash on any worker thread with a small stack. Coverage-guided fuzzing rarely finds it (random mutation almost never emits tens of thousands of identical opening tokens), so it must be sought by directed deep-nesting seeds and by reading the parser for an uncapped recursive call. Three blind trials on unseen parsers reproduced exactly this: tomlc99 `parse_array` (`toml.c:1060`, self-recursive per `[`) and tinyexpr's `expr→…→base→expr` cycle both stack-overflowed under ASan; the input was a valid NUL-terminated string the API documents it accepts.

```c
/* Before: parse_array recurses on every nested '[' with no cap (DoS on deep input). */
static node_t *parse_array(parser_t *p) {
    expect(p, '[');
    while (peek(p) != ']') {
        if (peek(p) == '[') append(p, parse_array(p));   /* unbounded self-recursion */
        else                append(p, parse_scalar(p));
    }
    ...
}
```

```c
/* After: thread a depth counter through the parser state and fail closed past a cap. */
enum { MAX_PARSE_DEPTH = 200 };                 /* documented, well under any real stack */
static node_t *parse_array(parser_t *p) {
    if (++p->depth > MAX_PARSE_DEPTH) {          /* fail BEFORE the recursive call */
        set_error(p, "nesting too deep");
        --p->depth; return NULL;
    }
    expect(p, '[');
    while (peek(p) != ']') { ... append(p, parse_array(p)); ... }
    --p->depth;                                  /* restore on every return path */
    ...
}
/* Equivalent fixes: convert the recursion to an explicit heap stack/work-list, or
   (less portable) probe remaining stack. A constant cap is simplest and auditable. */
```

Invariant restored: recursion depth is bounded by a constant independent of input, so the worst-case stack is fixed and known; over-deep input returns a clean parse error instead of crashing. Mutually recursive paths (e.g. array↔inline-table) must share the SAME counter, and every early return must decrement it.
Proving gate: Fuzz/dynamic gate — a directed deep-nesting regression input (e.g. `"a=" + "["*200000`) that crashed under ASan `stack-overflow` before the fix and returns a parse error after; keep it as a seed. Pair with the `-fno-sanitize-recover=all` UBSan build. Validate `--profile parser` (or `security`).
Precedent: classic parser-DoS CVEs (libxml2 entity/element nesting, many JSON/TOML/expression parsers) are this class; RFC 8259 explicitly lets a JSON implementation set a maximum nesting depth. The depth cap is the standard fix.

# Part B — Binary-Size Methodology

## Why size is a first-class budget

For embedded, space, and firmware packs, binary-size is a hard budget like latency or RSS. PERFORMANCE.md already lists "binary size, icache pressure, and cold-start behavior" as a lever and a user-visible metric. Treat an unexplained size increase like a perf regression: it needs a baseline, a single lever, and a recorded delta. There is **no-size-regression** gate below.

## Size oracle

Capture the size profile before and after, in a release-like build, recording compiler, flags, LTO/strip state, and target arch:

```bash
size -A out.elf                 # per-section bytes: .text/.rodata/.data/.bss
nm --size-sort --print-size out.o | tail -40   # largest symbols (template/inline bloat)
bloaty -d compileunits out.elf  # attribute bytes to translation units
bloaty -d symbols old.elf -- new.elf           # per-symbol before/after delta
readelf -SW out.elf             # section sizes/flags; check for surprise sections
```

Cost attribution to inspect explicitly: exception tables (`.eh_frame`, `.gcc_except_table`), RTTI (`-frtti` vtables/typeinfo), `<iostream>` static initializers pulling locale/streams into every TU, and template instantiation duplication across units.

## Per-change size delta record

For every change that could move code size, record the delta beside the perf packet:

```text
Size delta:
- Binary/section: <file, .text/.rodata/total>
- Build: <compiler, flags, -O level, LTO, strip, target arch>
- Before bytes: <size -A and bloaty totals>
- After bytes:
- Delta: <+/- bytes, +/- %>
- One lever: <the single size mechanism changed>
- Justification: <why any growth is required; what shrank in exchange>
- Behavior oracle: <goldens/tests prove the binary still behaves identically>
```

## Levers

Apply one at a time and re-measure with the oracle, smallest blast radius first:

- `-Os` / `-Oz` (Clang) for size-sensitive TUs; confirm hot paths did not regress on the perf gate.
- LTO (`-flto` / `-flto=thin`) to drop cross-TU dead code and dedupe.
- `-ffunction-sections -fdata-sections` plus `-Wl,--gc-sections` to garbage-collect unreferenced functions/data.
- `strip` (or `-Wl,--strip-all`) for release artifacts; keep separated debug info.
- Hidden visibility (`-fvisibility=hidden`, `-fvisibility-inlines-hidden`) plus `-Wl,--exclude-libs,ALL` to stop re-exporting static-lib symbols and shrink the dynamic symbol table.
- Template-instantiation bloat reduction: extern templates, fewer monomorphizations, type-erased seams behind a stable boundary; confirm via `nm --size-sort` that the duplicated symbols collapsed.
- Avoid `<iostream>` where it only adds static init; prefer `<cstdio>`/`fmt` so locale/stream machinery is not linked into every TU.
- `-fno-exceptions` / `-fno-rtti` **only where the contract allows it** — these change semantics (no `throw`, no `dynamic_cast`/`typeid`), so they are a contract change recorded under [REFACTOR-ISOMORPHISM.md](REFACTOR-ISOMORPHISM.md), not a free win.

## No-size-regression gate

Tie size to the existing `performance` profile and the Optimization Card:

1. Record the size oracle (`size -A`, `bloaty`) at baseline with full build metadata.
2. Change one lever; rebuild the same way.
3. Re-run the size oracle and the behavior oracle (goldens/tests/checksums) so the smaller binary is proven isomorphic.
4. Compute the delta. Any growth must be justified in the delta record or the change is rejected, exactly as an average-latency win that hides a p99 regression is rejected.
5. Fill the performance gate with `baseline:`, `profile:`/`hotspot:` (here, the size oracle output), `score:`, `oracle:`, and `after:`/`result:`, then run the strict check:

```bash
python3 .opencode/skills/c-cpp-profi/scripts/cpp_evidence_check.py gate-report.md --profile performance --require-performance-proof
```

Precedent: PERFORMANCE.md's Levers and Baseline matrix treat binary size and icache pressure as measured metrics; the canon's "performance claims require representative methodology" invariant applies to size deltas as much as to timing.
