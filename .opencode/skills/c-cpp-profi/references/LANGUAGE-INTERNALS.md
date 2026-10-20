# Language Internals — C & C++ Object/Memory Models, ABI, Templates & Type System

> The mechanisms underneath the syntax: what the compiler and ABI actually do, so an agent can reason at the lowest level. Pair with [MEMORY-SAFETY.md](MEMORY-SAFETY.md), [REMEDIATION-RECIPES.md](REMEDIATION-RECIPES.md) (Recipe 9 aliasing), [BUILD-PORTABILITY.md](BUILD-PORTABILITY.md), and the ABI gate in [QUALITY-GATES.md](QUALITY-GATES.md). Each major area cites authoritative sources (ISO, cppreference, Itanium C++ ABI, Lippman, Vandevoorde/Josuttis).


## C Memory & Object Model

### Objects, storage, and access
An **object** is a "region of data storage in the execution environment, the contents of which can represent values" (C standard §3.15). Every object has a **size** (`sizeof`), an **alignment**, a **storage duration**, and — once written — possibly an **effective type**. `sizeof(char) == 1` by definition; a byte is `CHAR_BIT` bits (>= 8, almost universally 8). C does **not** require IEEE-754, two's-complement (mandated only in C23, see below), or 8-bit bytes pre-C23 — but C23 requires two's-complement for signed integers (N3220, adopting the C++20 alignment).

### Effective type (the key concept behind aliasing)
The **effective type** of an object determines which lvalue types may legally access it (§6.5p6–7, identical numbering in C11/C17/C23):
- An object **declared with a type** has that declared type as its effective type for its whole lifetime.
- For **allocated storage** (`malloc`, no declared type), the object is *typeless* until written. A store through an lvalue of type `T` (other than a character type) **sets** the effective type to `T`. A store via `memcpy`/`memmove` or via a character-type lvalue copies the effective type of the *source* (or leaves it typeless). A read of an object with no effective type uses the lvalue type of the access.
- This is why `malloc`'d buffers can be reused for different types across their lifetime: each store re-establishes the effective type.

### Object lifetime
- **Static storage duration**: lifetime = whole program (file-scope objects, `static` locals). Zero-initialized if no initializer.
- **Thread storage duration** (`_Thread_local` / C23 `thread_local`, since C11): lifetime = the thread.
- **Automatic storage duration**: lifetime = enclosing block (or function for parameters). Lifetime ends at block exit; a VLA's lifetime ends when the *declaration* goes out of scope. Reusing a pointer to an automatic object after its block ends is **use-after-free / dangling** UB.
- **Allocated storage duration**: from `malloc`/`calloc`/`realloc`/`aligned_alloc` until `free`/`realloc`. After `free`, the pointer value itself becomes **indeterminate** — even *reading or comparing* the freed pointer is UB (not just dereferencing it), per §6.2.4.

### Alignment
Every type has an alignment requirement (a power of two). Accessing an object through a misaligned pointer is UB. `_Alignas` / `_Alignof` (C11; spelled `alignas`/`alignof` natively in C23, and via `<stdalign.h>` macros in C11/C17). `max_align_t` (in `<stddef.h>`) gives the strictest fundamental alignment; `malloc`-returned storage is suitably aligned for **any** object that fits. `aligned_alloc(alignment, size)` (C11): as **published**, C11 required `size` to be an integral multiple of `alignment` (otherwise UB), but **DR 460** (applied retroactively to C11/C17) relaxed this — an unsupported `alignment`/`size` is now a defined failure that returns `NULL`. In practice glibc does **not** enforce the size-multiple rule; portable code should still not rely on either behavior.

## Strict Aliasing Rule & Type Punning

### The rule (§6.5p7, C11 N1570 / C17 / C23 N3220)
An object's stored value may be accessed **only** through an lvalue of one of:
1. a type **compatible** with the effective type;
2. a **qualified** version of a compatible type;
3. the **signed/unsigned** version of the effective type (or a qualified version thereof);
4. an **aggregate or union** type that contains one of the above among its members (including, recursively, a member of a contained aggregate/union);
5. **a character type** (`char`, `signed char`, `unsigned char`).

Anything else is **undefined behavior**. The optimizer is *entitled to assume* aliasing does not occur (Type-Based Alias Analysis, TBAA), so `-O2` code can silently break violating programs. Note: C23 added `unsigned char`/`char8_t` nuances but the core list is unchanged since C99. (CERT EXP39-C.)

### Type punning — what is and isn't allowed
- **`union` member punning is well-defined in C** (footnote in §6.5.2.3): reading a different member than last written reinterprets the bytes (result may be a trap representation / unspecified). **This differs from C++**, where union type punning is technically UB (read-inactive-member). Common interview trap.
- **Pointer-cast punning** (`*(float*)&some_int`) is a **strict-aliasing violation** and UB.
- **The escape hatch**: `memcpy` into a fresh object of the target type. `memcpy(&f, &i, sizeof f);` is always legal because character-type access is permitted and `memcpy` copies the representation; compilers fully optimize it to a register move. This is the portable, blessed idiom for bit-reinterpretation.
- **C23** adds `<stdbit.h>` and, more cleanly, prefer `memcpy`. There is no `std::bit_cast` in C.

## Integer Conversions & Arithmetic

### Integer promotions (§6.3.1.1)
Any operand of an integer type with **rank lower than `int`** (`char`, `short`, `_Bool`, bit-fields, enums fitting in `int`) is promoted: to `int` if `int` can represent all its values, else to `unsigned int`. Promotions apply in arithmetic, `<<`/`>>` (each operand promoted independently — the **shift result type is that of the promoted left operand**), unary `+ - ~`, and variadic/default-argument contexts. Gotcha: `uint8_t a,b; a*b` is computed in `int`, and `~uint8_t` yields a (usually negative) `int`.

### Usual arithmetic conversions (§6.3.1.8)
For binary arithmetic on two promoted operands of different types, both convert to a common type by rank, with the classic signedness rule:
1. If same signedness → higher rank wins.
2. If the unsigned operand's rank >= the signed operand's rank → convert to the **unsigned** type.
3. Else if the signed type can represent all values of the unsigned type → convert to the **signed** type.
4. Else → convert both to the unsigned counterpart of the signed type.
Classic bug: `int i = -1; unsigned u = 1; i < u` is **false** because `i` converts to a huge unsigned. (`size_t` comparisons are a perennial source of this.)

### Signedness & overflow
- **Unsigned** arithmetic is defined to **wrap modulo 2^N** — never overflows.
- **Signed** integer overflow is **UB** (§6.5p5). The compiler may assume it never happens (e.g., it can conclude `x + 1 > x` is always true, delete bounds checks, or promote loop counters). Use `__builtin_add_overflow` / C23 `<stdckdint.h>` `ckd_add`/`ckd_sub`/`ckd_mul` for checked arithmetic.
- Conversion of an out-of-range value **to a signed type** is implementation-defined (or raises an impl-defined signal), **not** UB; to an unsigned type it wraps (well-defined). (CERT INT32-C.)

