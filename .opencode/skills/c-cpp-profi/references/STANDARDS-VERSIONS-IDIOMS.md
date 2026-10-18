# Standards, Versions, Idioms & Canon — C89→C23, C++98→C++26

> Per-version feature maps for C and C++, the idiom/technique catalog, and the foundational rules an expert never violates. Pair with [TOOLCHAIN-MATRIX.md](TOOLCHAIN-MATRIX.md) (-std flags), [CODE-TRANSFORM.md](CODE-TRANSFORM.md) (modernize), and [QUALITY-GATES.md](QUALITY-GATES.md). Each major area cites authoritative sources (ISO/WG14/WG21, cppreference, C++ Core Guidelines, Meyers, CERT, MISRA).


## C Standards & Versions: C89/C90 → C23

The single most reliable version discriminator is the predefined macro `__STDC_VERSION__` (`long`, decimal `yyyymmL`). It is **absent in C89/C90** (use `__STDC__ == 1` only). Treat the values below as ground truth for `#if`-based feature gating.

| Standard | Common name | ISO doc | `__STDC_VERSION__` | Notes |
|---|---|---|---|---|
| ANSI X3.159-1989 / ISO/IEC 9899:1990 | C89 / C90 | — | *(undefined)* | Same language; ANSI then ISO. Only `__STDC__` is defined. |
| ISO/IEC 9899:1990/AMD1:1995 | C95 / NA1 | Amendment 1 | `199409L` | First version to define `__STDC_VERSION__`; year≠value. |
| ISO/IEC 9899:1999 | C99 | — | `199901L` | Major revision. |
| ISO/IEC 9899:2011 | C11 | N1570 (final draft) | `201112L` | Concurrency, `_Generic`, optional Annex K. |
| ISO/IEC 9899:2018 | C17 / C18 | — | `201710L` | Bug-fix only; no new features. |
| ISO/IEC 9899:2024 | C23 (a.k.a. C2x) | N3220 (closest free draft), N3096 | `202311L` | Published 2024-10-31; value keeps `2311` despite 2024 publication, mirroring the C95 year mismatch. |

### C89/C90 (the baseline)
Function prototypes, `void`, `void*`, `const`/`volatile`, `enum`, `signed`, standard library partitioning, `<float.h>`/`<limits.h>`, the standard preprocessor (`#`/`##`, `defined`). This is the "lowest common denominator" target. Gotchas the agent must respect when constrained to C90: **no `//` comments**, **declarations must precede statements in a block**, **no mixing declarations and code**, **implicit `int`** and **implicit function declaration** are *legal*, and trailing comma in enums is illegal. K&R (non-prototype) function definitions are still valid here.

### C95 (Amendment 1 / Normative Addendum 1)
Mostly library: `<wchar.h>` (wide-char I/O and string analogues), `<wctype.h>`, the `wchar_t`/`wint_t` ecosystem and multibyte conversion; `<iso646.h>` macros (`and`, `or`, `not`, `bitand`, `xor`, `compl`, …) and the **digraphs** `<:` `:>` `<%` `%>` `%:` `%:%:` (distinct from C89 *trigraphs*). `__STDC_VERSION__ == 199409L` first appears here.

### C99 — the second pillar
- **`//` line comments** (borrowed from C++).
- **`inline`** function specifier (note: C's inline semantics differ from C++ — an `extern inline` / "no external definition unless an `extern` declaration exists" model; a non-`static` `inline` function needs exactly one external definition in some TU).
- **`restrict`** pointer qualifier — promise of no aliasing; **UB if violated**, but enables aggressive optimization (load/store reordering, FORTRAN-parity).
- **VLAs** (variable-length arrays) — runtime-sized automatic arrays and VM (variably-modified) types. Major footgun: unchecked stack allocation → stack overflow; made **optional in C11** (`__STDC_NO_VLA__`).
- **Designated initializers** — `struct P p = { .x = 1, .y = 2 };` and array `int a[] = {[3]=1,[7]=2};`. (Note: out-of-order/`.field`/range `[a...b]` ranges are a *GCC extension*, not standard.)
- **Compound literals** — `(struct P){1,2}`, `(int[]){1,2,3}`; an unnamed lvalue with automatic (or static at file scope) storage duration.
- **`_Bool`** + `<stdbool.h>` exposing `bool`/`true`/`false` macros.
- **`long long`** (≥64-bit) and `<stdint.h>` / `<inttypes.h>` fixed-width types (`int32_t`, `uintptr_t`, `INTMAX_C`, `PRId64`).
- **Flexible array members** — `struct S { size_t n; T data[]; };` (last member, incomplete array). Replaces the non-portable "struct hack" (`data[1]`/`data[0]`).
- **`<complex.h>`** + `_Complex`/`_Imaginary`, **`<tgmath.h>`** type-generic math, **`<fenv.h>`**.
- `__func__` predefined identifier; variadic macros (`__VA_ARGS__`); `snprintf`; `long double`/`hexadecimal float` literals (`0x1.8p3`); mixed declarations and code; `for (int i=...)` loop-scope declarations.

### C11 — concurrency, generics, safety
- **`_Generic`** — compile-time type-based selection; the engine behind type-generic macros (e.g. emulating overloading, `<tgmath.h>`-style dispatch).
- **`_Static_assert(cond, "msg")`** — compile-time assertion.
- **`_Noreturn`** function specifier + `<stdnoreturn.h>` (`noreturn`).
- **`_Alignas` / `_Alignof`** + `<stdalign.h>` (`alignas`/`alignof`), and `aligned_alloc`.
- **Anonymous `struct`/`union`** members.
- **`<stdatomic.h>`** — `_Atomic`, `atomic_*`, memory orders. **Optional**: guarded by `__STDC_NO_ATOMICS__`.
- **`<threads.h>`** — `thrd_*`, `mtx_*`, `cnd_*`, `tss_*`, `_Thread_local`/`thread_local`. **Optional**: guarded by `__STDC_NO_THREADS__` (widely unimplemented; glibc only since 2.28, MSVC much later — most code still uses pthreads/Win32/C++ threads).
- **Unicode**: `char16_t`/`char32_t` via `<uchar.h>`; `u"..."`, `U"..."`, `u8"..."` (the latter yields `char[]` in C11/C17).
- **Annex K bounds-checked interfaces** (`*_s` like `strcpy_s`, `gets_s`): **optional**, gated by `__STDC_LIB_EXT1__` and requires `#define __STDC_WANT_LIB_EXT1__ 1` before headers. Widely criticized (WG14 N1967) and **not implemented by glibc/most Unix libcs**; MSVC's `_s` functions are *similar but non-conforming*. Do not assume availability.
- **`gets` removed entirely** (was deprecated by C99 TC3). Anonymous-aggregate and static-assert support; `quick_exit`; `_Generic`-based `<tgmath.h>` cleanup.

### C17 / C18 — defect-fix release only
**No new language or library features.** It integrates Technical Corrigenda / Defect Reports against C11 (e.g., clarifications around `_Atomic`, sequencing, and other DR resolutions). `__STDC_VERSION__ == 201710L`. Practical guidance: targeting C17 ≈ targeting "clean C11"; pick C17 over C11 when the toolchain supports it for the corrigenda.

### C23 — the modernization wave (`202311L`)
**New keywords (clean spellings now first-class, underscore forms retained for compatibility):**
- **`bool`, `true`, `false`** are now real keywords/predefined constants; `_Bool` retained. `<stdbool.h>` becomes effectively empty/legacy.
- **`static_assert`, `thread_local`, `alignas`, `alignof`** are now keywords (the `<stdalign.h>`/`<stdnoreturn.h>`/`<assert.h>` macros become redundant).
- **`nullptr`** with type **`nullptr_t`** (`<stddef.h>`) — a true null-pointer constant, type-safe vs integer `0`/`NULL`.
- **`typeof` / `typeof_unqual`** — standardized (drops top-level qualifiers/atomic in the `_unqual` form); arrays/functions don't decay (like `sizeof`).
- **`constexpr`** — **objects only** (not functions, unlike C++): true compile-time constants usable where ICEs are required. Must be initialized per static-initialization rules.
- **`_BitInt(N)` / `unsigned _BitInt(N)`** — bit-precise integers of exact width N (e.g. `_BitInt(128)`, `_BitInt(3)`); defined two's-complement wraparound semantics for unsigned, distinct from `intN_t`.

**Literals / preprocessor:**
- **Binary literals** `0b1010` / `0B...`; **digit separators** with `'` (e.g. `1'000'000`, `0x1'F4`).
- **`#embed`** — embed binary/resource files as an initializer list of bytes (with parameters like `limit(...)`, `if_empty(...)`, `prefix(...)`, `suffix(...)`).
- New preprocessing: **`#elifdef` / `#elifndef`**, **`#warning`**, **`__has_include`**, **`__has_embed`**, **`__has_c_attribute`**, and **`__VA_OPT__`** (variadic-macro empty handling, ported from C++20).
- **`u8"..."` now yields `char8_t`** array (C23 adds `char8_t`); UTF-8 character constant `u8'x'`.
- **`_Decimal32/64/128`** decimal floating point (Annex; implementation-conditional).

**`[[attributes]]`** (C++11-style): standard set **`[[nodiscard]]`** (optionally `[[nodiscard("reason")]]`), **`[[maybe_unused]]`**, **`[[deprecated]]`**, **`[[fallthrough]]`**, **`[[noreturn]]`**, **`[[unsequenced]]`**, **`[[reproducible]]`**. Unknown attributes are ignorable by design.

**Other language changes / quality-of-life:** `{}` empty-braces zero-initialization for any object; unnamed function parameters allowed; labels may precede declarations and appear at end of compound statements; `enum` with explicit fixed underlying type (`enum E : unsigned char { ... }`); `auto` may be used for type inference (limited, C-flavored); `constexpr`-compatible compound literals with storage-class specifiers.

**Removals / breaking changes in C23 (critical when modernizing legacy code):**
- **K&R function definitions and declarations removed** — old-style `int f(a, b) int a; int b; {…}` is no longer valid.
- **`()` in a function declaration now means `(void)`** (no parameters), *not* "unspecified arguments". This silently changes semantics of legacy `int f();` declarations — a real migration hazard.
- **Implicit `int` and implicit function declarations** are gone (already errors in practice; now formally removed).
- **Trigraphs removed**; **`*_s`/Annex K** clarified but still optional; non-two's-complement signed representations no longer permitted (two's complement mandated).

