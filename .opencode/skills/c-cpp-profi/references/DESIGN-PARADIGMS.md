# Design Paradigms — OOP, Functional, Procedural & Data-Oriented (C and C++)

> How to recognize, work within, and transform between the paradigms a C/C++ codebase is written in. For a **mechanical paradigm read** of a repo, run `cpp_comprehension_map.sh <repo>` — its "Paradigm signals" lane counts OOP (virtual/override/access-specifier/inheritance), functional (lambda/ranges/algorithms/`optional`/`expected`), and C-OOP (function-pointer dispatch tables) markers with anchors and a soft dominant read (verified e.g. nuttx → C-OOP function-pointer tables, tinyxml2 → OOP, fmt → OOP+functional). Pair with [CODE-TRANSFORM.md](CODE-TRANSFORM.md) (paradigm migration), [REFACTOR-ISOMORPHISM.md](REFACTOR-ISOMORPHISM.md) (paradigm-preserving rewrites), [MEMORY-SAFETY.md](MEMORY-SAFETY.md) (RAII/ownership), and [PERFORMANCE.md](PERFORMANCE.md) (data-oriented design). Each major area cites authoritative sources.


## Object-Oriented Design in C++ (and C-OOP Idioms)

### The Central Axis: Value vs Reference/Pointer Semantics

C++ is unusual among OO languages in defaulting to **value semantics**: a declared object *is* the object, not a handle to one. Copying copies the whole object; assignment overwrites it; lifetime is scope-bound (RAII). Contrast Java/C#/Python, where almost everything is a reference. This choice is THE design fork in every C++ class.