## Undefined / Unspecified / Implementation-Defined — Taxonomy

- **Implementation-defined behavior**: behavior depending on the implementation, which **must document** its choice (e.g., size of `int`, right-shift of a negative integer is arithmetic-or-logical-but-documented, signedness of plain `char`, result of out-of-range conversion to signed).
- **Unspecified behavior**: the standard provides two or more possibilities and imposes **no requirement** on which is chosen, and need **not** document it (e.g., **order of evaluation of function arguments**, order of subexpression evaluation between sequence points, layout padding bytes' values).
- **Undefined behavior (UB)**: the standard imposes **no requirements whatsoever** — "anything can happen," including time-travel/optimizing-away surrounding code. The compiler may assume UB never occurs.

### Worst offenders (each is UB)
- **Signed integer overflow** (§6.5p5).
- **Out-of-bounds array access / invalid pointer arithmetic** — forming a pointer more than one-past-the-end of an array, or dereferencing one-past-the-end, is UB (the *one-past-the-end* pointer may be formed and compared but not dereferenced).
- **Use-after-free / dangling pointer** — including merely *using the value* of a freed or end-of-lifetime pointer.
- **Data races** — two threads, conflicting access (>=1 write) to the same non-atomic object without happens-before ordering (C11 memory model, §5.1.2.4).
- **Strict-aliasing violations** (above).
- **Shifting** by a count that is **negative or >= width** of the promoted left operand → UB; also left-shifting a **negative** signed value, or a positive value whose result is unrepresentable in the result type → UB (C11; C23 relaxed left-shift of unsigned/two's-complement somewhat but the >=width rule stands). (CERT INT34-C.)
- **Null-pointer dereference**, **modifying a string literal** or a `const` object, **calling through an incompatible function-pointer type**, **reading an uninitialized object with automatic storage** (when its address was never taken — may hold a trap representation).

## Sequence Points / Sequenced-Before

- **C90/C99 model**: *sequence points* are points at which all side effects of prior evaluations are complete. Between two sequence points, modifying an object more than once, or modifying it and also reading it for a purpose other than computing the new value, is **UB**. Classic UB: `i = i++;`, `a[i] = i++;`, `i = i++ + 1;`. `f() + g()` has **unspecified** ordering but no UB (the two calls' bodies have a sequence point boundary).
- **C11+ model** (aligned with C++11 for the thread memory model): replaces the global "sequence point" notion with **sequenced-before** / **indeterminately sequenced** / **unsequenced** relations. *Unsequenced* side effects on the same scalar → UB; *indeterminately sequenced* (e.g., separate function calls) means one fully precedes the other but which is unspecified.
- **Guaranteed sequence points / sequencing**: end of a **full expression** (incl. the controlling expression of `if`/`while`/`for`, each clause of `for`, the expression in a `return`); after evaluation of **all function arguments and the designator**, before the call; the second operands of **`&&`**, **`||`**, and **`?:`** (left fully sequenced before right/branch); after the left operand of the **comma `,`** operator; after a full declarator's initializer. Note the **assignment operator is NOT a sequence point** and its value-computation/side-effect ordering relative to operand evaluation is only partially constrained (C11 sequences the assignment's value computation after both operands but the store is unsequenced w.r.t. unrelated reads).

## Preprocessor

- **Macros**: object-like `#define X 1`, function-like `#define MAX(a,b) ((a)>(b)?(a):(b))`. Always **parenthesize parameters and the whole body**; function-like macros evaluate arguments **multiple times** (side-effect hazard: `MAX(i++, j)`).
- **`#` stringize** turns a parameter into a string literal; **`##` token paste** concatenates tokens. Need the classic **two-level indirection** to expand arguments before stringizing/pasting: `#define STR(x) #x` / `#define XSTR(x) STR(x)`.
- **X-macros**: a reusable list `#define LIST(X) X(A) X(B)` (or an included `.def` file) expanded with different `X` definitions to generate enums, name tables, and dispatch in sync — the canonical metaprogramming trick in C.
- **Include guards**: `#ifndef HDR_H` / `#define HDR_H` / … / `#endif`; `#pragma once` is non-standard but ubiquitously supported.
- **Predefined**: `__STDC__`, `__STDC_VERSION__` (C99=`199901L`, C11=`201112L`, C17=`201710L`, C23=`202311L`), `__FILE__`, `__LINE__`, `__func__` (a *predefined identifier*, not a macro, since C99), `__VA_ARGS__` for variadic macros (C99). C23: `__VA_OPT__`, `#elifdef`/`#elifndef`, `#embed`, and `#warning` standardized.

## Linkage & Storage Duration

- **External linkage**: file-scope identifiers without `static` (or `extern` re-declarations); shared across translation units. A name with **internal linkage** (`static` at file scope) is unique per TU. **No linkage**: block-scope objects (unless `extern`), parameters, typedefs, enum constants.
- **Definition vs declaration**: `extern int g;` *declares*; `int g;` (at file scope) or `int g = 0;` *defines*. Exactly **one** definition program-wide for an external object (ODR-like; violations are UB, often link errors).
- **Tentative definitions** (§6.9.2, C-specific): a file-scope `int g;` with no initializer and no `extern` is a *tentative* definition; if no external definition with initializer appears in the TU, it becomes a single definition initialized to zero. Multiple tentative definitions of the same object in one TU coalesce. (This is why `int g;` in two TUs historically "worked" under the common/`-fcommon` model — but C23 and modern GCC default to `-fno-common`, making such duplicates link errors.)
- **`static` local**: static storage duration, internal-to-the-block visibility, initialized once.

## `restrict`, `volatile`, and Other Qualifiers

### `restrict` (C99, §6.7.3)
A promise that, for the lifetime of the restricted pointer, **every access to the pointed-to object is made through pointers based on that restricted pointer**. It is a type qualifier applicable only to pointers to objects. It carries **no semantic meaning** by itself; it purely licenses optimization (the compiler may assume no aliasing among distinctly-`restrict`-qualified pointers, enabling vectorization, reordering, register caching). **Violating the promise is UB** even though the unqualified code would be fine. Canonical use: `void *memcpy(void *restrict dst, const void *restrict src, size_t n);` — passing overlapping ranges is UB (use `memmove`). (CERT EXP43-C.)

### `volatile`
Every access (read/write) to a `volatile`-qualified lvalue is an **observable side effect** that the implementation must not elide, reorder among other volatile accesses, or coalesce. It models **memory-mapped I/O** and `sig_atomic_t` flags touched by signal handlers / `setjmp`/`longjmp`. **`volatile` is NOT for thread synchronization** — it provides no atomicity and no inter-thread ordering; use `_Atomic`/`<stdatomic.h>` (C11) for that. `volatile` does not prevent torn reads/writes.

### `const`
Modifying an object **defined** `const` through a cast-away pointer is UB. Casting away `const` from a pointer to a non-`const`-defined object is legal.

## Flexible Array Members (C99, §6.7.2.1)

A `struct` whose **last** member is an array of **incomplete (unsized) type**, provided the struct has **at least one other named member**: `struct s { size_t len; int data[]; };`. The struct then has incomplete type only where the FAM appears; you size allocations as `malloc(sizeof(struct s) + n * sizeof(int))`. `sizeof(struct s)` counts the struct **as if the FAM were absent** (may include trailing padding the FAM can reuse). Accessing more than the allocated elements, or copying the struct by assignment (which doesn't copy the array), or putting an FAM struct anywhere but last in another struct/array, is UB. The legacy GCC "zero-length array" (`int data[0];`) and the "struct hack" (`int data[1];`) are non-standard predecessors. (CERT DCL38-C.)

## `_Generic` (C11, §6.5.1.1)

Compile-time, **type-driven** selection: `_Generic(controlling-expr, type1: expr1, type2: expr2, default: exprN)`. The controlling expression is **not evaluated** — only its type (after lvalue/array/function decay, with top-level qualifiers stripped) is matched against the type names; the chosen expression's value/type becomes the result. Each listed type must be a **complete object type**, all distinct; `default` is optional, and a missing match without `default` is a constraint error. It underpies **type-generic macros** (e.g., `<tgmath.h>` dispatching `sqrt`/`sqrtf`/`sqrtl`). Idiom for ad-hoc overloading: `#define abs(x) _Generic((x), int: absi, double: fabs, float: fabsf)(x)`. It does not recurse into expression value, cannot match qualified/array forms after decay, and pairs with `__typeof__`/C23 `typeof` for generic programming.

## Standard Library Structure

- **Freestanding** implementations must provide only headers that need no OS: `<float.h>`, `<iso646.h>`, `<limits.h>`, `<stdalign.h>`, `<stdarg.h>`, `<stdbool.h>`, `<stddef.h>`, `<stdint.h>`, `<stdnoreturn.h>` (C11; in C23 several of these — `<stdalign.h>`, `<stdbool.h>`, `<stdnoreturn.h>` — become near-empty as `alignas`/`bool`/`true`/`false`/`[[noreturn]]` are keywords). **Hosted** adds the full library (`<stdio.h>`, `<stdlib.h>`, `<string.h>`, `<math.h>`, `<time.h>`, etc.).
- Notable additions by version: **C99** — `<stdint.h>`, `<inttypes.h>`, `<stdbool.h>`, `<complex.h>`, `<tgmath.h>`, `<fenv.h>`, `restrict`, VLAs, designated initializers, compound literals, `//` comments, `long long`. **C11** — `<stdatomic.h>`, `<threads.h>`, `<stdalign.h>`, `<stdnoreturn.h>`, `_Generic`, `_Static_assert`, anonymous structs/unions, `aligned_alloc`, bounds-checked `_s` Annex K (optional), `gets` removed. **C17/C18** — defect-fix release, no new features. **C23 (N3220)** — `<stdbit.h>`, `<stdckdint.h>`, `bool`/`true`/`false`/`nullptr`/`typeof` as keywords, `constexpr` objects, binary literals `0b`, digit separators `'`, `[[attributes]]`, `_BitInt(N)`, `#embed`, `__VA_OPT__`, UTF-8 `char8_t`, mandated two's-complement, `auto` type inference, `=` empty-initializer, `realloc(p,0)` no longer special-cased. VLAs became **optional** in C11 (`__STDC_NO_VLA__`) and partly re-required for VLA *types* (not automatic VLA objects) in C23.

### Sources
- ISO/IEC 9899 — WG14 C standard drafts: C23 **N3220** and C11 **N1570** (effective type & aliasing §6.5p6–7; conversions §6.3.1.1/§6.3.1.8; shift/overflow §6.5; FAM §6.7.2.1; `restrict` §6.7.3; `_Generic` §6.5.1.1; library §7). https://www.open-std.org/jtc1/sc22/wg14/www/docs/n3220.pdf , https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf
- cppreference.com — C language reference (effective type, generic selection, restrict, eval order, FAM). https://en.cppreference.com/w/c/language
- SEI CERT C Coding Standard — EXP39-C (incompatible-type access / aliasing), EXP43-C (restrict UB), INT32-C (signed overflow), INT34-C (shift width), DCL38-C (FAM syntax). https://wiki.sei.cmu.edu/confluence/display/c
- Shafik Yaghmour, "What is the Strict Aliasing Rule and Why do we care?" (effective type, union punning, memcpy escape hatch). https://gist.github.com/shafik/848ae25ee209f698763cffee272a58f8
- Brian Kernighan & Dennis Ritchie, *The C Programming Language*, 2nd ed.; and Harbison & Steele, *C: A Reference Manual* (linkage, tentative definitions, storage duration).

## C++ Object Model & ABI Internals

Most concrete layout/dispatch/EH facts below describe the **Itanium C++ ABI** (used by GCC/Clang on Linux/macOS/most Unix; followed approximately by every non-MSVC toolchain). MSVC uses its own undocumented ABI — call out the differences explicitly (vtordisp, different mangling, different EH model on x86 vs SEH). The C++ *standard* mandates almost none of this; layout, vtable mechanism, mangling, and EH are all "implementation-defined" or unspecified. Treat ABI facts as platform contracts, not language rules.

### Object categories: trivial / standard-layout / POD / trivially-copyable

These are orthogonal classifications. Know which trait answers which question.

- **Trivial type** (`std::is_trivial_v`): trivially copyable **and** has a trivial default constructor (no user-provided/deleted, no non-trivial member init, no virtuals/vbases). "Trivial" governs whether default-construction and copy are byte-level no-ops. *Note: `std::is_trivial`/`is_trivial_v` are themselves **deprecated in C++26** (P3247) — prefer the specific traits.*
- **Trivially copyable** (`std::is_trivially_copyable_v`, C++11): every copy/move ctor, copy/move assignment is trivial or deleted (at least one not deleted), and the destructor is trivial and non-deleted. This is **the** trait that licenses `std::memcpy` of the object representation and `std::bit_cast`. Trivially-copyable does NOT require contiguous/predictable layout — only that copying bytes is well-defined. A type may be trivially copyable but NOT standard-layout (e.g., has virtual functions? no — virtuals make copy ctor non-trivial; but multiple access specifiers keep it trivially copyable yet non-standard-layout).
- **Standard-layout** (`std::is_standard_layout_v`): the layout is C-compatible. Requirements (all must hold): no virtual functions or virtual bases; all non-static data members have the **same access control**; no non-static data members in more than one class in the hierarchy (only one class in the chain may have NSDMs); no base class of the same type as the first non-static data member (this is what makes the address of the first member equal the address of the object — `[[no_unique_address]]`/EBO interaction); all base classes and NSDMs are themselves standard-layout. Standard-layout guarantees `offsetof` works, that a pointer to the object equals a pointer to its first member (`reinterpret_cast` between them is valid), and common-initial-sequence access through a union.
- **POD** (= trivial **and** standard-layout): **deprecated in C++20** along with `std::is_pod`/`is_pod_v`. Do not introduce POD reasoning in new code; decompose into the two independent properties (memcpy-ability vs C-layout-compatibility) per the rationale in the C++20 deprecation. The vast majority of code wants `is_trivially_copyable` (for memcpy) or `is_standard_layout` (for C interop), rarely both.
- **Implicit-lifetime type** (C++20, P0593): scalars, arrays, aggregate-or-trivial class types. Operations like `malloc`/`memcpy`/`std::bit_cast`/`std::start_lifetime_as` (C++23) can begin the lifetime of such objects without running a constructor — the formalization that legitimizes the long-standing "allocate buffer, treat bytes as struct" idiom.

Gotcha: a single user-declared (even `= default` **out-of-line**) special member can flip triviality. `= default` *in the class definition* keeps triviality; `= default` *out of line* makes it user-provided and non-trivial. This is a frequent silent regression.

### Data member layout, alignment, padding

- Within a single access region, members are laid out in declaration order at non-decreasing addresses (guaranteed since C++11 for standard-layout; for non-standard-layout the relative order across access specifiers is unspecified — compilers may reorder between access blocks, though GCC/Clang/MSVC in practice don't).
- Each member is placed at an offset that is a multiple of its alignment (`alignof`); the compiler inserts **padding** bytes. The whole struct's size is rounded up to a multiple of its overall alignment so that arrays keep every element aligned (`sizeof` is always a multiple of `alignof`). Hence reordering members large→small minimizes padding.
- `alignas` raises alignment (never lowers it). Over-aligned types (alignment > `alignof(std::max_align_t)`) need `operator new(size_t, std::align_val_t)` (C++17) to allocate correctly; plain `new`/`malloc` may under-align.
- `[[no_unique_address]]` (C++20) lets an empty (or potentially-empty) NSDM share an address with another member/subobject — the member-side analog of EBO. The padding/tail-padding reuse it enables is what makes zero-overhead "policy"/allocator/comparator members feasible without inheritance tricks. MSVC requires `[[msvc::no_unique_address]]` for ABI reasons.
- **Tail padding reuse**: a derived class may place its members inside the tail padding of a standard-layout-ish base in the Itanium ABI. This is why `sizeof(Derived)` can be < `sizeof(Base) + sizeof(extra members)`, and why `memset(this, 0, sizeof(*this))` in a base ctor can clobber derived members. Common UB source.

### Empty Base Optimization (EBO)

A complete object must have size ≥ 1 so distinct objects have distinct addresses; **base subobjects are exempt** and an empty base can occupy zero bytes. EBO does NOT apply when the empty base type also appears as (or as the type of) the first data member, because the base subobject and that member would need distinct addresses. EBO is the foundation of zero-overhead policy classes; `std::tuple`, `std::vector` (allocator), and `std::unique_ptr` (deleter) all rely on it (or on `[[no_unique_address]]` post-C++20) to avoid paying for stateless allocators/deleters/comparators.

### vptr / vtable mechanism (Itanium ABI)

A class with virtual functions or virtual bases gets a hidden **vptr** at offset 0 of its (primary-base) subobject. The vptr points to the **address point** of the vtable. Itanium vtable layout, relative to the address point:

- **Negative offsets** (header, in order farther from the address point): vcall offsets (for virtual-base virtual dispatch), then vbase offsets (one per virtual base), then at slot −2 the **offset-to-top** (`ptrdiff_t` displacement from this vptr to the start of the complete object — used by `dynamic_cast<void*>` and `this`-adjustment), then at slot −1 the **typeinfo pointer** (the `std::type_info` for RTTI; null/absent for non-polymorphic types — only polymorphic types have it, which is why `dynamic_cast`/`typeid` on references/pointers require a polymorphic type).
- **At/after the address point**: the **virtual function pointers**, in declaration order (overriders replace the base slot in place, preserving the slot index across the hierarchy — that fixed index is what makes dispatch O(1)).

The address point — not the start of the vtable — is what the vptr stores, precisely so the dispatcher indexes positively into function pointers and the runtime indexes negatively into RTTI/offsets.

**Virtual dispatch cost**: load vptr (1 dependent load), load function pointer at fixed offset (2nd dependent load, cache-missable), indirect call (branch-predictor dependent; misprediction is the real cost, often 10–20+ cycles, vs ~1–2 for the loads when hot). Virtual calls also **block inlining** unless the compiler can devirtualize (final, exact static type, LTO/`-fwhole-program-vtables`). Mark leaf overrides/classes `final` to enable speculative devirtualization.

- **Pure virtual** (`= 0`): the slot is filled with `__cxa_pure_virtual` (calling it = std::terminate). A pure virtual may still have a definition (callable via qualified name).
- **Virtual destructor**: occupies **two** adjacent vtable slots in Itanium — the *complete object destructor* (mangled `D1`) and the *deleting destructor* (mangled `D0`, which destroys then calls `operator delete`). The *base object destructor* (`D2`) is non-virtual and is NOT placed in the vtable. Deleting through a base pointer without a virtual destructor is UB. Adding a virtual destructor adds a vptr (8 bytes) and breaks triviality/standard-layout.

### RTTI cost (`typeid`, `dynamic_cast`)

- `typeid(expr)` on a **polymorphic** glvalue loads the type_info via the vptr at runtime (cheap, ~a vtable load); on non-polymorphic types it's a compile-time constant. `typeid` evaluates its operand only if it's a polymorphic glvalue.
- `dynamic_cast` to a derived type performs a runtime tree walk of `__class_type_info` (single, multiple, virtual inheritance variants `__si_class_type_info`/`__vmi_class_type_info`), comparing type_info nodes and applying recorded offsets. Cost is **unbounded in the worst case** (multiple/virtual inheritance graphs) and far more expensive than a virtual call — hot paths sometimes replace it with a virtual `clone`/visitor or a tagged enum. `dynamic_cast<void*>` uses offset-to-top to get the most-derived object address. Cross-casts and downcasts through virtual bases are the costly cases. `-fno-rtti` removes the type_info emission and breaks `dynamic_cast`/`typeid`-on-polymorphic.

### Name mangling & the Itanium ABI; linkage

- Mangled names encode the fully-qualified name + parameter types (NOT return type for non-templates) so overloads link distinctly. Itanium scheme: prefix `_Z`; nested/qualified names wrapped `N...E` with length-prefixed identifiers (`3foo`); `St` = `std`, plus a **substitution** table (`S_`, `S0_`, …) compressing repeated components. Demangle with `c++filt` or `abi::__cxa_demangle`. MSVC uses a completely different scheme (`?name@@...`); demangle with `undname`.
- `extern "C"` gives **C language linkage**: the function is NOT mangled (the symbol is the bare name), so it can be called from C / `dlsym`'d by name. Consequences: an `extern "C"` function **cannot be overloaded** (one symbol), and a function *pointer* with C linkage is a distinct type from a C++-linkage one (rarely matters but is a real type mismatch in strict mode). `extern "C"` affects linkage/calling, not the body's language.
- **Return type IS part of the mangling for function templates** (because it can participate in overload resolution / be the only distinguishing feature), but not for ordinary functions.

### ODR, inline, templates, vague linkage

- **ODR**: every entity used must be defined exactly once across the program; classes/inline functions/templates may be defined in multiple TUs **only if** all definitions are token-for-token and entity-for-entity identical. Violations are **IFNDR** (ill-formed, no diagnostic required) — the linker silently picks one definition, producing the classic "works in debug, miscompiles in release" / mismatched-struct-layout bugs. ODR violations are a top cause of heisenbugs; differing `-D` macros, packing pragmas, or struct definitions across TUs are the usual culprits.
- **Vague linkage / COMDAT**: inline functions, templates, vtables, RTTI, and static-init guard variables have *vague linkage* — emitted in every TU that needs them and **COMDAT-folded** by the linker to a single copy. This is why `inline` is fundamentally about the ODR (permission to define in multiple TUs), not about inlining the call. `inline` variables (C++17) extend this to data, enabling header-only single-definition globals.
- The vtable & RTTI for a class are emitted in the TU that defines its **key function** (first non-inline, non-pure virtual). If you declare but never define that key function, you get "undefined reference to vtable for X" — the canonical linker error for a missing out-of-line virtual definition.

### Reference & temporary lifetime; lifetime extension

- A temporary normally dies at the end of the **full-expression** that created it.
- Binding a temporary to a **const lvalue reference** or an **rvalue reference** extends its lifetime to that of the reference. This is the only lifetime-extension mechanism.
- **It does NOT transitively pass on**: binding a second reference (or a class member reference) from the first reference does not re-extend. Storing into a member reference via a constructor's mem-initializer does **not** extend (the temporary dies at the end of the ctor's full-expression) — a classic dangling-member bug.
- **Function return does NOT extend**: `const T& f() { return T(); }` returns a dangling reference; the returned temporary dies at the end of the return statement's full-expression. C++26 (P2748) makes binding a *returned* reference to a temporary **ill-formed** at last.
- Range-based `for (auto x : f().items())` historically dangled when `f()` returns a temporary; **C++23 (P2718)** extends the lifetime of all temporaries in the range-init to the whole loop, fixing this footgun.

### Copy elision / RVO / NRVO / guaranteed elision (C++17)

- **Guaranteed copy elision (C++17, P0135)** is not really "eliding a copy" — it redefines prvalues so the object is **never** materialized as a temporary first. A prvalue is only *materialized* (becomes an xvalue / a temporary object) when needed (binding a reference, member access, etc.). Returning a prvalue or initializing from a prvalue of the same type constructs **directly** into the destination. Consequence: returning by value and initializing from `T(...)` works for **non-movable, non-copyable** types (no copy/move ctor need exist or be accessible). The copy/move ctor need not be accessible in these mandatory cases.
- **RVO** (returning a prvalue temporary) is the mandatory C++17 case. **NRVO** (returning a *named local* variable) is still **optional** — the standard permits but does not require it; if not done, an implicit *move* is tried first (the return operand is treated as an rvalue, C++11+/clarified by P1825 in C++20 for more cases). So `return std::move(local);` is an anti-pattern: it **disables NRVO** and can pessimize. Just `return local;`.
- Elision is one place the compiler may legally change observable behavior (skip ctor/dtor side effects) — independent of the as-if rule.

### Exceptions: throw/unwind, noexcept, table-based EH cost

- Itanium uses the **table-based ("zero-cost") model**: the happy path executes with **no overhead** (no per-frame registration); instead the compiler emits static unwind/`.gcc_except_table` (LSDA) data. On `throw`, the generated code first calls `__cxa_allocate_exception` to allocate the exception object and copy-constructs the thrown value into it, then calls `__cxa_throw`, which runs a **two-phase** unwind: phase 1 (search) calls each frame's **personality routine** (`__gxx_personality_v0`) to find a handler without altering state; phase 2 (cleanup) unwinds, running destructors and the matching `catch`. "Zero-cost" = zero cost when **not** thrown; **throwing is expensive** (table lookups, dynamic type matching, indirect transfers — often microseconds), so exceptions are for exceptional, not control-flow, paths.
- MSVC differs: x64 is table-based (similar), but x86 historically used frame-based SEH with runtime per-frame setup (not zero-cost).
- **`noexcept`**: a `noexcept` violation calls `std::terminate` (via `__cxa_call_terminate`); the compiler may omit unwind tables for that frame, shrinking code and enabling optimizations. `noexcept` is part of the **type system** (C++17, P0012) — function pointer/virtual override types carry it, and a `noexcept` function pointer can't bind a potentially-throwing function. Critically, **move constructors should be `noexcept`**: `std::vector` reallocation uses `std::move_if_noexcept` — a throwing move ctor forces vector to **copy** instead of move during growth (correctness/strong-guarantee preservation), a major silent performance loss.
- Throwing across a `noexcept` boundary, or letting an exception escape `main`/a destructor during unwinding, → `std::terminate`. Throwing from a destructor during stack unwinding is the canonical terminate trap.

### Virtual & multiple inheritance layout (thunks, vtordisp)

- **Multiple inheritance**: a class with N non-empty bases has multiple subobjects, each possibly with its own vptr/vtable. A pointer to a secondary base points **into the middle** of the complete object; converting between base/derived pointers requires **`this`-pointer adjustment** by a fixed offset (the offset-to-top / known static delta). The compiler emits **thunks**: tiny trampolines that adjust `this` before jumping to the real virtual function when it's called through a secondary base's vtable slot. **Covariant return** overrides also use thunks to adjust the *returned* pointer.
- **Virtual inheritance** (`virtual` base, the diamond): the shared virtual base subobject exists once, placed at a position only known at most-derived-object construction time. Access to vbase members goes through **vbase-offset** entries in the vtable (runtime offset, not a compile-time constant) — this is why virtual-base member access is slower and why `static_cast` from a virtual base to derived is ill-formed. During construction/destruction the vptrs must point to **construction vtables** so virtual calls resolve to the partially-built type; the **VTT** (virtual-table table) array supplies these to ctors. This is also why calling a virtual function from a constructor dispatches to the *current* class's override, not the most-derived one.
- **vtordisp** is an **MSVC-only** mechanism (`/vd`, `#pragma vtordisp`): a hidden displacement field for adjusting `this` when a virtual function declared in a class with virtual bases is called during construction/destruction. Itanium handles the same problem with construction vtables/VTT instead; there is no vtordisp in the Itanium ABI.

### The as-if rule and initialization order

- **As-if rule** ([intro.execution]): the implementation may transform the program arbitrarily provided **observable behavior** is preserved — observable = volatile accesses, I/O (data written to files/stdout), and (since the relevant memory model) program termination. This licenses all optimization. Copy elision is a *separate, explicit* exception that may change observable side effects. Floating-point contraction (`-ffast-math`) and reordering are NOT permitted under as-if unless allowed (FP is observable per IEEE); `-ffast-math` opts out of strict conformance.
- **Static initialization order**: within a TU, namespace-scope objects with dynamic initialization init in **definition order**; *zero/constant initialization* happens first (before any dynamic init). **Across TUs the order is unspecified** → the **Static Initialization Order Fiasco**: object A in TU1 using object B in TU2 may run before B is constructed. Fix with the **Construct-On-First-Use idiom** (function-local `static` — guaranteed initialized on first call, thread-safely since C++11 via guard variables / `__cxa_guard_acquire`), or `constinit` (C++20) to force constant initialization at compile time (no fiasco, but only for compile-time-constructible objects). Function-local statics also have a symmetric **destruction order fiasco** at exit (`__cxa_atexit` LIFO) — accessing one destroyed static from another's destructor is UB.

### Sources
- Itanium C++ ABI (itanium-cxx-abi / CodeSourcery–HP), vtable layout, thunks, VTT/construction vtables, mangling, vague linkage — https://itanium-cxx-abi.github.io/cxx-abi/abi.html
- Itanium C++ ABI: Exception Handling (two-phase unwind, personality routine, LSDA) — https://itanium-cxx-abi.github.io/cxx-abi/abi-eh.html
- cppreference.com — object/type model, trivial/standard-layout/trivially-copyable, EBO, `[[no_unique_address]]`, copy elision, lifetime/reference initialization, `noexcept` — https://en.cppreference.com/
- Stanley B. Lippman, *Inside the C++ Object Model* (Addison-Wesley) — vptr/vtable construction, `this`-adjustment, member layout, ctor/dtor virtual semantics
- C++ Core Guidelines (Stroustrup & Sutter) — value semantics, RAII, rule of five, `noexcept` move guidance — https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines

## Templates, Metaprogramming & the Type System

Templates are a Turing-complete, compile-time substitution mechanism. A template is *not* code — it is a pattern from which the compiler generates ("instantiates") code on demand. Mastering this area means reasoning precisely about *when* names are looked up, *when* types are deduced, *what value category* an expression has, and *which standard version* a feature requires.

### Template kinds & basic forms

- **Function templates** — `template<class T> T max(T,T);`. Template args are usually *deduced* from the call; explicit args fill from the left (`max<int>(...)`). Deduction does **not** consider implicit conversions on deduced parameters (only identity, qualification, derived→base, array/function decay).
- **Class templates** — `template<class T> struct vector;`. Pre-C++17 the type args were mandatory at the use site; C++17 CTAD relaxes this (see below).
- **Variable templates** (C++14) — `template<class T> constexpr T pi = T(3.1415926535);`. Heavily used for trait shortcuts: `template<class T> inline constexpr bool is_pointer_v = is_pointer<T>::value;`.
- **Alias templates** (C++11) — `template<class T> using Vec = std::vector<T, MyAlloc<T>>;`. Alias templates are **never deduced** and **cannot be specialized**; they are pure aliases (transparent to the type system). A common trick: `template<class...> using void_t = void;` (C++17).
- **Member templates**, including templated constructors/conversion operators and the templated `operator()` that makes function objects generic.

### Instantiation model & two-phase lookup

Templates use **two-phase name lookup** (a hard rule of the standard, enforced strictly by GCC/Clang; MSVC historically delayed phase 1 but `/permissive-` fixes it):

- **Phase 1 (at definition / parse time):** non-dependent names are bound and the template body is parsed/checked for syntax. Names not depending on a template parameter are resolved *here*, using the declarations visible at the point of definition.
- **Phase 2 (at instantiation):** **dependent names** are looked up using both ordinary lookup *from the template definition context* and **ADL from the instantiation context**.

Consequences and gotchas:
- A **dependent type name** requires the `typename` disambiguator: `typename T::value_type x;` — without it the compiler assumes `T::value_type` is a value, not a type. (C++20 removed the need for `typename` in many "type-only" contexts, e.g. trailing return types, `new` type-ids, member declarations.)
- A **dependent member template** requires the `template` disambiguator: `obj.template get<0>()`, `T::template rebind<U>`.
- In a class template deriving from a dependent base, base members are **not** found by unqualified lookup. Use `this->member`, `Base<T>::member`, or a `using` declaration — otherwise phase-1 lookup fails to find them.
- Phase-1 errors (independent of template args) can be diagnosed before any instantiation; this is why a never-instantiated template with a non-dependent error can still be ill-formed, **NDR** (no diagnostic required) in some cases.

Each distinct set of template arguments produces one instantiation; the linker merges duplicates across TUs (COMDAT folding). Control instantiation explicitly with `extern template` (suppress) / `template class Foo<int>;` (force) to cut compile time.

### Argument-dependent lookup (ADL) & niebloids

ADL: for an unqualified call `f(args)`, the compiler also searches the namespaces and *associated classes/enums* of the argument types. This is what makes `std::swap(a,b)` idioms and `begin(c)`/`end(c)` work, and why `operator<<` for a user type need not be qualified. Gotcha: ADL can pull in unintended overloads; it does **not** apply when the name to the left of `(` was found by ordinary lookup to be a *block-scope* declaration or is a non-function (e.g. a variable/object).

**Niebloids / customization point objects (C++20 Ranges):** `std::ranges::find`, `std::ranges::begin`, etc. are **function objects** (not functions). Because the name resolves to an *object*, ADL is *disabled* at the call site — so a stray `using namespace` or an ADL-found `find` in user code cannot hijack the algorithm. CPOs additionally implement a controlled, concept-constrained customization protocol internally (they call user `begin()`/member `begin()` via ADL *inside* the object). "Niebloid" = the colloquial name for the ADL-inhibiting algorithm objects; "CPO" = the customizable subset (Eric Niebler's design). Pre-C++20 idiom for the same goal: `using std::swap; swap(a,b);` (the "two-step").

### Specialization (full & partial)

- **Full (explicit) specialization** — `template<> struct hash<MyType> { ... };`. Allowed for class, function (discouraged — prefer overloading; see below), and variable templates. Must appear after the primary template and before first use.
- **Partial specialization** — class and variable templates only (**not** function templates). `template<class T> struct X<T*> {...};`. The most-specialized matching partial spec is chosen via partial ordering.
- **Function templates cannot be partially specialized.** Use overloading or tag dispatch instead. Pitfall (Sutter, GotW #49): adding a full specialization of a function template does **not** participate in overload resolution the way you expect — overloads are chosen first, then the most-specialized template's specialization is used; an explicit specialization can be "hidden" by a better-matching overload. Prefer overloads.

### Template argument deduction, forwarding references & reference collapsing

- **Forwarding (universal) reference**: `template<class T> void f(T&& x)` — *only* when `T` is a deduced template parameter of that function (Meyers's term). `const T&&`, `vector<T>&&`, or `T&&` where `T` is a class template param (not deduced here) are plain rvalue refs, **not** forwarding refs.
- **Reference collapsing** (the engine behind it): `& &`→`&`, `& &&`→`&`, `&& &`→`&`, `&& &&`→`&&`. So an lvalue arg deduces `T=U&`, collapsing `T&&`→`U&`.
- **`std::forward<T>(x)`** = conditional cast preserving value category; **`std::move(x)`** = unconditional cast to xvalue. Rule of thumb: `move` an rvalue ref, `forward` a forwarding ref. Forward exactly once, at the point of use.
- **Perfect forwarding gotchas:** braced-init-lists (`{1,2,3}`) can't be deduced through `T&&` (non-deduced context); `0`/`NULL` deduce as `int` not pointer (use `nullptr`); bitfields and overloaded-function names don't forward cleanly; forwarding `const`/volatile and rvalue-ness through to the wrong constructor can shadow copy/move ctors — constrain "perfect-forwarding constructors" (e.g. `requires (!std::same_as<std::remove_cvref_t<T>, Self>)`) to avoid greedily out-competing the copy ctor.

### Value categories (the type-system substrate)

Since C++11/17 every expression has a *type* **and** a *value category*:
- **glvalue** = has identity (an addressable object/function). Splits into **lvalue** (e.g. named variable, `*p`) and **xvalue** (expiring: `std::move(x)`, a function returning `T&&`, materialized temporaries).
- **prvalue** = "pure rvalue": initializes an object / yields the value of an operator (`42`, `a+b`, a function returning `T` by value).
- **rvalue** = prvalue ∪ xvalue.

**C++17 guaranteed copy elision / temporary materialization:** a prvalue no longer denotes a temporary object — it is "the recipe for initializing one." A temporary is *materialized* (prvalue→xvalue) only when needed (bind a reference, access a member, etc.). Result: `T x = T(T(T()));` and returning a prvalue (`return Widget{};`) require **no** copy/move ctor at all (RVO is mandatory for prvalues). NRVO (named local return) remains a non-mandatory optimization. `decltype(expr)` encodes category: `T` for prvalue, `T&` for lvalue, `T&&` for xvalue — and `decltype((x))` (extra parens) on a name yields a *reference* type.

### SFINAE & `std::enable_if`

**SFINAE** ("Substitution Failure Is Not An Error"): during overload resolution, an ill-formed *type/expression in the immediate context* of a function template's signature removes that candidate silently rather than erroring. Failures in the function *body* are hard errors, **not** SFINAE.

- `std::enable_if<B, T>::type` / `std::enable_if_t<B,T>` (C++14) — present only when `B` is true. Apply in: return type, a defaulted non-type template param (`template<class T, std::enable_if_t<cond,int> = 0>` — the most robust form, since it doesn't change the signature collisions), or a function-parameter default.
- **Expression SFINAE** via `decltype(...)` + `std::declval<T>()`: `template<class T> auto f(T t) -> decltype(t.foo(), void());`.
- Gotchas: two overloads differing *only* in `enable_if` on the *return type* or on a type-param default can be redeclarations (ODR clash) — vary the dummy non-type param value/position. SFINAE is verbose, gives poor diagnostics, and is largely superseded by **concepts** in C++20.

### Concepts & `requires` (C++20)

Concepts are named, composable boolean predicates on template params; they replace most SFINAE, improve diagnostics, and participate in **partial ordering** (a more-constrained overload is preferred).

```cpp
template<class T> concept Addable = requires(T a, T b) { { a + b } -> std::same_as<T>; };
```

- **`requires` *expression*** (a bool prvalue): clauses are *simple* (`a+b;` — must be valid), *type* (`typename T::value_type;`), *compound* (`{ expr } noexcept -> Concept;`), and *nested* (`requires Other<T>;`). Local params have no storage/lifetime — they're notation only.
- **`requires` *clause*** constrains a template: `template<class T> requires Addable<T> void f(T);` or trailing: `void f(T) requires Addable<T>;`. Combine with `&&`/`||` (constraint conjunction/disjunction — short-circuit and subsumption-aware) and `!`. Atomic constraints are compared by *identity* for subsumption (gotcha: `sizeof(T)>4` in two places are distinct atomic constraints and don't subsume).
- **Constrained `auto` / abbreviated function templates:** `void f(Addable auto x);` and `Addable auto g();`. `std::integral auto n = ...;`.
- Standard concepts live in `<concepts>` (`std::same_as`, `convertible_to`, `integral`, `invocable`, `derived_from`, ...) and `<iterator>`/`<ranges>`. Prefer them over hand-rolled traits.

### Type traits (`<type_traits>`)

Compile-time queries/transformations. Categories: primary/composite categories (`is_integral`, `is_class`, `is_pointer`), properties (`is_const`, `is_trivially_copyable`, `is_nothrow_move_constructible`), relationships (`is_same`, `is_base_of`, `is_convertible`, `is_invocable` C++17), and transformations (`remove_reference`, `decay`, `conditional`, `enable_if`, `remove_cvref` C++20, `add_pointer`, `underlying_type`, `common_type`). Idioms:
- `_t` aliases (C++14) and `_v` variable templates (C++17): `std::decay_t<T>`, `std::is_same_v<A,B>`.
- `std::true_type`/`std::false_type` and the `::value` / `::type` member convention (inherit from `std::integral_constant`).
- `std::void_t<...>` (C++17) is the linchpin of the **detection idiom**.
- Many traits are **compiler intrinsics** (e.g. `is_trivially_constructible`) — not expressible in pure C++.

### The detection idiom

A SFINAE-friendly way to ask "is expression E valid for type T?" using `void_t` + partial specialization:

```cpp
template<class, template<class...> class, class...> struct detector : std::false_type {};
template<template<class...> class Op, class... Args>
struct detector<std::void_t<Op<Args...>>, Op, Args...> : std::true_type {};
template<template<class...> class Op, class... Args>
using is_detected = detector<void, Op, Args...>;
// Op example: template<class T> using has_size = decltype(std::declval<T>().size());
```

Standardized (experimentally) in LFTS v2 as `std::experimental::is_detected`/`detected_t`/`detected_or` (never adopted into the IS — superseded by concepts). In C++20, write a `requires`-expression instead.

### Tag dispatch & CRTP

- **Tag dispatch:** select an overload via an empty tag type computed from a trait — the canonical pre-concepts way to branch on properties (e.g. `std::advance` dispatches on `iterator_category` tags like `std::random_access_iterator_tag`). Pattern: a public `f(T)` forwards to `f_impl(T, category_tag{})`. Modern alternative: `if constexpr` (C++17) or concepts.
- **CRTP (Curiously Recurring Template Pattern)** — *static polymorphism*: `struct D : Base<D> {...};`. `Base` casts `static_cast<Derived*>(this)` to call derived methods with **zero virtual-call overhead**, enabling mixins, `enable_shared_from_this`, and the Barton–Nackman trick. Gotchas: `this`-downcast is only valid once `D` is complete (don't call derived members from `Base`'s constructor); CRTP bases are distinct types per derived class (no common base for heterogeneous storage). C++23's "deducing `this`" (explicit object parameter, `auto&& self`) supersedes much CRTP boilerplate.

### Variadic templates, parameter packs & fold expressions

- **Parameter packs:** `template<class... Ts>`, `Ts... args`. `sizeof...(Ts)` gives the count. Expand with the `...` postfix in a *pack-expansion pattern*: `f(args...)`, `g(h(args)...)`, `std::tuple<Ts...>`, base lists, capture lists, `[args...]`.
- **Fold expressions (C++17)** collapse a pack with a binary operator — replacing recursive instantiation:
  - Unary right: `(args op ...)` → `a1 op (a2 op (... op aN))`
  - Unary left: `(... op args)` → `((a1 op a2) op ...) op aN`
  - Binary: `(args op ... op init)` / `(init op ... op args)`.
  - **Empty-pack rule:** a *unary* fold over an empty pack is only well-formed for `&&` (→ `true`), `||` (→ `false`), and `,` (→ `void()`); any other operator on an empty pack is ill-formed — use a *binary* fold with an identity element instead. Common idioms: `(... && preds)`, `(std::cout << ... << args)`, `(sum += args, ...)`, `(v.push_back(args), ...)`.
- Pre-C++17 recursion (head/tail with a base-case overload) and `std::initializer_list` "expander" tricks are legacy alternatives.

### CTAD — class template argument deduction (C++17)

`std::pair p{1, 2.0};`, `std::vector v{1,2,3};` — deduce class template args from the constructor call. Mechanism: the compiler forms **implicit deduction guides** from constructors (each ctor → a function template returning the class) plus any **user-defined deduction guides**:

```cpp
template<class It> vector(It,It) -> vector<typename std::iterator_traits<It>::value_type>;
```

- C++20 adds **deduction from aggregates** (auto-generated guides) and CTAD for **alias templates**.
- Gotchas: CTAD applies only when *no* template args are given (partial specification disables it); copy-vs-init can surprise (`std::vector v2{v1};` may wrap vs copy depending on guides); not all libraries shipped guides in early C++17. Deduction guides are declarations only (no body), can be `explicit`, and live in the class's namespace.

### `constexpr` / `consteval` / `constinit` & compile-time programming

- **`constexpr` (C++11, expanded C++14/17/20/23):** "may run at compile time *if* called in a constant-expression context, else at runtime." Applies to functions, variables, ctors. C++14 allowed loops/locals/branches; C++17 added `if constexpr` and `constexpr` lambdas; C++20 allowed `constexpr` allocation/`new`/`virtual`/`try`, `std::vector`/`std::string` in constant evaluation; C++23 relaxed further (`constexpr` `cmath`, `static constexpr` in `constexpr` fns, non-literal locals).
- **`if constexpr` (C++17):** the *discarded* branch is not instantiated (but must still be syntactically/grammatically valid and not be ill-formed for *all* instantiations). Replaces tag dispatch / SFINAE for branching. Note: `static_assert(false)` in a discarded branch was ill-formed NDR before C++23's "`static_assert(false)` in templates" fix (P2593) — guard with a dependent `false`.
- **`consteval` (C++20):** *immediate function* — **every** call must produce a constant expression; cannot run at runtime (you cannot take its address generally). Use for compile-time-only factories (e.g. format-string checking). C++23/20 `std::is_constant_evaluated()` lets a `constexpr` fn branch on the context, but it is **always** treated as evaluating non-trivially inside a `consteval`/manifestly-constant context — a classic footgun (`if (std::is_constant_evaluated())` is fine; `if constexpr (std::is_constant_evaluated())` is *always true* and wrong). C++23 adds `if consteval` to do this safely.
- **`constinit` (C++20):** forces **constant (static) initialization** of a static/thread-local variable — guarantees it's initialized at compile time, eliminating the **static initialization order fiasco** for that variable. Does **not** imply `const` (the object may still be mutated at runtime); it constrains *when* init happens, not mutability.

### Common metaprogramming gotchas (quick hits)

- **Most vexing parse:** `Widget w();` declares a function; use `{}`.
- **Dependent base members invisible** without `this->` (two-phase lookup).
- **`typename`/`template` disambiguators** required on dependent names (relaxed in C++20 for type-only contexts).
- **Specializing function templates** is a trap — overload instead.
- **Adding a specialization in `std`** is UB except where explicitly permitted (e.g. `std::hash` for your own type, satisfying the requirements).
- **ODR violations** from inconsistent template definitions across TUs are IFNDR (ill-formed, no diagnostic required).
- **`std::initializer_list` hijacking** uniform-init ctors (e.g. `vector<int>{10}` = one element `10`, not size 10).
- **Greedy forwarding constructors** out-competing copy/move — constrain them.
- **Excessive recursive instantiation** blows up compile time/memory; prefer fold expressions, `if constexpr`, and pack-index tricks.
- **`auto` + braces:** `auto x{1};` is `int` (C++17 fixed the single-element case; multiple still ill-formed), `auto x = {1};` is `std::initializer_list<int>`.

### Sources

- cppreference.com — *Templates*, *Constraints and concepts*, *requires expression*, *Fold expressions*, *Value categories*, *Class template argument deduction*, *consteval/constinit specifiers*, *Dependent names / two-phase lookup* (https://en.cppreference.com/w/cpp/language)
- D. Vandevoorde, N. M. Josuttis, D. Gregor — *C++ Templates: The Complete Guide*, 2nd ed. (Addison-Wesley) — deduction, SFINAE, value categories, perfect forwarding, fold expressions, concepts
- ISO/IEC — *Working Draft, Standard for Programming Language C++* (WG21), and feature papers P0135R1 (guaranteed copy elision), P2593 (`static_assert(false)`) (https://www.open-std.org/jtc1/sc22/wg21/)
- B. Stroustrup, H. Sutter — *C++ Core Guidelines* (T.* templates/concepts sections) (https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines)
- S. Meyers — *Effective Modern C++* (Items 1–6, 23–30: deduction, forwarding refs, `std::move`/`std::forward`, value categories) (O'Reilly)