### `-std=` flags, GNU dialects, and feature-test
- GCC/Clang accept: `-std=c89`/`c90`/`iso9899:1990`, `-std=iso9899:199409` (C95), `-std=c99`, `-std=c11`, `-std=c17`/`c18`, `-std=c23` (use `-std=c2x` on older GCC ≤12 / Clang ≤17). MSVC: `/std:c11`, `/std:c17`, `/std:clatest` (no `/std:c99` — MSVC went straight to C11/C17; C23 support is partial).
- **`gnu*` vs strict `c*`**: `-std=gnuNN` enables GNU extensions (statement expressions `({...})`, `typeof` pre-C23, `__attribute__`, case ranges `case 1...5:`, zero-length arrays, binary literals pre-C23, anonymous structs pre-C11, `__builtin_*`). **`gnu17` is GCC's historical default**; recent GCC 15 defaults to **`gnu23`** (Clang 19+ similarly moved toward c23 defaults). Use `-std=cNN -pedantic`/`-pedantic-errors` to enforce strict ISO and flush out extensions.
- **Feature detection idiom**: gate on `__STDC_VERSION__ >= 201112L`, then refine with capability macros: `__STDC_NO_THREADS__`, `__STDC_NO_ATOMICS__`, `__STDC_NO_VLA__`, `__STDC_NO_COMPLEX__`, `__STDC_LIB_EXT1__` (+ `__STDC_WANT_LIB_EXT1__`), `__STDC_HOSTED__`, `__STDC_IEC_559__`/`__STDC_IEC_60559_BFP__`. For C23 toolchain probing prefer `__has_include`, `__has_c_attribute`, `__has_embed`.

### Targeting guidance (what to compile as, and why)
- **C17 (`-std=c17`) is the safest broad default** today: full C11 feature set plus defect fixes, universally supported by GCC/Clang/MSVC. Choose it for portable libraries that must run everywhere.
- **C11** only if you must support an older toolchain that lacks C17; behavior is otherwise equivalent.
- **C23 (`-std=c23`/`c2x`)** for greenfield code on current GCC ≥14/Clang ≥18: prefer `nullptr`, real `bool`/`static_assert`/keywords, `[[nodiscard]]`/`[[fallthrough]]`, `_BitInt`, `#embed`, `constexpr` objects, `enum` with fixed type. Verify each feature against your minimum compiler; C23 library support lags language support.
- **C99** remains relevant for embedded/older vendor toolchains; it's the floor for designated initializers, compound literals, `<stdint.h>`, and `//` comments. Avoid VLAs in safety-critical code (MISRA C and the Linux kernel ban them).
- **C89/C90** only for maximally portable code or constrained legacy/embedded compilers; expect to forgo nearly all of the above and to write declarations-before-statements.
- **Migration red flags** when reading legacy → modern: empty-paren `()` declarations (semantics change in C23), reliance on implicit `int`/implicit declarations, K&R definitions, MSVC-only `_s` functions, GNU extensions assumed under a strict `cNN` build, and VLA-based APIs.

### Sources
- WG14, *ISO/IEC 9899:2024 (C23) working draft* N3220 (and N3096), open-std.org — https://www.open-std.org/jtc1/sc22/wg14/www/docs/n3220.pdf
- WG14, *ISO/IEC 9899:2011 (C11) final draft* N1570, open-std.org — https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf
- cppreference.com, *C language history* and per-standard pages (C95/C99/C11/C17/C23) — https://en.cppreference.com/w/c/language/history
- Wikipedia, *C23 (C standard revision)* and *C11 (C standard revision)* — https://en.wikipedia.org/wiki/C23_(C_standard_revision)
- WG14, N1967 *Field Experience With Annex K — Bounds Checking Interfaces* — https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1967.htm

## C++ Standards & Versions: C++98 → C++26

ISO/IEC 14882 editions and their `-std=` flags. Each edition is the union of *core language* and *library* changes; "feature-test macros" (`__cpp_*` in `<version>` since C++20) are the portable way to detect individual features at compile time. Mental model for transforming real code: **identify the minimum standard a construct requires, the compiler version that ships it, and the safer/idiomatic replacement for anything deprecated or removed.**

### Quick `-std=` / `__cplusplus` map

| Edition | ISO doc | `__cplusplus` | GCC flag | Clang flag | MSVC flag |
|---|---|---|---|---|---|
| C++98 | 14882:1998 | `199711L` | `-std=c++98` | `-std=c++98` | (default, legacy) |
| C++03 | 14882:2003 | `199711L` (unchanged) | `-std=c++03` | `-std=c++03` | — |
| C++11 | 14882:2011 | `201103L` | `-std=c++11` (was `c++0x`) | `-std=c++11` | implicit ≥VS2015 |
| C++14 | 14882:2014 | `201402L` | `-std=c++14` (was `c++1y`) | `-std=c++14` | `/std:c++14` |
| C++17 | 14882:2017 | `201703L` | `-std=c++17` (was `c++1z`) | `-std=c++17` | `/std:c++17` |
| C++20 | 14882:2020 | `202002L` | `-std=c++20` (was `c++2a`) | `-std=c++20` | `/std:c++20` |
| C++23 | 14882:2024 | `202302L` | `-std=c++23` (was `c++2b`) | `-std=c++23` | `/std:c++23preview`→`/std:c++latest` |
| C++26 | 14882 (final ballot London, Mar 2026) | `202400L`+ (TBD) | `-std=c++26`/`c++2c` | `-std=c++26`/`c++2c` | `/std:c++latest` |

Note: MSVC under `/std:c++14`/`/std:c++17` still reports `__cplusplus == 199711L` unless `/Zc:__cplusplus` is passed — a classic detection gotcha. Use `_MSVC_LANG` on MSVC instead.

### C++98 / C++03

- **C++98** is the first ISO C++: templates, exceptions, RTTI (`dynamic_cast`/`typeid`), namespaces, the STL (containers/iterators/algorithms), `std::string`, `iostream`, `bool`.
- **C++03** is a *defect-report* (TC1) revision — bug fixes only, no new features. The one behaviorally visible change: value-initialization semantics were tightened. Treat C++98 and C++03 as the same language for feature purposes.
- Era gotchas you will still see in legacy code: no move semantics (copies everywhere), `std::auto_ptr` (broken copy = transfer of ownership), `throw()` dynamic exception specifications, `0`/`NULL` for null pointers, manual functors instead of lambdas, `>>` parsing ambiguity in nested templates (`vector<vector<int>>` needed a space until C++11).

### C++11 — the "modern C++" inflection point

Core language:
- **Rvalue references & move semantics** (`T&&`): move ctor/assign; `std::move` (unconditional cast to rvalue), `std::forward` (perfect forwarding in templates). Foundation for the whole modern performance story.
- **`auto`** type deduction and **trailing return types** (`auto f() -> T`); **`decltype`**.
- **Lambdas** (`[capture](params){body}`), with capture by value/reference, `mutable`.
- **Range-based `for`** (`for (auto& x : c)`), backed by `begin()`/`end()`.
- **Smart pointers** (library, but defining): `std::unique_ptr` (move-only owning), `std::shared_ptr`/`std::weak_ptr` (ref-counted). `std::make_shared` in C++11; **`std::make_unique` arrived in C++14**.
- **`constexpr`** (single-`return` functions only in C++11; relaxed in C++14).
- **Variadic templates** (`template<class... Ts>`), parameter packs, `sizeof...`.
- **`nullptr`** (`std::nullptr_t`) — type-safe null replacing `0`/`NULL`.
- **`enum class`** (scoped, strongly typed enums) with explicit underlying type.
- **`= default` / `= delete`**; the Rule of Five emerges (dtor, copy ctor/assign, move ctor/assign).
- **Uniform/brace initialization** `{}` and `std::initializer_list` (introduces the *narrowing* prohibition and the "most vexing"-avoiding init).
- **Threading & memory model**: `<thread>`, `<mutex>`, `<condition_variable>`, `<future>`/`std::async`, `<atomic>`, `thread_local`, and a formally specified memory model (`std::memory_order`).
- Other: `static_assert`, `noexcept`, `override`/`final`, raw string literals, `using` alias templates, `alignas`/`alignof`, delegating/inheriting constructors, `long long`, `char16_t`/`char32_t`.
- **Deprecated/removed here**: `std::auto_ptr` deprecated (removed in C++17); dynamic exception specs deprecated.

### C++14 — completion of C++11

- **Generic lambdas** (`[](auto x){}`).
- **Return type deduction for normal functions** (`auto f() { return ...; }`).
- **Variable templates** (`template<class T> constexpr T pi = ...;`).
- **Relaxed `constexpr`** (loops, locals, multiple statements).
- **`std::make_unique`** (filling the C++11 gap), `std::integer_sequence`, `std::shared_timed_mutex`.
- Binary literals `0b1010`, digit separators `1'000'000`, `[[deprecated]]` attribute.

### C++17