- **Regular/value types** (Stepanov's "regular"): copyable, equality-comparable, default-constructible, semantically a value (`int`, `std::string`, `std::vector`, `std::complex`). Pass by `const&` (no copy) or by value when you'll store/move. Prefer these — they compose, are exception-safe, cache-friendly, and need no manual lifetime management. Core Guidelines **C.10/C.11**: prefer concrete (value) types.
- **Reference/entity types**: identity matters, non-copyable, polymorphic, manipulated through pointers/references (a `Widget` in a UI tree, a hardware device, a base-class subobject). These live behind `unique_ptr`/`shared_ptr`/references. Polymorphic base classes are entity types — Core Guidelines **C.67**: a polymorphic class should suppress public copy/move (delete or protect them) to prevent slicing.
- **Pass-by conventions** (Core Guidelines F.15–F.21): in-param read-only → `const T&` (or `T` by value if cheap-to-copy ≤ ~2-3 words, e.g. `int`, `string_view`, `span`); in/out → `T&`; sink/will-store → `T` by value then `std::move` (the "pass-by-value-and-move" idiom); out-only → return by value (NRVO/guaranteed copy elision since C++17 makes this free). Avoid out-params via pointer except for optional outputs.
- **Don't conflate `&` (reference, never null, non-rebindable, no ownership) with `*` (may be null, rebindable, ownership ambiguous)**. Use `gsl::not_null` / `std::reference_wrapper` where semantics need sharpening.

### Object Slicing (the classic value-semantics trap)

Assigning/copying a derived object into a base value *slices* off the derived part: `Base b = derived;` copies only the `Base` subobject, the vtable pointer is set to `Base`'s, and virtual dispatch resolves to `Base`. Same with by-value parameters (`void f(Base b)`) and `std::vector<Base>` holding derived instances. **Mechanism**: the copy invokes `Base`'s copy ctor on the `Base` subobject only. **Prevention**: make polymorphic bases non-copyable (Core Guidelines C.67), pass/store by reference or smart pointer, or use a closed-set value-polymorphic container (`std::variant`). This is *the* reason polymorphism in C++ requires indirection.

### Rule of 0 / 3 / 5 (special member functions)

The six special members: default ctor, destructor, copy ctor, copy assign, move ctor, move assign. Core Guidelines **C.21**: "If you define or `=delete` any default operation, define or `=delete` them all."

- **Rule of Zero** (preferred default): design so the class needs *no* user-declared special members — let each member (smart pointers, containers, `std::string`) manage its own resource; the compiler-generated members compose correctly. Pure business logic + value members ⇒ write none.
- **Rule of Three** (pre-C++11 / still applies): if you need a user destructor, copy ctor, or copy assign (because you hold a raw resource: `new`'d pointer, FD, `FILE*`, mutex, OS handle), you almost certainly need all three (shallow copy of a raw handle causes double-free / use-after-free).
- **Rule of Five** (C++11+): declaring any of {destructor, copy ctor, copy assign} *suppresses* implicit move generation, so a resource-owning class that wants move must declare all five. Conversely, declaring a move member implicitly `delete`s/suppresses the copies.

**Subtle deprecations/gotchas:** A user-declared destructor makes implicit copy generation *deprecated* (since C++11) though still emitted — relying on it is a latent bug. `= default` keeps the member *trivial/implicit-flavored* (matters for triviality traits, `constexpr`, and ABI) where a `{}`-body definition does not. A defaulted move that would be ill-formed becomes deleted, silently falling back to copy. Always write `noexcept` on move ops you intend containers to use (see below).

### Move Semantics (mechanism, not magic)

Move = transferring ownership of resources from an expiring object, leaving the source in a *valid-but-unspecified* state (Core Guidelines C.64). Built on **value categories**: lvalue (has identity), xvalue (expiring, identity + movable), prvalue (pure value). `T&&` is an rvalue reference binding to rvalues; but a *named* rvalue reference parameter is itself an **lvalue** — you must `std::move` it again to pass the rvalue-ness on.

- `std::move(x)` is just a `static_cast<T&&>(x)` — it moves *nothing*; it casts to xvalue so an overload taking `T&&` (move ctor/assign) is selected.
- `std::forward<T>(x)` is conditional cast for **perfect forwarding** in templates: with a *forwarding reference* `T&&` (deduced `T`), it preserves the caller's value category. Forwarding references only arise in deduced contexts (`template<class T> f(T&&)`, `auto&&`), NOT for `std::vector<int>&&` (concrete) — that's a plain rvalue ref.
- **`noexcept` move is load-bearing**: `std::vector` reallocation uses `std::move_if_noexcept` — if the move ctor isn't `noexcept`, the vector *copies* on growth to preserve the strong exception guarantee. Mark move ops `noexcept` (Core Guidelines C.66).
- **Don't move into the same object you read from**; **self-move-assignment must be safe** (or at least leave a valid state). Don't `return std::move(local)` — it pessimizes NRVO; just `return local;` (guaranteed elision for prvalues since C++17; NRVO is allowed-but-not-guaranteed for named locals).
- A moved-from `unique_ptr` is null; a moved-from `std::string`/`vector` is empty-or-unspecified — valid to destroy/reassign, not to read meaningfully.

### Dispatch Mechanisms Compared

| Mechanism | Binding | Cost | Open/closed set | Notes |
|---|---|---|---|---|
| **Runtime virtual** | dynamic, per-call | indirect call via vtable (1 load + indirect branch), defeats inlining, hurts branch predictor | **open** (add types w/o touching callers) | classic OO; ABI-sensitive |
| **CRTP** static polymorphism | compile-time | zero (fully inlinable) | closed at compile time | no vtable; code bloat per instantiation |
| **`std::variant`+`std::visit`** | compile-time generated jump | small jump-table dispatch, inlinable bodies | **closed** value-polymorphism | no heap, value semantics, no slicing |
| **Function pointers / `std::function`** | runtime | indirect call (+possible heap/type-erasure for `std::function`) | open | the C-OOP and Strategy mechanism |

**Runtime virtual + vtable (Itanium C++ ABI).** Each polymorphic class has one vtable; each object's first word (typically) is the **vptr**. Per the Itanium C++ ABI, the address stored in the vptr points at the **first virtual function pointer**, and *just before* it sit two metadata slots: **offset-to-top** (a `ptrdiff_t` displacement from this subobject to the complete object — zero in the primary vtable, negative in secondary base subobjects; needed for `dynamic_cast<void*>`) and the **RTTI / typeinfo pointer** (one shared `type_info` per most-derived class *type* — a single object shared by all instances of that type, per the Itanium ABI). Calls resolve as `(*vptr[slot])(this, args...)`; the implicit `this` may be *adjusted* by a thunk for multiple/virtual inheritance. `dynamic_cast` and `typeid` on a polymorphic type read the RTTI slot. Virtual calls in ctors/dtors dispatch to the *currently-constructed* type (the vptr is rewritten as each base ctor runs) — never get virtual-override behavior from within a base ctor/dtor.

- **ABI fragility ("fragile base class"):** adding/reordering virtual functions, adding data members, or changing the inheritance graph changes vtable layout/object size ⇒ all dependents must recompile. This is why stable plugin boundaries use abstract interfaces (pure-virtual classes, which are stable so long as you only *append* virtuals) or C ABIs.
- **Always `virtual ~Base()`** (or protected non-virtual destructor) for a class deleted polymorphically — `delete basePtr` of a derived through a non-virtual dtor is UB (Core Guidelines C.35).
- **Security (CFI):** vtables are a control-flow-hijacking target (vtable injection / fake-vtable exploits). Mitigations: Clang/GCC **`-fsanitize=cfi`** (forward-edge Control Flow Integrity validates the vptr against the expected class set; requires LTO), MSVC `/guard:cf`, and hardware CET. The `final` keyword lets the compiler *devirtualize* (turn virtual calls into direct/inlined calls) and shrinks the CFI valid-set.

**CRTP** (`template<class D> struct Base { void f(){ static_cast<D*>(this)->impl(); } }; struct X : Base<X>{ ... };`): static "polymorphism" with zero indirection; used for mixins, `enable_shared_from_this`, expression templates, and policy injection. Costs: separate instantiation per `D` (code bloat), no heterogeneous containers, leakier interface. **C++23 "deducing this"** (explicit object parameter, `void f(this auto&& self)`, P0847) largely *replaces* CRTP for self-referential method dispatch — no `static_cast`, no template-base boilerplate; compilers: GCC 14+, Clang 18+, MSVC 19.32+ (Visual Studio 2022 17.2).

**`std::variant` + `std::visit`** (C++17): closed-set, value-based polymorphism — sum type with no heap, no vptr, no slicing, value semantics. `std::visit(overloaded{...}, v)` (the `overloaded` lambda-inheritance trick) is the modern **Visitor**. Gotcha: a variant can become **`valueless_by_exception`** if a type-changing assignment/emplace throws mid-construction; then `index()==variant_npos`, `std::get` and `std::visit` throw `std::bad_variant_access`. Prefer nothrow-movable alternatives to avoid it. Use variant when the type set is fixed and you want value semantics + exhaustiveness; use virtual when the set is open/extensible by third parties.

### Encapsulation, Invariants, and RAII as the OOP Backbone

- **Invariant** = a condition the class guarantees true between public calls. If a struct has no invariant, make it a `struct` with public data (Core Guidelines C.2/C.8); the moment an invariant exists, make data `private` and enforce it in constructors + mutators. A constructor's job is to *establish* the invariant; if it can't, throw (Core Guidelines C.41/C.42) — never leave a half-built object.
- **RAII** (Resource Acquisition Is Initialization) is the backbone: bind every resource (memory, lock, file, socket, transaction) to an object whose destructor releases it. This is what makes C++ exception-safe *without* `finally`. `std::unique_ptr`/`shared_ptr`, `std::lock_guard`/`scoped_lock`, `std::fstream`, custom guards. Core Guidelines **R.1**: manage resources automatically using RAII. Stack unwinding runs destructors in reverse construction order, giving deterministic cleanup. **Never** `new`/`delete` raw in modern code (R.11); prefer `make_unique`/`make_shared`.
- **Exception safety levels** (Abrahams): *nothrow* (`noexcept`), *strong* (commit-or-rollback, state unchanged on throw — the copy-and-swap idiom achieves this for assignment), *basic* (no leak, valid state). copy-and-swap also gives a unified, self-assignment-safe assignment operator.

### Inheritance vs Composition

- **Prefer composition** (has-a / uses-a) over inheritance (is-a). Inheritance is the *tightest* coupling in C++ and exposes you to the fragile-base-class problem. Use **public inheritance only for true is-a substitutability**.
- **LSP (Liskov Substitution Principle):** functions taking `Base*`/`Base&` must work with any derived object without knowing it — derived may *weaken preconditions / strengthen postconditions*, never the reverse. A `Square : Rectangle` that breaks `setWidth` independence is the canonical violation ⇒ use composition or rethink the model.
- **Core Guidelines:** **C.120** use hierarchies only for inherently hierarchical concepts; **C.121** if a base is an interface, make it a *pure abstract class*; **C.129** distinguish **interface inheritance** (separate users from implementations — pure virtual) from **implementation inheritance** (share code — prefer composition/delegation instead); **C.133** avoid protected data; **C.135** use multiple inheritance to represent multiple distinct *interfaces*.
- **NVI (Non-Virtual Interface) idiom** (Herb Sutter, "Virtuality", C/C++ Users Journal 2001): make the public interface **non-virtual**, and the customization points **private (or protected) virtual**. `public void process(){ check_pre(); do_process(); check_post(); }` calls `private virtual do_process()`. This decouples the *caller-facing* interface from the *implementer-facing* hooks, gives the base a single chokepoint for pre/post conditions, logging, locking, and invariant checks, and lets you change the wrapping without touching overrides. Note: a virtual function can be private *and still be overridden* — accessibility is checked at the call site (the base's), overriding is independent. Corollary (Sutter's guideline): prefer to make virtual functions non-public; make public functions non-virtual; a base destructor should be either public-and-virtual or protected-and-non-virtual.
- `override` (C++11) on every override (Core Guidelines C.128) — catches signature-mismatch bugs (e.g., `const`/ref-qualifier divergence silently creating a *new* function). `final` to seal and enable devirtualization.

### SOLID Applied to C++

- **S**ingle Responsibility: one reason to change; small cohesive classes (composes with Rule of Zero).
- **O**pen/Closed: extend without modifying — via virtual interfaces (open set), templates/CRTP/concepts (compile-time), or registries.
- **L**iskov: see above; the *only* justification for public inheritance.
- **I**nterface Segregation: many small pure-abstract interfaces over one fat base (C.121, C.135). Reduces fragile-base coupling.
- **D**ependency Inversion: depend on abstractions. In C++ this is *either* runtime (inject an abstract-base reference / `std::function`) *or* compile-time (template the policy / use concepts — DI with zero overhead). Concepts (C++20) make compile-time interface contracts checkable and diagnosable.

### GoF Patterns in Modern C++ (many became language/library features)

- **Strategy** ⇒ `std::function` (runtime, type-erased) or a template policy parameter / lambda (compile-time, zero-overhead).
- **Iterator** ⇒ standard iterators and **Ranges** (`std::ranges`, views, pipe `|` composition) (C++20).
- **Visitor** (over a closed set) ⇒ `std::variant` + `std::visit` + `overloaded`. Over an open set, double-dispatch via virtual `accept`.
- **Observer** ⇒ signals/slots, or `std::function` callback lists.
- **Factory** ⇒ functions returning `unique_ptr<Base>`; **Singleton** ⇒ function-local `static` (thread-safe init guaranteed since C++11, "Meyers singleton") — but singletons are often a design smell.
- **Command** ⇒ `std::function` / lambdas. **Adapter/Decorator** ⇒ composition + wrapping (sometimes templates).
- **Type Erasure** (a C++-specific super-idiom, e.g. `std::function`, `std::any`, `std::shared_ptr<void>` deleter): hide a concrete type behind a value-semantic facade backed by an internal vtable/virtual concept-model. Gives runtime polymorphism *without* requiring clients to inherit (duck-typed, non-intrusive) — `std::function` and `std::any` are library instances; libraries like Boost.TypeErasure / `dyno` / `proxy` (P3086) generalize it.

### C-OOP Idioms (object orientation in C)

Since C has no classes, OO is built from structs + function pointers + conventions:

- **Opaque pointer / handle (PIMPL at the C level):** the header declares only `typedef struct Foo Foo;` and functions taking `Foo*`; the full `struct Foo` lives in the `.c` file. Callers can't see layout ⇒ true encapsulation and a *stable ABI* (changing fields doesn't break callers). The C++ analogue is the **Pimpl idiom** (`std::unique_ptr<Impl>`), which also firewalls compile-time dependencies and stabilizes ABI (Core Guidelines C.129/I.27, Sutter "Compilation Firewalls").
- **Struct + function-pointer "vtable":** put function pointers in the struct (per-object dispatch, e.g. Linux kernel **`struct file_operations`**, where each device fills in `.read`, `.write`, `.open`) or in a shared *class struct* referenced by each instance (one vtable per type — **GObject/GType**: a `GObjectClass` holds `constructor`, `dispose`, `finalize`, `set_property`, … and each instance's first member points at it, exactly mirroring a compiler vtable to avoid storing N pointers in M instances). This is hand-rolled virtual dispatch.
- **COM / `IUnknown`:** the binary-stable OO contract for Windows. A COM interface pointer points at a vtable whose first three slots are `QueryInterface` (GUID → interface pointer, the cross-cast/feature-query), `AddRef`, `Release` (manual refcount lifetime); interface-specific methods follow. The layout deliberately matches the MSVC C++ single-inheritance vtable so C++ classes implement COM "for free." This is *the* canonical language-neutral ABI-stable virtual interface.
- **Inheritance by embedding:** put the base struct as the *first member* of the derived struct (`struct Derived { struct Base base; ... };`). Because the base subobject is at offset 0, a `Derived*` is safely castable to `Base*` (the basis of `container_of` and the kernel/GObject upcast macros).
- **Tagged unions (sum types / discriminated unions):** `struct { enum Tag tag; union { ... } u; };` — the C ancestor of `std::variant`. Dispatch via `switch(tag)`. **Gotcha:** reading the wrong union member is UB-adjacent (type punning); only the last-written member is valid in standard C/C++ (a few exceptions: common initial sequence of standard-layout structs in a union; in C, certain compiler-blessed punning). Prefer `std::variant`/`std::bit_cast`/`memcpy` in C++.
- **`this`-by-convention:** every "method" takes the object pointer as its first parameter explicitly (`foo_do_thing(Foo* self, ...)`) — exactly what C++ generates implicitly and what deducing-this makes explicit again.

### High-Value Gotchas Checklist

- Polymorphic base without `virtual` (or protected) destructor ⇒ UB on polymorphic delete.
- Slicing on by-value pass/store of derived ⇒ delete copy/move on bases (C.67).
- Calling virtuals in ctor/dtor ⇒ dispatches to current type, not the final override.
- Forgetting `noexcept` on move ⇒ silent copy-on-vector-grow.
- Relying on implicitly-generated copies after declaring a destructor ⇒ deprecated, fragile.
- `std::function` may heap-allocate and adds an indirect call — don't use it on hot paths where a template/lambda suffices.
- ABI: never add/reorder virtuals or data members in a shipped polymorphic interface; only append, or version the interface.
- `std::variant` valueless-by-exception ⇒ guard with nothrow-movable alternatives.

### Sources

1. cppreference.com — *The rule of three/five/zero*, *std::variant / std::visit / valueless_by_exception*, *Curiously Recurring Template Pattern (CRTP)* — https://en.cppreference.com/w/cpp/language/rule_of_three.html , https://en.cppreference.com/w/cpp/utility/variant , https://en.cppreference.com/w/cpp/language/crtp
2. C++ Core Guidelines (Stroustrup & Sutter) — esp. C.2/C.8/C.10/C.11, C.21, C.35, C.64–C.67, C.120/C.121/C.128/C.129/C.133/C.135, R.1/R.11, F.15–F.21 — https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines
3. Herb Sutter — "Virtuality" (C/C++ Users Journal, Sept. 2001), defining the Non-Virtual Interface idiom; and GotW / "Pimpl / Compilation Firewalls."
4. Itanium C++ ABI (vtable layout: offset-to-top, RTTI/typeinfo pointer, virtual function entries, thunks) — https://itanium-cxx-abi.github.io/cxx-abi/abi.html
5. Scott Meyers, *Effective C++* / *Effective Modern C++* (slicing, NVI, virtual destructors, move semantics, `std::move`/`std::forward`, special members); Raymond Chen, "The layout of a COM object" (The Old New Thing) — https://devblogs.microsoft.com/oldnewthing/20040205-00/?p=40733

## Functional / Declarative + Procedural + Data-Oriented Paradigms (C and C++)

C and C++ are multi-paradigm. "Functional" here is *pragmatic FP*: purity, immutability, higher-order functions, expression-oriented pipelines, and monadic error handling — not Haskell. The agent's job is to apply each paradigm where its cost model wins. Default to **declarative algorithms + const-correctness**; drop to **procedural C / data-oriented** in hot loops and FFI/ABI boundaries.

### Purity & referential transparency

- A function is *pure* if its result depends only on its arguments and it has no observable side effects (no global/static mutation, no I/O, no time/random). Pure functions are *referentially transparent*: a call can be replaced by its value without changing program meaning — this is what enables CSE, memoization, reordering, and `constexpr` evaluation.
- C++ has no portable purity attribute in the standard, but compilers expose hints: GCC/Clang `__attribute__((const))` (no reads of memory, no side effects — strongest) and `__attribute__((pure))` (may read global memory, no side effects). These authorize the optimizer to elide/CSE calls; **misapplying them is UB** (e.g. marking a function `const` that dereferences a pointer arg). C23 standardizes `[[reproducible]]` and `[[unsequenced]]` (N3220) expressing roughly "effectless"/"idempotent + effectless" — `unsequenced` is the stronger, ~ pure.
- Gotcha: a `const`-qualified member function is *not* pure — it can mutate `mutable` members, mutate through pointer/reference members, or call I/O. `const` constrains the object's *logical* state, not effects.

### Immutability: const-correctness, `constexpr`, `consteval`, `constinit`

- **`const` is the primary immutability tool.** Make everything `const` that can be (Core Guidelines Con.1/Con.2/Con.3). `const` on a local/param is a contract checked at compile time; on a member function it means "callable on a `const` object." Top-level `const` on by-value params is ignored in the function *type* (doesn't affect overloading) but still enforces immutability in the body.
- **`mutable`** carves an exception for *bitwise-but-not-logical* const (caches, mutexes). A `std::mutex` member is the canonical `mutable`.
- **`const` ≠ thread-safe by itself**, but the standard library contract (and Core Guidelines CP.2/Con) is: `const` member functions must be safe to call concurrently. Treat "`const` == read-only == safe for concurrent reads" as the design invariant.
- **`constexpr`** (C++11, hugely relaxed in C++14/17/20): *may* run at compile time, *may* run at runtime. A `constexpr` function used in a constant-expression context is evaluated at translation time; otherwise it's an ordinary function. `constexpr` variable ⇒ immutable + usable in constant expressions. C++20 added `constexpr` allocation (`new`/`delete` and `std::vector`/`std::string` in constant evaluation, provided allocations don't escape), `constexpr` virtual, `try`/`catch`. C++23 further relaxed (`constexpr` with `goto`, `static`/`thread_local` locals allowed in non-evaluated portions, `std::is_constant_evaluated` usable).
- **`consteval`** (C++20): *immediate function* — **must** produce a constant; every call is evaluated at translation time or is ill-formed. Use for compile-time-only factories/checks (e.g. validated string literals). C++20 pitfall: a `constexpr` function could not call a `consteval` one (the call isn't in an immediate context). **C++23 `if consteval`** (P1938) fixes this: inside its true-branch you are in an immediate context and may call `consteval`; it also cleanly replaces the `if (std::is_constant_evaluated())` idiom (which is itself a footgun — `if constexpr (std::is_constant_evaluated())` is *always true* and a classic bug).
- **`constinit`** (C++20): guarantees *static/constant initialization* (no dynamic init) — solves static-init-order fiasco for globals — but the object is **not** `const` afterward.
- Gotcha: `const` does **not** imply `constexpr`. A namespace-scope `const int n = f();` may be dynamically initialized; `constexpr int n = f();` requires `f()` be a constant expression.

### Higher-order functions & callables (cost model matters)

Callable spectrum, cheapest/most-inlinable first:

1. **Lambda / named function object (functor)** passed as a *template type parameter* (`template<class F> void g(F f)`) or `auto` param. The concrete type is known ⇒ the call is **inlinable**, zero indirection, zero allocation. This is the idiomatic way to pass callbacks to algorithms; it is why `std::sort` beats C `qsort` (qsort takes a function pointer ⇒ indirect call per comparison, no inlining).
2. **Function pointer** `R(*)(Args...)`: one indirection, no allocation, C-ABI-compatible. A *captureless* lambda implicitly converts to a function pointer; a *capturing* lambda does **not**.
3. **`std::function_ref<R(Args...)>`** (C++26): non-owning, type-erased, trivially copyable (passable in registers), **zero allocation** — the right type for "callback parameter I don't store." Analogous to `std::string_view` for callables. Until C++26, emulate with a template param or a `{void* ctx, R(*)(void*,Args...)}` pair.
4. **`std::move_only_function<R(Args...) cv ref noexcept>`** (C++23): owning, type-erased, move-only; honors `const`/`&`/`&&`/`noexcept` qualifiers in its signature (fixing `std::function`'s broken const-correctness — `std::function`'s `operator()` is always `const`-callable even on mutable state). Can hold move-only callables (e.g. capturing `unique_ptr`). C++26 adds **`std::copyable_function`** (copyable, correct const semantics — the intended modern replacement for `std::function`).
5. **`std::function<R(Args...)>`** (C++11): owning, copyable, type-erased. Costs: **(a) potential heap allocation** — small callables may fit a Small-Buffer-Optimization (SBO) buffer (implementation-defined size; libstdc++/libc++ ~16 bytes; *not guaranteed*), larger captures heap-allocate; **(b) virtual-style indirect call** through the type-erased vtable (no inlining at the call site); **(c)** copies may allocate. Throws `std::bad_function_call` if empty-invoked. Prefer 1–4 unless you genuinely need owning + copyable type erasure stored in a container.

**C callbacks (no closures):** the universal idiom is *function pointer + `void* context`*. Standard examples: `qsort(base,n,size, int(*cmp)(const void*,const void*))` and `bsearch` (no context param — must smuggle state via globals/TLS, a known limitation), POSIX `pthread_create(.., void*(*)(void*), void* arg)`, `atexit`. **qsort gotchas:** comparator must impose a strict weak ordering and return `<0/0/>0` (returning `a-b` on `int` overflows — UB/incorrect); element access is via `void*` + `memcpy`-style reads, so types must be trivially copyable. The modern C "closure" pattern is a struct `{ R(*fn)(void* ctx, Args); void* ctx; }`.

### Algorithms & ranges over raw loops ("no naked loops")

- Core Guidelines: **prefer named algorithms to hand-written loops** — they state intent, are correct-by-construction (off-by-one, iterator invalidation), and are often better-optimized. Sean Parent's "no raw loops." Map intent → algorithm: search `find`/`find_if`/`any_of`/`all_of`/`none_of`; transform `transform`; reduce `accumulate`(serial)/`reduce`(unordered, parallelizable)/`transform_reduce`; partition/sort/`nth_element`; dedup `unique`; etc.
- **`std::accumulate` vs `std::reduce`:** `accumulate` is strictly left-fold, deterministic, *not* parallelizable; `reduce` (C++17) requires associative+commutative op and may reorder/parallelize (`<execution>` policies). Floating-point: `reduce` results can differ run-to-run; `accumulate` is stable.
- **Ranges (C++20, `std::ranges`)** — algorithms take a *range* (not iterator pair), accept a **projection** (a member-pointer/lambda applied to each element before the predicate, e.g. `ranges::sort(v, {}, &Rec::key)`), and are constrained by concepts (better errors).
- **Views (`std::views`, lazy)** compose with `operator|`: `v | views::filter(pred) | views::transform(f)`. Views are **lazy** — no work and no allocation at construction; each element is computed on demand during iteration. Equivalent to nested functional form `transform(filter(v, pred), f)`. Common views: `filter`, `transform`, `take`/`take_while`, `drop`/`drop_while`, `iota` (generator), `join`, `reverse`, `elements`/`keys`/`values`, `split`/`lazy_split`, `enumerate`(C++23), `zip`(C++23), `chunk`/`slide`/`stride`(C++23). C++23 adds `ranges::to<Container>()` to materialize a view into a container.
- **C++23 fold algorithms** (`ranges::fold_left`, `fold_right`, `fold_left_first`, `fold_left_with_iter`, …, P2322 by Barry Revzin): the rangified, projection-aware, concept-constrained replacement for `accumulate` — and they *return the accumulated value by deduced type* rather than mutating, fixing `accumulate`'s init-type pitfall (`accumulate(v.begin(), v.end(), 0)` truncating to `int`/silently doing integer math on `double` is the classic bug).

### Monadic error handling

- **`std::optional<T>`** (C++17): "value or nothing." C++23 adds the **monadic interface**: `and_then(f)` (f: `T -> optional<U>`; chains a step that may itself be empty), `transform(f)` (f: `T -> U`; maps the value, never returns optional), `or_else(f)` (f: `() -> optional<T>`; recovery when empty). These collapse nested `if (opt) {…}` ladders into a single pipeline.
- **`std::expected<T,E>`** (C++23, P0323): "value or *typed* error" — the principled alternative to exceptions and to `optional` (which loses the *why*). Same monadic set plus error-side ops: `and_then` (`T -> expected<U,E>`), `transform` (`T -> U`), `or_else` (`E -> expected<T,E>`, recovery), `transform_error` (`E -> F`, remap error). `value()` throws `bad_expected_access<E>` on the error state; `value_or`, `error()`, `has_value()`/`operator bool` for explicit handling. (Monadic ops for `expected` standardized via P2505.) Use `and_then` when the next step *can fail*, `transform` when it *can't*.
- Choose: **exceptions** for truly exceptional, rare, cross-cutting failures (zero cost on the happy path, but unwinding cost + control-flow opacity on throw); **`expected`** for expected, frequent, local, "I must decide right here" errors (explicit, visible in the type, no unwinding). Avoid mixing `optional` for errors when the caller needs the cause.
- **C error discipline (no monads):** consistent **error-return codes** with a single convention (e.g. `0 == OK`, negative `errno`-style, or an `enum`), the **early-return / guard-clause** pattern, and `goto cleanup;` single-exit for resource release (the canonical Linux-kernel idiom — acquire in order, `goto` to staged unwind labels in reverse). For value+status, return a **result struct** `{ T value; int err; }` or an out-param (`R fn(Args, T* out)` returning status). Always check `errno` *after* the call and only when the function documents setting it, and reset `errno=0` before if you need to distinguish.

### Composition & avoiding shared mutable state

- Favor **value semantics** and pure transforms; pass data in, return new data out. Shared mutable state is the source of data races and aliasing bugs. Core Guidelines: **prefer immutable data** (Con), pass cheap-to-copy by value, expensive by `const&`, sink/owning by value-then-move.
- Composition tools: pipe-composed views, `std::bind_front`/`bind_back`(C++23) over the legacy `std::bind` (which has placeholder/nesting footguns), generic lambdas as combinators. Prefer returning `expected`/`optional` so steps compose monadically rather than communicating through shared error flags.
- **Aliasing/UB note:** the compiler assumes objects of unrelated types don't alias (strict aliasing). "Functional" copying that round-trips bytes through the wrong pointer type is UB; use `std::bit_cast`(C++20) or `memcpy`, never a reinterpreting pointer cast, to reinterpret bits.

### Recursion & tail calls

- C++ does **not** guarantee tail-call optimization (TCO); deep recursion risks **stack overflow** (UB — typically a crash, not a catchable error). GCC/Clang *may* perform TCO at `-O2`, but it is never guaranteed and is defeated by non-trivial destructors that must run after the call. Prefer iteration or an explicit stack for unbounded depth.
- **Security:** recursion driven by *attacker-controlled input* (deeply nested JSON/XML, recursive descent parsers, `realpath`/symlink chains) is a **stack-exhaustion DoS** (CERT C MEM05-C, CWE-674 uncontrolled recursion). Bound recursion depth explicitly or convert to an iterative algorithm with a heap-allocated work stack.

### Procedural C: modules, opaque types, error codes

- **Translation-unit modularity:** `static` at file scope gives **internal linkage** (private to the .c file); declare the public surface in a header, keep helpers `static`. This is C's encapsulation. (C++ `static`/anonymous-namespace plays the same role; C++20 `module`/`export` is the modern replacement for header-based separation but is a parallel mechanism, not the topic here.)
- **Opaque types (Pimpl in C):** the header declares only `typedef struct Foo Foo;` (incomplete type) and functions taking `Foo*`; the full `struct Foo { … }` lives in the .c file. Callers can't see or sizeof the layout → true ABI/encapsulation boundary, but **must** allocate via a `Foo* foo_create(void)` / `void foo_destroy(Foo*)` API (they can't stack-allocate an incomplete type). Mirror in C++ with the **Pimpl idiom** (`std::unique_ptr<Impl> pImpl;`) for ABI stability / compile-firewalling (note: Pimpl costs a heap allocation + indirection per object).
- **Module pattern:** prefix-namespaced functions (`foo_init`, `foo_step`), an explicit context handle as first arg, consistent ownership/error conventions documented at the header.

### Data-Oriented Design (DoD)

- **Premise (Mike Acton):** the purpose of code is to transform data; design for the *hardware's* access pattern, not for conceptual objects. The dominant cost in hot loops is **memory latency / cache misses**, not instructions. A cache line is typically **64 bytes**; touching one byte pulls in the whole line.
- **AoS vs SoA.** *Array of Structures* (`struct P{float x,y,z; int id;}; P a[N];`) keeps an entity's fields together — good when you touch *most* fields of *one* entity (random per-entity access, OOP-natural). *Structure of Arrays* (`float x[N], y[N], z[N]; int id[N];`) keeps each field contiguous — good when a hot loop touches *one field across all entities*: maximizes cache-line utilization (no wasted bytes pulled in), enables **SIMD auto-vectorization**, reduces bandwidth. Classic win: a physics `integrate()` that only reads/writes position+velocity wastes a third+ of each cache line in AoS.
- **When DoD beats OOP:** hot inner loops over large homogeneous collections — **ECS** (entity-component-system game engines), **codecs/DSP/image pipelines**, columnar analytics, particle systems. The transform is run over millions of records; eliminating per-element virtual dispatch and packing the *exact* hot fields contiguously dominates everything else.
- **DoD techniques:** SoA; *hot/cold splitting* (separate frequently-touched fields from rarely-touched ones into parallel arrays); replace `vector<unique_ptr<Base>>` + virtual calls with type-segregated arrays processed in batches (devirtualization + branch-prediction friendliness); `alignas(64)` to avoid **false sharing** (two threads writing different objects that share one cache line ⇒ ping-ponging line ownership, silent ~10–100× slowdown) — `std::hardware_destructive_interference_size` (C++17) gives the platform's separation. Prefer indices/handles over pointers (smaller, relocatable, stable across vector growth).
- **When *not* to use DoD:** cold code, small N, code where clarity dominates and the access pattern is irregular. DoD trades abstraction for layout control; spend it only where the profiler points.

### FP costs & gotchas in C/C++ (load-bearing)

- **Dangling lambda captures.** Capturing by reference (`[&]`) or capturing a pointer/`this` and then letting the lambda *outlive* the referent ⇒ dangling reference / use-after-free (UB). Especially deadly for lambdas stored in `std::function`, posted to a thread pool/coroutine, or returned. **Capture by value (`[=]`) for escaping lambdas**; C++14 init-capture `[p = std::move(p)]` to move ownership in; C++20 changed `[=]`-implicit-`this` rules (deprecated, must write `[=, this]`). Capturing `this` is capturing a *raw pointer to the object*, not its members.
- **`std::function` hidden heap allocation** for captures larger than the implementation's SBO buffer, plus a non-inlinable indirect call. In hot paths this is a measurable cost; reach for a template param, `function_ref` (C++26), or `move_only_function` (C++23) instead. SBO size is *not portable* — never rely on a particular capture fitting inline.
- **Ranges/views debug performance.** Views are zero-cost only when *optimized*. In **debug/`-O0`** builds the deep template + iterator-adaptor nesting compiles to many tiny non-inlined function calls and can be **dramatically slower** than a hand loop (and bloats compile time + diagnostics). Also: `views::filter` produces a non-common, input-ish range — re-iterating it re-runs the predicate, and `filter_view`'s cached `begin()` can dangle if the underlying range is mutated after first iteration. Views borrowing from temporaries dangle — only `borrowed_range`s (e.g. `span`, `string_view`, `ref_view`) are safe to outlive the expression.
- **`reduce`/parallel + floating point:** non-deterministic results due to reordering; don't use where bit-reproducibility matters.
- **`std::move` is not a move**; it's a cast to rvalue. Moving from an object leaves it in a *valid but unspecified* state — don't read it expecting the old value. "Functional" pipelines that move out of a captured/aliased object then reuse it are a common bug.
- **Over-`constexpr`-ing** explodes compile time and template instantiation depth; `consteval` mis-placed forces evaluation contexts you didn't intend. Measure compile-time cost too.

### Quick decision rules for the agent

- Replace a hand loop with the *named algorithm/range* that states intent — unless profiling shows the loop is hot and the view chain is slower (debug builds, filter re-evaluation).
- Make it `const`/`constexpr` first; relax only when a write or runtime input forces it. `constinit` for globals you want statically initialized but mutable.
- Callback param you don't store → `function_ref`/template/`auto`; need to own + move → `move_only_function`; own + copy → `copyable_function`(C++26) / `std::function`(legacy). In C → fn-pointer + `void*`.
- Error you must handle locally with a cause → `std::expected`; absence with no cause → `optional`; rare cross-cutting failure → exceptions; C → consistent return codes + early-return + `goto cleanup`.
- Hot loop over many homogeneous records → consider SoA / hot-cold split / `alignas(64)`; otherwise keep AoS for clarity.
- Recursion on untrusted input → bound depth or go iterative (DoS).

### Sources
- cppreference.com — std::expected, std::optional monadic ops (and_then/transform/or_else/transform_error), std::function / std::move_only_function / std::function_ref, std::ranges/std::views, std::ranges::fold_left, std::reduce/accumulate, consteval, constinit, std::hardware_destructive_interference_size — https://en.cppreference.com/w/cpp
- C++ Core Guidelines, B. Stroustrup & H. Sutter (Con.1–Con.5 const-correctness; F.50 use a lambda when a function won't do; T.1/algorithms over loops; ES.* expressions & statements; CP.2 concurrency & const) — https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines
- ISO WG21 papers: P0323 std::expected; P2505 monadic operations for std::expected; P2322 (Barry Revzin) ranges::fold; P0288 std::move_only_function; P1938 if consteval; P1073 (consteval/immediate functions) — https://www.open-std.org/jtc1/sc22/wg21/docs/papers/
- ISO/IEC 9899:2024 (C23) working draft N3220 — [[reproducible]] / [[unsequenced]] attributes; CERT C Secure Coding (MEM05-C avoid large stack allocations / uncontrolled recursion), CWE-674 Uncontrolled Recursion — https://www.open-std.org/jtc1/sc22/wg14/www/docs/n3220.pdf
- Mike Acton, 'Data-Oriented Design and C++' (CppCon 2014); Richard Fabian, 'Data-Oriented Design' (dataorienteddesign.com); Scott Meyers, Effective Modern C++ (Items 31–34 lambdas/captures); Agner Fog optimization manuals (cache lines, SoA/AoS, false sharing) — https://www.dataorienteddesign.com/dodbook/