Core:
- **Structured bindings** (`auto [a, b] = pair;`).
- **`if constexpr`** — compile-time branch pruning; replaces much SFINAE/tag-dispatch.
- **Fold expressions** (`(... + args)`) over parameter packs.
- **Guaranteed copy elision** for prvalues (RVO is mandatory for prvalue init; enables truly non-movable return-by-value).
- **CTAD** (class template argument deduction): `std::pair p{1, 2.0};` — plus user *deduction guides*.
- **`inline` variables** — header-only definable globals/`static` members, fixing ODR pain.
- **`if`/`switch` with initializer** (`if (auto it = m.find(k); it != m.end())`).
- `constexpr` lambdas, `auto` non-type template params, nested namespace `a::b::c`, `[[nodiscard]]`/`[[maybe_unused]]`/`[[fallthrough]]`.
Library:
- **`std::optional`**, **`std::variant`**, **`std::any`**, **`std::string_view`** (non-owning; beware dangling on temporaries).
- **`<filesystem>`** (from Boost.Filesystem).
- **Parallel algorithms** — execution policies `std::execution::seq/par/par_unseq`.
- `std::byte`, `std::invoke`, `std::apply`, `std::clamp`, `std::reduce`, `std::scan`, `std::shared_mutex`, `std::from_chars`/`std::to_chars`.
- **Removed**: `std::auto_ptr`, `std::random_shuffle`, old `std::bind1st`/`bind2nd`, `std::unary_function`/`binary_function`, trigraphs, `register` storage class meaning, `throw(T)` dynamic exception specs (only `throw()` ≡ `noexcept` kept, then removed in C++20).

### C++20 — second large wave

Core:
- **Concepts** (`concept`, `requires`-clauses/expressions, constrained `auto`) — checked template constraints with clear diagnostics; standard concepts in `<concepts>`.
- **Modules** (`export module M;`, `import M;`) — replace textual `#include`; faster builds, real encapsulation. Implementation maturity varies widely (MSVC most complete early; GCC/Clang improved later).
- **Coroutines** (`co_await`, `co_yield`, `co_return`) — language machinery only; you supply/await library types (no general coroutine types until C++23 `std::generator`).
- **Ranges** (`<ranges>`): range algorithms (`std::ranges::sort`), views/adaptors (`views::filter`, `views::transform`, pipe `|`), projections.
- **`operator<=>`** (three-way "spaceship") with `<compare>`; `= default` synthesizes all six relational operators.
- **Designated initializers** (`T{.x = 1, .y = 2}`) — *must follow declaration order*, no nesting tricks like C.
- **`consteval`** (immediate functions, must run at compile time) and **`constinit`** (guaranteed constant initialization, attacks the static-init-order fiasco).
- `constexpr` greatly expanded (virtual calls, `try`/`catch`, `std::vector`/`std::string` in constant evaluation), `using enum`, `[[likely]]`/`[[unlikely]]`, `[[no_unique_address]]`, abbreviated function templates (`void f(auto x)`), aggregate init with parens.
Library:
- **`std::format`** (`<format>`) — type-safe, Python-style formatting (replacing `printf`/`iostream` ceremony).
- **`std::span`** (`<span>`) — non-owning contiguous view (ptr+size).
- **Calendar & time zones** in `<chrono>`.
- **`std::jthread`** (auto-joining, `std::stop_token` cooperative cancellation), `std::atomic_ref`, `std::atomic<shared_ptr>`, `<semaphore>`, `<latch>`, `<barrier>`, `std::counting_semaphore`.
- `std::bit_cast`, `<bit>` ops (`std::popcount`, `std::endian`), `std::source_location`, `std::ssize`, `starts_with`/`ends_with` on string(_view).
- **Removed/deprecated**: `throw()` removed; `std::is_pod` deprecated (use `is_trivial`+`is_standard_layout`); comparison operators for some types deprecated when `<=>` supersedes.

### C++23

Core:
- **Deducing `this`** (explicit object parameter): `R f(this Self&& self, ...)` — deduplicates const/non-const/ref overloads, enables recursive lambdas and CRTP simplification.
- **`if consteval`** — distinguishes constant-evaluation context (cleaner than `std::is_constant_evaluated()`, and required when calling `consteval` from `constexpr`).
- **Multidimensional subscript** `a[i, j, k]` (operator `[]` now variadic); **static `operator()`/`operator[]`**.
- **`auto(x)` / `auto{x}`** — explicit decay-copy.
- Preprocessor: `#elifdef`/`#elifndef`, `#warning`; **`#embed`** (binary resource inclusion, from C23); named UCN escapes `\N{...}`.
- `constexpr` extended further (e.g. `cmath` subset, non-literal vars in `constexpr` functions, `static constexpr` in `constexpr` functions).
Library:
- **`std::expected<T, E>`** (`<expected>`) — value-or-error vocabulary type with monadic `and_then`/`transform`/`or_else`; plus monadic ops added to `std::optional`.
- **`std::print` / `std::println`** (`<print>`) — `std::format`-backed direct stdout (faster + Unicode-correct vs `printf`).
- **`std::mdspan`** (`<mdspan>`) — non-owning N-D view with pluggable layout/accessor; zero-overhead.
- **`std::flat_map`/`std::flat_set`** — sorted-vector container adaptors (cache-friendly, slow insert).
- **`std::generator<T>`** (`<generator>`) — the first standard coroutine type (lazy synchronous range).
- `std::stacktrace`, `std::move_only_function`, `std::spanstream`, `std::byteswap`, `std::to_underlying`, `std::unreachable`, `std::ranges::to` (`r | std::ranges::to<std::vector>()`); ranges adaptors `views::zip`, `views::enumerate`, `views::chunk`, `views::slide`, `views::join_with`, `views::cartesian_product`; `std::ranges::contains`, `find_last`, `fold_left`.
- **Removed**: `std::aligned_storage`/`aligned_union` deprecated; garbage-collection support (`std::declare_reachable` etc.) removed; `std::unexpected` (old handler) gone (name reused for `std::unexpected<E>` in `<expected>`).

### C++26 — finalized; final-approval ballot London, March 2026

(Feature set frozen June 2025 Sofia; ballot comments resolved Nov 2025 Kona; final ballot March 2026 London. Verify exact wording against the working draft.)

Core:
- **Static reflection** (P2996) — value-based `^^` ("cat-ears") reflection operator producing `std::meta::info`, with `<meta>` and **`template for`** expansion statements (P1306); splicers `[: r :]`. Replaces the abandoned Reflection TS `reflexpr`.
- **Contracts** (P2900) — `pre`/`post` on functions and the `contract_assert` statement, with configurable evaluation semantics (ignore/observe/enforce); `<contracts>`.
- **Pack indexing** (`pack...[N]`), **structured bindings as a pack** and in conditions, **variadic friends**, `= delete("reason")` with a message.
- `[[indeterminate]]` attribute; erroneous behavior for reading uninitialized values (memory-safety hardening); `#embed`/`__has_embed` confirmed; placeholder (`_`) variables; more `constexpr` (placement-new, exceptions, `constexpr` containers, virtual inheritance).
Library:
- **`std::execution`** (P2300) — senders/receivers async model: schedulers, senders, receivers, `std::execution::task`, system parallel scheduler in `<execution>`.
- **`std::inplace_vector`** (`<inplace_vector>`) — fixed-capacity, no-heap dynamic-size vector.
- **`std::hive`** (`<hive>`, ex-`plf::colony`) — bucketed container with stable references and fast unordered insert/erase.
- **Hazard pointers** (`<hazard_pointer>`) and **RCU** (`<rcu>`) — lock-free reclamation primitives.
- **`<simd>`** (data-parallel types), **`<linalg>`** (BLAS-style on `mdspan`), **`<text_encoding>`**, **`<debugging>`** (`std::breakpoint`, `std::is_debugger_present`).
- Saturating arithmetic `std::saturating_add/sub/mul/div`; `std::copyable_function`; `std::is_within_lifetime`; `std::submdspan`.
- **Deprecated/removed**: `std::memory_order::consume` deprecated; `[[carries_dependency]]` removed. (Trivial relocatability did *not* make C++26 — deferred.)

### Cross-version compiler-support reality (verify on cppreference "Compiler support" tables)

- **C++11/14**: fully supported by GCC ≥ 5/6, Clang ≥ 3.4/3.4, MSVC ≥ VS2015/2017.
- **C++17**: **Clang ≥ 5**, **GCC ≥ 7–9** (core 7, library incl. parallel algos later), **MSVC ≥ VS2017 15.7** (`__cplusplus`/`_MSVC_LANG == 201703L`).
- **C++20**: **GCC ≥ 10** for `-std=c++20` (concepts in 10, much library by GCC 11–13), **Clang ≥ 10** core (concepts 10, ranges by Clang 15–16), **MSVC ≥ VS2019 16.8+/19.28**. *Modules* and *coroutines* are uneven: MSVC led on modules early; GCC/Clang modules matured later; coroutine ABI was aligned across GCC/Clang/MSVC for interop.
- **C++23**: GCC ≥ 13–14, Clang ≥ 16–17 advancing toward full library; MSVC via `/std:c++23preview` then `/std:c++latest`. `std::print`, `std::expected`, `std::mdspan`, `std::generator` land at differing minor versions — gate with `__cpp_lib_*` macros.
- **C++26**: in flight — GCC trunk (≈16) has reflection + contracts merged; Clang and MSVC tracking. Treat as bleeding-edge; always feature-test.

### Practical rules for an agent transforming code

- **Detect, don't assume**: branch on `__cplusplus`/`_MSVC_LANG` for dialect and on `__cpp_*`/`__cpp_lib_*` feature-test macros (and `#include <version>`) for individual features.
- **Modernization ladder** (behavior-preserving): `NULL`/`0` → `nullptr`; raw `new`/`delete` & owning raw pointers → `unique_ptr`/`make_unique`; functors → lambdas; `typedef` → `using`; `std::bind` → lambdas; `enum` → `enum class`; index loops → range-`for`/ranges; SFINAE/`enable_if` → concepts (C++20) or `if constexpr` (C++17); error-codes/out-params → `std::optional`/`std::expected`; `printf`/`stringstream` → `std::format`/`std::print`.
- **Known removals to flag in old code**: `std::auto_ptr` (→`unique_ptr`), `std::random_shuffle` (→`std::shuffle` with a URBG), dynamic exception specs `throw(...)`/`throw()` (→`noexcept`), `std::is_pod` (→`is_trivial && is_standard_layout`), `register`/trigraphs.
- **UB hotspots tied to versions**: dangling `string_view`/`span` over temporaries (C++17/20); coroutine frame lifetime & dangling captured references (C++20); designated-initializer reordering rejected (C++20, unlike C); reading uninitialized values is UB pre-C++26 (erroneous behavior from C++26 with `[[indeterminate]]` opt-out).

### Sources

1. cppreference.com — per-version feature pages (C++11/14/17/20/23/26) and "C++ compiler support" tables. https://en.cppreference.com/w/cpp/23 ; https://en.cppreference.com/w/cpp/compiler_support.html
2. ISO/IEC WG21 working draft and adopted papers (e.g. P2300 std::execution, P2900 Contracts, P2996 Reflection, P0847 deducing this, P0323 expected, P0009 mdspan); current C++ standard ISO/IEC 14882. https://www.open-std.org/jtc1/sc22/wg21/
3. C++ Core Guidelines — Stroustrup & Sutter (idiom/modernization guidance, Rule of Five, RAII). https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines
4. Scott Meyers, *Effective Modern C++* (C++11/14 move semantics, `auto`, smart pointers, special-member rules).
5. Bjarne Stroustrup, *A Tour of C++* (3rd ed.) and *The C++ Programming Language* (4th ed.) — language overview across editions.

## C++ Idioms & Techniques Catalog

Each entry: **mechanism** (one line) + **when-to-use** + **gotchas/version**. Treat this as a lookup table while reading or transforming code.

### Resource & special-member idioms

#### RAII (Resource Acquisition Is Initialization)
- **Mechanism:** bind a resource's lifetime to an automatic object; acquire in constructor, release in destructor; cleanup runs deterministically on scope exit including during stack unwinding.
- **When:** every owning resource — memory, file handles, locks, sockets, GPU buffers. The foundational C++ idiom; exception safety derives from it.
- **Gotchas:** destructors must not throw (since C++11 they are implicitly `noexcept`; a throwing dtor during unwinding calls `std::terminate`). Don't manage two resources in one constructor body (if the second throws, the first leaks) — wrap each in its own RAII member. Prefer `unique_ptr`/`lock_guard`/`scope_guard` over hand-rolled RAII.

#### Rule of Zero / Three / Five
- **Mechanism:** *Zero* — design classes to need no user-declared destructor/copy/move; let members (smart pointers, containers) handle resources. *Three* (C++98) — if you declare any of dtor/copy-ctor/copy-assign, declare all three. *Five* (C++11) — add move-ctor and move-assign.
- **When:** Rule of Zero is the default goal. Reach for Five only in a low-level RAII wrapper that directly owns a raw resource.
- **Gotchas:** declaring a destructor (even `= default`) **suppresses** implicit move operations and deprecates implicit copy — a silent performance/correctness trap (Core Guidelines C.21). `= default` a member to restore it. A user-declared move disables implicit copy entirely. `std::vector<T>` reallocation only uses `T`'s move ctor if it is `noexcept`; otherwise it copies — mark moves `noexcept`.

#### Copy-and-swap
- **Mechanism:** implement copy-assignment by taking the RHS **by value** (invoking copy/move ctor) then `swap`ing with `*this`; the temporary's destructor cleans up the old state.
- **When:** classes owning a raw resource where you want one assignment operator that is automatically strong-exception-safe and self-assignment-safe.
- **Gotchas:** provide a `noexcept` member `swap` and an ADL-findable free `swap`. Costs one extra allocation vs. a hand-tuned assignment; in hot paths a separate move-assign may be preferable. With C++11 a single by-value `operator=` handles both copy and move assignment (unified assignment).

### Compilation & interface idioms

#### PIMPL (compilation firewall / "Cheshire Cat")
- **Mechanism:** move all private data/members into a forward-declared `struct Impl` held via `unique_ptr<Impl>`; the header exposes only the public interface.
- **When:** stable ABI across versions, cutting compile-time coupling (header changes don't recompile clients), hiding implementation dependencies.
- **Gotchas:** the destructor must be declared in the header and **defined in the .cpp** where `Impl` is complete — otherwise `unique_ptr`'s default deleter tries to delete an incomplete type (ill-formed; GotW #100/#101). Adds a heap allocation + indirection per object. `const` methods don't propagate `const` through the pointer (use `propagate_const` from Library Fundamentals TS if needed). Copy must be hand-written (Impl is incomplete in header).

#### NVI (Non-Virtual Interface)
- **Mechanism:** public methods are non-virtual and call private/protected `virtual` hooks; base controls the invariant/pre/post-conditions, derived customizes only the steps.
- **When:** Template Method pattern; you want a stable public contract while allowing overriding of inner behavior.
- **Gotchas:** a base can call a private virtual in the derived class (access control and virtual dispatch are orthogonal). Prefer NVI over public virtuals (Sutter). Public virtual destructor or protected non-virtual destructor for base classes.

#### Named constructor / factory function
- **Mechanism:** make constructors private; expose `static` functions with descriptive names that return objects (by value, relying on copy elision).
- **When:** multiple construction modes with identical signatures, enforcing invariants before construction, returning derived types, or returning `optional`/`expected` on failure instead of throwing.
- **Gotchas:** factory returning `unique_ptr<Base>` is the polymorphic-factory norm. Can't be used where brace-init / aggregate construction is expected.

#### Builder
- **Mechanism:** accumulate configuration via chained setters (return `*this`/`&`), then `build()` produces the immutable object.
- **When:** objects with many optional parameters; replaces telescoping constructors and avoids the boolean-parameter-soup antipattern.
- **Gotchas:** for fluent chaining on rvalues, overload setters on `&&` to move out. Consider aggregate + designated initializers (C++20) as a lighter alternative.

### Static polymorphism & metaprogramming

#### CRTP (Curiously Recurring Template Pattern)
- **Mechanism:** `class Derived : public Base<Derived>`; base `static_cast`s `this` to `Derived*` to call derived methods — compile-time ("static") polymorphism with no vtable.
- **When:** mixins, static interfaces, `enable_shared_from_this`, operator-injection (`boost::operators`), avoiding virtual-call overhead in hot code.
- **Gotchas:** the cast is only valid because `Derived` is the real type — passing a different type as the parameter is UB. No runtime dispatch through `Base<Derived>*` across different `Derived`s (each instantiation is a distinct type). C++23 "deducing `this`" (explicit object parameter) replaces many CRTP uses with cleaner syntax.

#### Policy-based design
- **Mechanism:** parameterize a host class template on orthogonal "policy" template parameters (each a small class supplying one behavioral axis); compose behavior at compile time.
- **When:** libraries needing many orthogonal configuration axes without a combinatorial class explosion (Alexandrescu, *Modern C++ Design*; `std::allocator`, `char_traits`).
- **Gotchas:** code bloat from many instantiations; error messages are dense. Use EBO / `[[no_unique_address]]` for stateless policies to avoid size cost.

#### EBO (Empty Base Optimization)
- **Mechanism:** a base class of empty type (no non-static data members) may occupy zero bytes in the derived object; standard-permitted size optimization.
- **When:** storing stateless policies/comparators/allocators/deleters without paying for them (used in `std::vector`, `std::tuple`, compressed-pair patterns).
- **Gotchas:** doesn't apply if the empty base is also the type of the first data member (two same-type subobjects need distinct addresses). C++20 `[[no_unique_address]]` (see below) achieves the same as a **member** without inheritance — prefer it.

#### `[[no_unique_address]]` (C++20)
- **Mechanism:** marks a non-static data member as *potentially-overlapping*; if empty it can occupy zero bytes (like an empty base), and tail padding may be reused.
- **When:** member-based replacement for EBO/compressed-pair — store empty allocators, comparators, deleters as named members at zero cost.
- **Gotchas:** two `[[no_unique_address]]` members of the **same type** cannot share an address (must be distinct), so they don't both vanish. MSVC ABI honors it only under `[[msvc::no_unique_address]]` for back-compat reasons.

### Constraint mechanisms

#### SFINAE / `enable_if` vs Concepts
- **Mechanism:** SFINAE — "Substitution Failure Is Not An Error": an invalid type/expression in the immediate context of template argument substitution removes the candidate from overload resolution rather than erroring; `std::enable_if_t<cond, T>` conditionally enables overloads. **Concepts (C++20)** — named, composable boolean predicates on types (`requires`-expressions) that constrain templates with readable diagnostics and proper subsumption-based overload ordering.
- **When:** constrain templates / select overloads. Use **Concepts on C++20+**; reserve `enable_if`/`void_t` SFINAE for C++14/17 codebases.
- **Gotchas:** SFINAE only applies to the *immediate context* — an error deep inside an instantiated body is a hard error, not a substitution failure. Tag-dispatch is often cleaner than nested `enable_if`. Concepts subsume (a more-constrained overload wins) which `enable_if` cannot express. `requires requires` is a `requires`-clause introducing an ad-hoc `requires`-expression.

#### Detection idiom
- **Mechanism:** `void_t`-based metafunction (`is_detected<Op, Args...>`) that yields `true_type`/`false_type` based on whether `Op<Args...>` is a valid type, detecting member/operation existence.
- **When:** pre-C++20 "does type `T` have member `x` / support expression `e`?" introspection; a "stopgap for concepts."
- **Version:** `std::void_t` is C++17; `is_detected`/`detected_t`/`detected_or`/`nonesuch` live in **Library Fundamentals TS v2** (`std::experimental::`, `<experimental/type_traits>`), never voted into the IS. On C++20 use a concept / `requires`-expression instead.
- **Gotchas:** detecting member existence does not check accessibility nor return type unless you build that into the op alias.

### Type erasure & dispatch

#### Type erasure (`std::function` / `std::any` / manual)
- **Mechanism:** store any type satisfying a behavioral contract behind a fixed interface by erasing the concrete type — typically a heap-allocated concept/model pair (an internal abstract base + templated derived holder) accessed via virtual dispatch. `std::function<R(Args...)>` erases callables; `std::any` (C++17) erases any copyable type (recover with `any_cast`); `std::variant` (C++17) is the closed/value-based alternative.
- **When:** heterogeneous storage in one container, stable interface boundaries, plugin-style polymorphism without forcing a common base on user types (Sean Parent's "inheritance is the base class of evil" / runtime-polymorphism value semantics).
- **Gotchas:** typically heap-allocates (SBO mitigates small objects, see below). `std::function` requires the target be CopyConstructible (use `std::move_only_function`, C++23, for move-only). `std::any` strips all interface — only the exact stored type is recoverable. Manual type erasure with SBO gives value semantics + no allocation for small targets.

#### Tag dispatch
- **Mechanism:** select among overloads by passing an extra empty "tag" argument whose type encodes a trait/category; overload resolution picks the matching function.
- **When:** dispatch on iterator categories (`std::iterator_traits`), trait properties, or type categories — works in C++98 and reads better than `enable_if`.
- **Gotchas:** `if constexpr` (C++17) replaces many tag-dispatch chains with a single function. Tags must be related by inheritance for "best match" fallthrough (e.g. `forward_iterator_tag : input_iterator_tag`).

#### ADL customization points, niebloids & `tag_invoke`
- **Mechanism:** **ADL CPO** (customization point object) — a `constexpr` function object (e.g. `std::ranges::begin`) that finds user customizations via ADL but disables the "found-by-ADL of the unqualified name" trap and enforces concept constraints. **Niebloid** — a function-object that is *never* found by ADL (poisons the well), forcing qualified or CPO-style calls. **`tag_invoke`** (P1895, not standardized) — route all customizations through one ADL name `tag_invoke(tag, args...)`, distinguishing CPOs by passing the CPO as the first argument; customizations are usually **hidden friends**.
- **When:** library customization points with hijack-proof, constraint-checked lookup. `tag_invoke` is the de-facto pattern in sender/receiver libraries (libunifex) pre-`std::execution`.
- **Gotchas:** the classic `using std::swap; swap(a,b);` two-step is the pre-CPO idiom. C++26 `std::execution` (P2300) moved **away** from `tag_invoke` toward member-based / language CPOs. Define customizations as **hidden friends** to keep overload sets small and avoid leaking names into the namespace.

### Storage & layout tricks

#### Small-buffer / Small-object optimization (SBO/SSO)
- **Mechanism:** embed a fixed inline buffer in the object; store small payloads in-place and only heap-allocate when they exceed the buffer (a union of inline storage + pointer).
- **When:** `std::string` (SSO, typically 15–22 inline chars), `std::function`, `std::any`, custom type-erasure — avoid allocation for the common small case.
- **Gotchas:** SSO is *implementation-defined*, not standard-mandated (libstdc++/libc++/MSVC differ in capacity). Moving an SBO object can be non-trivial (must relocate inline data); a moved-from SSO string is in a valid-but-unspecified state. Inline buffer enlarges `sizeof`.

#### Expression templates
- **Mechanism:** operators return lightweight proxy types encoding the *expression tree* rather than computing; evaluation is fused/deferred at assignment, eliminating temporaries and enabling whole-expression optimization.
- **When:** numeric/linear-algebra libraries (Eigen, Blaze) — `a = b + c + d` becomes one fused loop, no intermediate vectors.
- **Gotchas:** `auto x = b + c;` captures a **proxy holding references** to `b`,`c` — dangling if operands are temporaries (the classic `auto` + expression-template lifetime bug). Huge compile times and inscrutable types. C++23 deducing-`this` and ranges lazy views cover some cases more safely.

#### `std::launder` (C++17)
- **Mechanism:** `<new>` function returning a pointer to the object actually occupying the storage, defeating the optimizer's assumption that a pointer keeps referring to the original object after placement-new replaced it in the same storage.
- **When:** after `new (p) T{...}` reuses storage that held an object with `const`/reference members or a vptr, and you must access the new object through the **old** pointer; obtaining a pointer to an object nested in a `char[]`/aligned-storage buffer.
- **Gotchas:** does **not** end/begin lifetimes or change the address — purely an optimization barrier on the *value* of the returned pointer. Needed only when const/reference members or polymorphic vtables make the old pointer "stale." Reachability rule: the new object must be reachable from the original via the same storage, same type (modulo cv on transparently-replaceable subobjects). Usually you should just keep the pointer returned by placement-new.

#### `std::bit_cast` (C++20)
- **Mechanism:** `<bit>` `constexpr` function reinterpreting the object representation of `From` as `To`, bit-for-bit, creating a new `To` object — the well-defined replacement for `memcpy`/`reinterpret_cast` type punning.
- **When:** type-pun between same-size trivially-copyable types (float↔uint bits, reading binary formats) without strict-aliasing UB; usable in constant expressions.
- **Gotchas:** requires `sizeof(To)==sizeof(From)` and both TriviallyCopyable (else removed from overload resolution). `constexpr` only if no member is a pointer/reference/union/`volatile` and result has no indeterminate/padding bits. Replaces the UB of `*reinterpret_cast<float*>(&u)`; a union "type pun" is well-defined in C but UB in C++ (reading the non-active member). C++23 adds `std::start_lifetime_as` for the placement-into-bytes case `bit_cast` can't express.

#### Strong typedefs / opaque typedefs
- **Mechanism:** wrap a primitive in a one-field struct (often EBO/`[[no_unique_address]]` for tag types) so distinct semantic types (`UserId` vs `OrderId`) are not interchangeable; `using`/`typedef` alone is a transparent alias and gives no type safety.
- **When:** prevent argument-order mix-ups, unit safety, API clarity (`enum class` for closed sets; libraries: Boost.SafeInt, `named_type`).
- **Gotchas:** `typedef`/`using` are aliases, **not** new types (no overloading, no extra safety). Strong types need explicit operator forwarding. `enum class` (C++11) is the lightweight strong-id for enumerated values.

### Scope & control-flow idioms

#### Scope guards (`gsl::finally`, `scope_exit`)
- **Mechanism:** an RAII object holding a callable that runs on scope exit; `gsl::finally(f)` / Library Fundamentals TS `std::experimental::scope_exit`/`scope_fail`/`scope_success`.
- **When:** ad-hoc cleanup of C-API resources without writing a dedicated RAII wrapper; "execute this on the way out" (Andrei Alexandrescu / Petru Marginean ScopeGuard).
- **Gotchas:** `scope_exit` always runs; `scope_fail` runs only during unwinding (uses `uncaught_exceptions()`, C++17); `scope_success` runs only on normal exit. The cleanup lambda must be `noexcept`. Not in the IS — TS or GSL only; prefer a real RAII type for recurring patterns.

#### Copy-elision idioms (RVO / guaranteed elision)
- **Mechanism:** **Guaranteed copy elision (C++17)** — returning a *prvalue* of the function's return type constructs directly into the caller's storage; the value categories are redefined so *no copy/move exists at all* (the type need not even be movable). **NRVO** (named return value optimization) — eliding the copy of a *named local* on return — remains a permitted-but-**not-guaranteed** optimization.
- **When:** factory functions can return non-movable types by value (C++17); `return MakeT();` / `return T{...};`.
- **Gotchas:** **Do not `std::move` a local in a `return` statement** — it turns an NRVO-eligible object into an xvalue, *blocking* NRVO and (since C++11) is a pessimization; the language already treats the named return operand as an rvalue for overload resolution. `return std::move(x)` is correct only when `x`'s type differs from the return type (e.g. returning a member, or up-converting). NRVO is impossible if multiple return paths name different objects.

#### Monadic `optional`/`expected` chaining (C++23)
- **Mechanism:** chain fallible computations without explicit `if (opt)` checks: `.and_then(f)` (f returns the wrapped type; flattens), `.transform(f)` (f returns a bare value; re-wraps), `.or_else(f)` (supply fallback on empty/error). `std::expected<T,E>` (C++23) adds `.transform_error(f)`.
- **When:** railway-oriented error pipelines; replaces nested checks and avoids exceptions for expected-failure paths.
- **Version:** `std::optional` monadic ops and `std::expected` are **C++23**. (`tl::optional`/`tl::expected` backport to C++11/14.)
- **Gotchas:** `and_then` vs `transform` confusion — `and_then`'s callable must return an `optional`/`expected`, `transform`'s must return a plain value. `expected`'s `value()` throws `bad_expected_access` if it holds an error; prefer monadic ops over `.value()`.

### Useful attributes (quick map)

#### `[[nodiscard]]` (C++17), `[[nodiscard("reason")]]` (C++20)
- **Mechanism:** warns if a return value is discarded; the C++20 form carries an explanatory message. Applies to functions, and to enum/class types (then any function returning them by value warns on discard).
- **When:** error codes, `[[nodiscard]] bool empty()` (catch "called instead of `clear`"), factory results, RAII handles, `expected`. Mark types like `expected`/`unique_ptr`-returns.
- **Gotchas:** suppress intentionally with `(void)expr` or `std::ignore`. Only a warning (compiler-encouraged), not an error.

#### `[[likely]]` / `[[unlikely]]` (C++20)
- **Mechanism:** branch-probability hints on statements/labels guiding the optimizer's code layout (hot/cold path placement).
- **When:** measured hot paths where the compiler mispredicts likelihood (error-handling branches `[[unlikely]]`, fast paths `[[likely]]`).
- **Gotchas:** premature/unmeasured use can pessimize; profile first. GCC/Clang `__builtin_expect` is the pre-C++20 equivalent. Misplacement hurts more than it helps.

#### `[[no_unique_address]]` — see *Storage & layout tricks* above.

---

## C Idioms Catalog

#### X-Macros
- **Mechanism:** a single list macro `#define LIST(X) X(a) X(b) ...` expanded multiple times with different `X` definitions to generate enums, name tables, and dispatch arrays from one source of truth.
- **When:** keep parallel data (enum values ↔ string names ↔ handlers) synchronized; serialization tables; avoiding the "add an enumerator, forget the string table" bug class.
- **Gotchas:** opaque to debuggers and tooling; macro errors are cryptic. C23/`_Generic` and code generators are alternatives. Stringify with `#` operator (`#name`).

#### Opaque pointers
- **Mechanism:** the header forward-declares `typedef struct Foo Foo;` but never defines `Foo`; callers hold only `Foo*` and use functions; the full struct lives in the .c file.
- **When:** ABI-stable C library boundaries, information hiding, handle-based APIs (the C analog of PIMPL).
- **Gotchas:** clients can't `sizeof` or stack-allocate — must use a `foo_create()`/`foo_destroy()` pair (heap). Don't expose internals via the header or the encapsulation breaks.

#### `container_of`
- **Mechanism:** given a pointer to a member, recover the enclosing struct pointer: `(type*)((char*)ptr - offsetof(type, member))` (Linux-kernel macro).
- **When:** intrusive data structures where a node is embedded in a larger object; generic containers that don't allocate the element.
- **Gotchas:** UB if `ptr` doesn't actually point to that member of a real `type` object. `offsetof` is only defined for standard-layout/POD types. Watch alignment and the cast through `char*`. The kernel version adds a `typeof` compile-time type check.

#### Intrusive lists
- **Mechanism:** embed the link pointers (`next`/`prev`) **inside** the element struct rather than in separately-allocated node wrappers; traversal recovers the element via `container_of`.
- **When:** zero-allocation linked structures, an element living on multiple lists simultaneously, real-time/kernel code avoiding malloc on the hot path.
- **Gotchas:** an element can be on a given list only once per embedded link; manual lifetime discipline (removing before free). The C++ analog is `boost::intrusive`.

#### Flexible Array Members (C99)
- **Mechanism:** last member of a struct is an incomplete array `T data[];`; allocate `offsetof(S, data) + n*sizeof(T)` so the array trails the header in one allocation.
- **When:** variable-length records (packets, strings-with-length) in a single contiguous allocation — one malloc, better locality than a separate pointer.
- **Version:** standardized in **C99** (`struct s { int n; char d[]; };`). Pre-C99 used the non-conforming "struct hack" `d[1]` or `d[0]`.
- **Gotchas:** `sizeof(S)` ignores the FAM (use `offsetof`, not `sizeof`, for allocation size). Must be the **last** member, struct must have ≥1 other named member, and an FAM struct can't itself be an array element or a non-last struct member. C++ has no standard FAM (compiler extension only). CERT DCL38-C governs correct syntax.

#### Designated initializers
- **Mechanism:** initialize by member/element name: `struct P p = { .x = 1, .y = 2 };` and `int a[5] = { [2] = 7 };` — unlisted members are zero-initialized.
- **When:** clear, order-independent, partial struct/array init; config tables; robust to struct field reordering.
- **Version:** **C99** (full power incl. array indices, ranges via GCC ext). **C++20** added a *restricted* form: members only (no array designators), must appear **in declaration order**, no nesting-out-of-order — stricter than C.
- **Gotchas:** C allows out-of-order and mixing; C++20 does not (declaration order enforced). C++ requires aggregate type. Cannot mix designated and positional for the same aggregate in C++.

#### Function-pointer dispatch tables
- **Mechanism:** an array (or struct of) function pointers indexed by an enum/opcode replaces a `switch`; the C mechanism for virtual-dispatch / state machines / interpreters.
- **When:** opcode interpreters, plugin/vtable emulation, state machines, command dispatch — O(1) selection and data-driven extension.
- **Gotchas:** bounds-check the index (out-of-range → wild call). Keep table and enum in sync (combine with X-Macros). Indirect call defeats inlining and hurts branch prediction vs. a `switch` the compiler can jump-table; uniform signatures required (often a `void* ctx` for state).

### Sources
- cppreference.com — `std::launder`, `std::bit_cast`, copy elision, `[[no_unique_address]]`, `[[nodiscard]]`, `[[likely]]`, `std::optional`/`std::expected` monadic ops, `std::experimental::is_detected` / `void_t` (authoritative per-feature semantics and version tags).
- ISO C++ Core Guidelines (B. Stroustrup & H. Sutter) — RAII, Rule of 0/3/5 (C.20/C.21/C.67), NVI, special-member suppression. https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines
- Herb Sutter, *GotW* (esp. #91 smart-pointer params, #100/#101 PIMPL/compilation firewalls) and *Exceptional C++* — PIMPL, copy-and-swap, NVI. https://herbsutter.com/gotw/
- Scott Meyers, *Effective Modern C++* (Items 25 `std::move`/`forward` & return, 41 by-value sinks) and *Effective C++* (Item 11 copy-and-swap) — move semantics, copy-elision/`std::move`-in-return pitfalls.
- WG21 papers: P0135 (guaranteed copy elision), P0840 (`[[no_unique_address]]`), P0798 (`optional` monadic), P0323/P2505 (`expected` + monadic), P1895 (`tag_invoke`); SEI CERT C (DCL38-C flexible array members). https://www.open-std.org/jtc1/sc22/wg21/docs/papers/

## How to use this canon

Each entry below is **RULE → WHY → HOW TO CHECK**. "Check" means what an agent can grep/static-analyze/compile-test, or which compiler flag, sanitizer, or static analyzer surfaces a violation. Version tags (C++11/14/17/20/23, C99/C11/C17/C23) state exactly when a feature/guarantee exists. Treat anything marked **UB** (undefined behavior) or **IFNDR** (ill-formed, no diagnostic required) as non-negotiable: a single violation poisons the *whole program*, not just the offending line, and may be silently "optimized" by the compiler.

---

## The foundational laws of the abstract machine

These are *standard guarantees*. Violating them is UB; an agent must never produce or "preserve" code that relies on the violated behavior.

### The as-if rule (observable behavior)
**Rule.** A conforming implementation may transform the program arbitrarily *as long as observable behavior is unchanged*. ([expr]/[intro.abstract]). Observable behavior = (1) accesses (reads/writes) through `volatile` glvalues, done strictly per the abstract machine; (2) data written to files at program termination; (3) prompting-output/input interaction of interactive devices happens as sequenced. Everything else (register allocation, reordering, eliding temporaries) is the compiler's to change.
**Why.** This is the license for *all* optimization. It is also why "but it works in debug" is meaningless: the optimizer is allowed to assume no UB occurred.
**Exceptions where observable behavior MAY change anyway.** (a) **Copy elision** — the *only* optimization permitted to change observable side effects, eliding copy/move ctors and their dtors (C++11+ permitted; **C++17 guaranteed** for prvalue initialization and prvalue `return`). (b) **Allocation elision/coalescing** of replaceable `operator new`/`delete` calls (C++14+, [expr.new]). (c) **Signal handlers** restricted to `volatile sig_atomic_t` and lock-free atomics.
**Check.** If code depends on a copy ctor *side effect* running (e.g. logging in a copy ctor that counts copies), it is fragile under elision — flag it. `volatile` is *not* a threading primitive (see data races).

### Sequencing & evaluation order
**Rule.** Within an expression, evaluations are *sequenced-before*, *sequenced-after*, or **unsequenced/indeterminately-sequenced**. Modifying a scalar twice, or modifying and separately reading it, with **no** sequencing between them is **UB** (C++) / UB (C). Classic UB: `i = i++;`, `a[i] = i++;`, `f(i++, i++)`.
**Why/Version nuance.** C++11 replaced "sequence points" with the sequenced-before relation. **C++17 (P0145)** tightened ordering: in `a.b`, `a->b`, `a[b]`, `a << b`, `a >> b`, the LHS/postfix-expr is now sequenced *before* the RHS; **assignment is the exception** — in `a = b` and `a @= b` the RHS (right operand) is sequenced *before* the LHS ([expr.ass]); and `a(b1,b2,...)` the function expression is sequenced before the args — but **argument evaluations remain indeterminately sequenced relative to each other** (not unsequenced, not ordered). So `f(g(), h())` still has no guaranteed g/h order in any standard.
**Check.** `-Wsequence-point` (GCC), `clang-tidy bugprone-*`, CERT **EXP30-C / EXP50-CPP**. Grep for `++`/`--` appearing twice on one line referencing the same object; multiple unsequenced modifications.

### Lifetime & object model
**Rule.** An object's lifetime begins after storage is obtained *and* initialization (if any) completes; it ends when the dtor starts (or storage is reused/released). Accessing an object outside its lifetime (use-after-free, use-before-construction, reading uninitialized indeterminate values, dangling reference/pointer) is **UB**. Reusing storage of an object with a non-trivial dtor without `std::launder`/proper restart is UB. **Type punning** via reading an inactive union member is UB in C++ (well-defined in C since C99 for the "common initial sequence" / and union read is implementation-/standard-defined in C); use `std::bit_cast` (C++20) or `memcpy`.
**Why.** The compiler assumes objects of type `T` are only accessed through `T`'s lifetime; this underpins TBAA and devirtualization.
**Check.** ASan (`-fsanitize=address`) for use-after-free/scope; MSan for uninitialized reads; UBSan (`-fsanitize=undefined`) for many lifetime/alignment issues. CERT **MEM30-C** (no access to freed memory), **EXP53-CPP** (no read of uninitialized).

### Strict aliasing
**Rule.** A stored value may be accessed only through a glvalue of: the object's dynamic type, a cv-qualified version, a (signed/unsigned) variant, a type whose members include the above (aggregate/union), a base class, or **`char`/`unsigned char`/`std::byte`** (the universal aliasing escape hatch). `char*`/`std::byte*` may alias anything, but **not** vice-versa. Violations are **UB**.
**Why.** Lets the optimizer assume `float*` and `int*` never alias. Reinterpreting an `int` buffer as `float` via pointer cast is UB even if sizes match.
**Check.** GCC/Clang `-fstrict-aliasing -Wstrict-aliasing=2`. Disable enforcement only deliberately with `-fno-strict-aliasing`. Prefer `memcpy`/`std::bit_cast` for reinterpretation. Note GCC/Clang treat `memcpy` of trivially-copyable bytes specially and optimize it away.

### One Definition Rule (ODR)
**Rule.** (1) Every entity *odr-used* must have **exactly one** definition in the program. (2) **Exactly one** definition per TU for any used non-inline thing. (3) Certain entities (class types, enums, **inline functions**, **inline variables (C++17)**, templates, and `constexpr` entities that are implicitly inline) may be defined in *multiple* TUs **iff** every definition is token-for-token identical *and* each token resolves to the same entity. Violating the multi-TU identity requirement is **IFNDR** — typically a silent miscompile/ODR-clash.
**Why.** The linker merges duplicate inline/template definitions assuming they are identical; mismatches (e.g. same class compiled with different `-DNDEBUG`, different struct layout per TU, different `enum` value) produce one-definition-rule violations that no diagnostic is required to catch.
**Check.** CERT **DCL60-CPP**. Tooling: GCC/Clang `-Wodr` with LTO (catches many ODR mismatches across TUs), `gold`/`lld` warnings. Discipline: put definitions of multiply-defined entities in headers, keep them identical, mark header-defined globals/constants `inline` (C++17) instead of relying on `static`/`extern` hacks; avoid macros that change type layout between TUs.

### Data races & the memory model (C++11+, C11+)
**Rule.** A **data race** = two *conflicting* actions (at least one a write to the same memory location) in different threads, at least one **non-atomic**, with neither *happens-before* the other. Any data race is **UB**. Avoid via: same thread; both atomic; or establish *happens-before* (mutex unlock synchronizes-with subsequent lock; atomic release synchronizes-with acquire).
**Why.** Lets the optimizer cache values in registers and reorder non-atomic memory ops. `volatile` does **not** establish ordering or atomicity between threads — it is for memory-mapped I/O / signals only.
**Check.** TSan (`-fsanitize=thread`). CERT **CON**-series (e.g. CON40-C, CON43-C, CON50-CPP). Prefer `std::atomic`, `std::mutex`/`std::scoped_lock` (C++17), and the default `memory_order_seq_cst` until proven hot. C++20 adds `std::jthread`, `std::barrier`, `std::latch`, `std::atomic_ref`, `atomic<shared_ptr>`.

---

## C++ Core Guidelines — the rules worth holding (cite by ID)

The Core Guidelines (Stroustrup & Sutter) are the canonical *style* law. High-value IDs with verified text:

### Philosophy (P)
- **P.1** Express ideas directly in code. **P.4** Ideally a program should be statically type safe. **P.5** Prefer compile-time checking to run-time checking. **P.8** Don't leak any resources. **P.9** Don't waste time or space (the *zero-overhead* mindset).
- **Check.** P.4/P.5: prefer `enum class`, strong types, `constexpr`, `static_assert`, concepts (C++20) over runtime tags + asserts. P.8: every resource must have an owner (RAII).

### Interfaces (I)
- **I.4** Make interfaces precisely and strongly typed. **I.11** **Never transfer ownership by a raw pointer (`T*`) or reference (`T&`)** — use `unique_ptr`/`shared_ptr` or `owner<T*>` (GSL). **I.13** Do not pass an array as a single pointer — use `span` (C++20 `std::span`).
- **I.10 / I.5 / I.6 / I.7 / I.27**: signal failure (don't leave object in invalid state); state pre/postconditions; prefer return values to out-params.
- **"Make interfaces hard to use wrong, easy to use right"** is the umbrella maxim (I-section intro). Encode invariants in types: distinct units, `gsl::not_null`, `std::optional`/`expected` (C++23) for "maybe", enums not `bool` flags.
- **Check.** Grep for raw-pointer params that own (paired `new`/`delete` across the boundary), `T*, size_t` pairs (→ `span`), and `bool` parameters (→ enum).

### Resource management (R) — RAII, ownership, no naked new/delete
- **R.1** Manage resources automatically using resource handles and RAII. **R.3** A raw pointer (`T*`) is non-owning. **R.4** A raw reference (`T&`) is non-owning. **R.5** Prefer scoped objects, don't heap-allocate unnecessarily. **R.10** **Avoid `malloc()`/`free()`** in C++. **R.11** **Avoid calling `new` and `delete` explicitly** (the *"no naked new/delete"* law). **R.12** Immediately give the result of an explicit resource allocation to a manager object. **R.20** Use `unique_ptr`/`shared_ptr` to represent ownership. **R.21** Prefer `unique_ptr` over `shared_ptr` unless you need to share ownership. **R.23** Use `make_unique`/`make_shared`. **R.30** Take smart pointers as parameters *only* to express lifetime semantics.
- **Why.** RAII ties resource lifetime to object lifetime so every path (including exceptions) releases. `make_shared`/`make_unique` are exception-safe and avoid the unsequenced-`new` leak that `f(unique_ptr<T>(new T), g())` could once cause (mitigated but still preferred post-C++17).
- **Check.** Grep `\bnew\b` / `\bdelete\b` / `\bmalloc\b` / `\bfree\b` in C++; each is a candidate finding unless inside a low-level handle class. clang-tidy `cppcoreguidelines-owning-memory`, `modernize-make-unique`, `modernize-make-shared`, `cppcoreguidelines-no-malloc`.

### Classes & special members (C) — Rule of Zero / Three / Five / Six
- **C.20** **If you can avoid defining default operations, do (Rule of Zero).** **C.21** **If you define or `=delete` any copy/move/destructor function, define or `=delete` them all (Rule of Five/Six).** **C.22** Make default operations consistent.
- **Rule of Three (C++98):** if you need a user dtor, copy-ctor, or copy-assign, you almost certainly need all three (they manage a resource).
- **Rule of Five (C++11):** add move-ctor and move-assign to the trio.
- **Rule of Zero (preferred):** own resources via members that are themselves RAII (`unique_ptr`, `vector`, `string`) and declare *none* of the five; the compiler-generated ones are correct.
- **Critical mechanism:** declaring a **destructor** (even `= default`) **suppresses implicit move** operations → the class silently falls back to *copy*, killing performance (Meyers, *Effective Modern C++* Item 17). Declaring a copy op also deprecates implicit move. So Rule of Zero or explicitly default all five.
- **C.67** A polymorphic base should suppress public copy/move (slicing) and provide a **virtual or protected destructor** (C.35: a base class destructor should be either public+virtual or protected+non-virtual).
- **Check.** clang-tidy `cppcoreguidelines-special-member-functions`, `hicpp-special-member-functions`. Grep for a user-declared dtor without the four others. Flag `delete` of a base pointer with non-virtual dtor (UB) — CERT **OOP52-CPP**.

### Expressions & statements (ES) — initialization & evaluation
- **ES.20** **Always initialize an object.** **ES.21** Don't introduce a variable (or constant) before you need it. **ES.22** Don't declare a variable until you have a value to initialize it with. **ES.23** Prefer the `{}`-initializer syntax (avoids narrowing, avoids the *most vexing parse*). **ES.43** Avoid expressions with undefined order of evaluation. **ES.44** Don't depend on order of evaluation of function arguments. **ES.45** Avoid "magic constants"; use symbolic constants. **ES.48** Avoid casts; **ES.49** if you must cast, use a named cast. **ES.78** Don't rely on implicit fallthrough in `switch` — use `[[fallthrough]]` (C++17).
- **Why.** ES.20 kills the largest class of UB (indeterminate reads). `{}` prevents narrowing conversions (a compile error in braces) and the most-vexing-parse (`Widget w();` declaring a function).
- **Check.** `-Wmaybe-uninitialized`, MSan, `-Wimplicit-fallthrough`, clang-tidy `cppcoreguidelines-init-variables`, `-Wnarrowing`.

### Const-correctness (Con)
- **Con.1** By default, make objects immutable (`const`/`constexpr`). **Con.2** By default, make member functions `const`. **Con.3** By default, pass pointers/references to `const`. **Con.4** Use `const` to define objects with values that do not change after construction. **Con.5** Use `constexpr` for values computable at compile time.
- **Why.** `const` documents intent, enables more aggressive optimization, prevents accidental mutation, and is required for safe sharing across threads (a truly-`const`, immutable object can be read concurrently without a race). Note **logical vs bitwise const**: a `const` member function must be safe to call concurrently; use `mutable` only for genuinely-transparent caches and protect them.
- **Check.** clang-tidy `misc-const-correctness`, `readability-make-member-function-const`. Grep for non-const member functions that don't mutate.

### Concurrency & parallelism (CP)
- **CP.1** Assume your code will run as part of a multi-threaded program. **CP.2** Avoid data races. **CP.3** Minimize explicit sharing of writable data. **CP.4** Think in terms of tasks, not threads. **CP.8** Don't try to use `volatile` for synchronization. **CP.20** Use RAII, never plain `lock()`/`unlock()` — use `lock_guard`/`scoped_lock`/`unique_lock`. **CP.21** Use `std::lock` or `std::scoped_lock` (C++17) to acquire multiple `mutex`es (deadlock-free ordering). **CP.32** To share ownership between unrelated `shared_ptr`s use `shared_ptr`.
- **Check.** TSan; grep for raw `mtx.lock()` not in a guard; grep for `volatile` used between threads (CP.8 violation).

### Design canon (umbrella maxims an expert applies)
- **Value semantics by default.** Prefer regular, copyable, comparable value types (`std::regular`, C++20); reach for reference semantics/`shared_ptr` only when identity/sharing is essential. Values compose, are race-free when copied, and play with the STL.
- **Liskov Substitution Principle (LSP).** A derived type must be usable wherever its base is expected without breaking base invariants/contracts (no strengthened preconditions, no weakened postconditions). Core Guidelines C.120–C.129 (use hierarchies for inherent is-a). **Slicing** (passing a derived by value as base) silently breaks this.
- **Prefer composition over inheritance.** Inherit only for *is-a* polymorphism; otherwise contain. (Core Guidelines C.129: distinguish implementation inheritance from interface inheritance; C.121: if a base class is used as an interface, make it a pure abstract class.)
- **Zero-overhead principle (Stroustrup).** "What you don't use, you don't pay for; and what you do use, you couldn't hand-code better." Abstractions (`unique_ptr`, ranges, `span`) compile to what an expert would write by hand. Exceptions to scrutinize: RTTI/`dynamic_cast`, exceptions on the *throw* path (zero-cost on the happy path under table-based EH), `std::function` (type erasure heap-allocates), virtual dispatch in hot inner loops.

---

## CERT C / C++ Secure Coding — the high-severity rules

CERT classifies by *severity × likelihood × remediation*. The high-impact rules an agent must enforce when reading/transforming code:

### Integers (INT)
- **INT30-C** Unsigned integer operations must not wrap (wrap is *defined* but usually a bug → buffer-size miscalc). **INT31-C** Ensure integer conversions don't lose/misinterpret data. **INT32-C** **Signed integer overflow is UB** — guard before `+ - * / <<`. **INT33-C** Guard against division/remainder by zero (`/ 0`, `% 0` are UB). **INT35-C** Use correct integer precisions.
- **Check.** UBSan `-fsanitize=signed-integer-overflow,shift`, `-ftrapv`; check arithmetic against `INT_MAX`/`SIZE_MAX` *before* the op (`if (a > INT_MAX - b)`), not after. Prefer `<stdckdint.h>` `ckd_add/ckd_sub/ckd_mul` (**C23**) or compiler `__builtin_*_overflow`.

### Memory (MEM)
- **MEM30-C** Do not access freed memory (use-after-free / double-free). **MEM31-C** Free dynamically allocated memory when no longer needed (leak). **MEM34-C** Only free memory allocated dynamically (no `free` of stack/static). **MEM35-C** Allocate sufficient memory (size = `count * sizeof(*p)`, overflow-checked). **MEM36-C** Don't modify alignment of objects via `realloc`.
- **Check.** ASan + LeakSanitizer; Valgrind. Always null the pointer after `free`; never `free` twice; never `free` non-heap. C++ equivalents in MEM/EXP/OOP (e.g. **MEM50-CPP** don't access freed memory, **MEM51-CPP** properly deallocate with matching new/delete & new[]/delete[]).

### Strings (STR)
- **STR30-C** Do not modify string literals (UB — they may be in read-only memory). **STR31-C** Guarantee storage for strings has space for the character data **and the null terminator** (the #1 C buffer-overflow). **STR32-C** Null-terminate strings passed to library functions that expect them. **STR38-C** Don't confuse narrow/wide strings.
- **Check.** Replace `strcpy/strcat/sprintf/gets` with bounded forms (`snprintf`, `strncat` with correct math, or C11 Annex K `*_s` where available); ASan catches overruns. Account `+1` for the terminator everywhere.

### Arrays / pointers (ARR / EXP)
- **ARR30-C** Do not form or use out-of-bounds pointers or array subscripts (out-of-bounds, and even forming a pointer >1-past-end, is UB). **ARR38-C** Guarantee library functions don't form invalid pointers. **ARR39-C** Don't add a scaled integer to a pointer incorrectly.
- **EXP** highlights: **EXP33-C** Do not read uninitialized memory. **EXP34-C** Do not dereference null pointers. **EXP45-C** Do not perform assignments in selection conditions inadvertently (`if (a = b)`). **EXP30-C / EXP50-CPP** Do not depend on order of evaluation / on UB side-effect ordering.
- **Check.** ASan/UBSan; `-Wnull-dereference`; clang-tidy `bugprone-*`; bounds via `std::span`/`gsl::span` in C++.

### C++-specific high-value
- **OOP52-CPP** Don't `delete` a polymorphic object through a base pointer lacking a virtual destructor (UB). **DCL60-CPP** Obey the ODR. **ERR**-series: don't leak / leave invalid state on exceptions. **EXP53-CPP** Don't read uninitialized memory. **EXP63-CPP** Don't rely on the value of a moved-from object (it's valid-but-unspecified).

---

## MISRA C / MISRA C++ — the safety-critical canon

MISRA targets embedded/safety-critical (automotive, medical, aero). Editions: **MISRA C:2012** (+ Amendments 1–4, covering C11/C18 and security; built on C99/C11), **MISRA C++:2023** (targets **C++17**, 179 rules, supersedes MISRA C++:2008 and merges AUTOSAR C++14).

### Rule classification (the meta-rule)
- **Mandatory** — shall always be complied with; *no deviation permitted*. **Required** — comply unless a documented, justified **deviation** exists. **Advisory** — recommended; deviations need not be formally recorded. An agent transforming "MISRA code" must know which class a rule is before relaxing it.

### The umbrella laws MISRA encodes
- **Rule 1.3 — There shall be no occurrence of undefined or critical unspecified behavior** (the master rule; any C/C++ UB violates MISRA regardless of other rules).
- **Dir 4.12 (Required) — Dynamic memory allocation shall not be used** (and **Rule 21.3** bans `malloc/calloc/realloc/free`). *Why:* heap → fragmentation, non-determinism, leaks, allocation failure on a tiny MCU. Allowed only via documented deviation, init-time-only allocation, or a bounded custom pool. MISRA C++:2023 similarly restricts `new`/`delete` outside controlled wrappers.
- **No recursion** (MISRA C **Rule 17.2**: functions shall not call themselves directly or indirectly) — unbounded stack growth is unacceptable without a worst-case bound.
- **No `goto` / restricted flow** (Rule 15.1 advisory against `goto`; 15.2/15.3 constrain it), single function exit historically (relaxed), all `switch` clauses well-formed with `break`, every `if/else if` chain ends in `else`, every `switch` has `default`.
- **No implicit conversions that lose information / the "essential type" model** (Rules 10.x): arithmetic must respect a defined essential-type system; no mixing signed/unsigned implicitly; no narrowing without explicit cast.
- **Restricted language surface:** no `union` type-punning, constrained pointer arithmetic (only within arrays), no flexible/variadic surprises, restrictions on `volatile`, no relying on evaluation order, no side effects in `sizeof`/`&&`/`||`/`?:` operands, no use of features with unspecified/implementation-defined behavior without documentation.
- **No use of certain library facilities** (`<stdio.h>`, `<stdlib.h>` abort/exit/system, `setjmp/longjmp`, `<time.h>` in some profiles, signal handling) due to UB or non-determinism.

### How to check MISRA compliance
- Dedicated checkers: **Coverity, Helix QAC (Perforce), Polyspace (MathWorks), PVS-Studio, LDRA, Cppcheck (MISRA add-on), clang-tidy** (partial). Compliance requires a **deviation record** for each relaxed Required/Advisory rule and a **guideline compliance summary**. An agent should *not* silently "fix" MISRA code in ways that introduce dynamic allocation, recursion, or implicit conversions.

---

## Contracts & assertions discipline

- **`assert` (C `<assert.h>` / C++ `<cassert>`)** checks programmer assumptions (preconditions/invariants) and is **compiled out when `NDEBUG` is defined**. *Rule:* an assertion must be **side-effect free** — never `assert(x = foo())` or `assert(do_work())`, because release builds drop it (CERT **MSC11-C**; **ERR06-C** covers assert/abort termination behavior). Use it for *programming errors*, never for *runtime/recoverable* errors (bad input, I/O failure → return error / throw / `std::expected` C++23).
- **`static_assert`** (C++11; **C11** `_Static_assert`, and C23 makes `static_assert` a keyword) — compile-time checks; zero runtime cost; the first line of defense for size/layout/concept assumptions.
- **C++ Core Guidelines I.5–I.7 / Pro.bounds / `gsl::Expects`/`gsl::Ensures`** express pre/postconditions today. The standard **Contracts** facility (`pre`/`post`/`contract_assert`) targets **C++26** (P2900) — do not assume it in C++23 or earlier code.
- **`[[assume(expr)]]`** (**C++23**, P1774): tells the optimizer `expr` is true; if it is *false at runtime that is UB*. Use only when provably true; it is an optimization hint, not a check. Earlier: `__builtin_assume`/`__assume`.
- **Check.** Grep `assert(` for `=`, function calls with side effects, or I/O; ensure recoverable conditions don't use `assert`. Confirm `NDEBUG`-sensitivity is intentional.

---

## Quick agent checklist (apply when reading/transforming C/C++)

1. **UB scan first:** signed overflow, OOB, uninit reads, use-after-free, data races, strict-aliasing casts, unsequenced modifications, null deref → run UBSan+ASan+TSan mentally or actually.
2. **Ownership:** every `new`/`malloc` has exactly one owner and a guaranteed-on-all-paths release → push to RAII (Rule of Zero).
3. **Special members:** any one of the five declared ⇒ all consistent; base classes have virtual-or-protected dtor; declaring a dtor didn't silently kill moves.
4. **`const`/`constexpr` by default;** pass-by-`const&` for non-trivial inputs; immutable-by-default.
5. **Interfaces hard to misuse:** no owning raw pointers across APIs, `span` not `(ptr,len)`, enums not `bool` flags, `optional`/`expected` for "maybe/error".
6. **Standard version sanity:** don't attribute C++17 guaranteed-elision / inline-variables / `[[fallthrough]]`, C++20 concepts/`span`/`jthread`, C++23 `expected`/`[[assume]]`, or C++26 contracts to an older `-std`.
7. **Assertions** side-effect-free and used only for programmer errors.

### Sources

- ISO/IEC 14882 (C++) Working Draft and ISO/IEC 9899 (C, C23 N3220 / C11 N1570) — normative definitions of as-if, sequencing, lifetime, ODR ([intro.abstract], [basic.def.odr], [intro.races], [expr.eval]).
- cppreference.com — *Definitions and ODR*, *The as-if rule*, *Copy elision*, *Multi-threaded executions and data races*, *Order of evaluation* (https://en.cppreference.com/w/cpp/language/definition , .../as_if , .../copy_elision , .../multithread , .../eval_order).
- C++ Core Guidelines, Stroustrup & Sutter (https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines) — P/I/R/C/ES/Con/CP rule IDs and text.
- SEI CERT C and C++ Secure Coding Standards (https://wiki.sei.cmu.edu/confluence/display/c , https://wiki.sei.cmu.edu/confluence/display/cplusplus) — INT/MEM/STR/ARR/EXP/DCL60-CPP/OOP52-CPP.
- MISRA C:2012 (incl. Amendments) and MISRA C++:2023, The MISRA Consortium; Scott Meyers, *Effective Modern C++* (Item 17, special-member generation).
