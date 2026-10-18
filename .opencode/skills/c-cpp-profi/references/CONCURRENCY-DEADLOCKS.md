# Concurrency And Deadlocks

## Principle

C/C++ concurrency defects are proof problems. A finding is not real until the agent can name the threads, locks, operations, and ordering edge that reaches the bad state. A fix is not done until the same path is rerun under the best available dynamic or stress evidence.

## Required Workflow

1. Inventory every concurrency primitive in scope: `pthread_*`, C11 threads/atomics, `std::thread`, `std::mutex`, `std::shared_mutex`, `std::atomic`, condition variables, semaphores, futex wrappers, thread pools, callbacks, signal handlers, and event loops.
2. Draw a thread and ownership map: who owns state, who can mutate it, which functions borrow it, and which callbacks can re-enter.
3. Draw a lock-order graph for every nested lock. A total order is a proof; an inconsistent order is a defect candidate.
4. Construct a concrete interleaving for each race, deadlock, livelock, or lost wakeup before reporting it.
5. Run the applicable gate: TSan, Helgrind/DRD, rr, stress runner, debugger backtraces, or targeted unit/replay tests.
6. Audit for the fourth instance. A found deadlock pattern usually has siblings.
7. Record false positives and why they are safe.

Do not hold locks while calling user callbacks, FFI/plugin callbacks, allocator hooks, logging hooks, signal handlers, destructors, constructors, `dlopen`/`dlclose`, or code that can re-enter the library. Prefer one owner, one queue, or a total lock order over recursive locks and trylock timeouts.

## Deadlock Classes

| Class | Native shape | Required proof |
|---|---|---|
| AB-BA lock cycle | T1 holds A and waits for B while T2 holds B and waits for A | wait-for graph or complete lock-order audit |
| Self-deadlock | callback, virtual call, signal, or destructor re-enters a non-recursive lock | call chain from first lock to re-entry |
| Reader upgrade | thread holds read lock and tries to acquire write lock | guard lifetime proof |
| Lost wakeup | notify happens outside the predicate/wait protocol | waiter and notifier interleaving |
| Livelock | retries, CAS loops, or `EAGAIN` loops make activity without progress | progress metric and bounded retry/backoff proof |
| Data race | unsynchronized read/write or write/write of shared memory | TSan report or memory-model interleaving |
| TOCTOU | check and use are split across mutable shared state | mutation window and stale-state consequence |
| Runtime reentrancy | loader, signal, allocator hook, constructor, destructor, or FFI callback reaches locking/allocation | call chain from reentrant entry |

## Static Audit Commands

Use these as prompts, not proofs:

```bash
rg -n '\\b(pthread_mutex_lock|pthread_rwlock_|pthread_cond_wait|std::mutex|std::shared_mutex|std::lock_guard|std::unique_lock|std::scoped_lock)\\b' .
rg -n '\\b(std::thread|pthread_create|std::async|std::jthread|pthread_join|detach\\s*\\()\\b' .
rg -n '\\bmemory_order_relaxed|memory_order_acquire|memory_order_release|memory_order_acq_rel|memory_order_seq_cst\\b' .
rg -n '\\b(sigaction|signal\\s*\\(|fork\\s*\\(|pthread_atfork|dlopen|__attribute__\\(\\(constructor\\)\\)|__attribute__\\(\\(destructor\\)\\))\\b' .
rg -n '\\b(malloc|free|new\\s*\\(|delete\\s*)\\b' .  # inspect only inside handlers/hooks/constructors/callbacks
```

For each hit, answer:

- Which thread can run this?
- Which locks are held?
- Can user, plugin, allocator, logger, or signal code re-enter?
- What happens if the value read is stale?
- What invariant makes this safe?

## Lock Order

Rules:

- Assign every lock a stable rank when nested locks exist.
- Acquire locks in ascending rank and release in reverse order.
- Do not call unknown code while holding a lock: user callbacks, virtual dispatch, logging sinks, FFI, allocator hooks, destructors, or signal-facing code.
- Prefer `std::scoped_lock` for acquiring multiple C++ mutexes together when it matches the design.
- Avoid recursive mutexes unless the reentrant contract is documented and tested. Recursive locks often hide an ownership bug.

Evidence:

```text
Lock graph:
- Lock:
- Rank:
- Protected state:
- Acquisition sites:
- Nested acquisitions:
- Calls made while held:
- Reentrancy risk:
```

## Condition Variables

Use predicate-based waits:

```cpp
std::unique_lock<std::mutex> lock(mu);
cv.wait(lock, [&] { return ready; });
```

For POSIX:

```c
pthread_mutex_lock(&mu);
while (!ready) {
    pthread_cond_wait(&cv, &mu);
}
pthread_mutex_unlock(&mu);
```

Rules:

- Wait in a loop or use the predicate overload.
- Mutate the predicate before notification.
- Record whether notification occurs under lock or after unlock and why that is safe for the predicate.
- Keep predicate and wait protected by the same mutex.
- Treat `if` around a condition wait as a bug until a double-check gate pattern is proven.
- Check timeout paths, cancellation paths, spurious wakeups, destruction while waiters exist, and broadcast storms.
- Timeouts are diagnostics, not deadlock fixes.

## Atomics And Memory Ordering

Rules:

- `relaxed` is valid for metrics, hints, or values synchronized by another mechanism. It is not valid for data publication by itself.
- Use release/acquire for publishing initialized data to another thread.
- Use `acq_rel` for read-modify-write synchronization.
- Use `seq_cst` when the design needs one global order or the weaker proof is not clear.
- Do not rely on x86 behavior. Prove on the C/C++ memory model and assume weakly ordered targets exist.
- If a stale read only causes extra work and a later locked path rechecks the predicate, document that as the proof.
- CAS loops need ABA, lifetime, reclamation, failure ordering, backoff, and progress notes.

Evidence:

```text
Atomic proof:
- Atomic object:
- Producer:
- Consumer:
- Published data:
- Store ordering:
- Load ordering:
- Happens-before edge:
- Stale-read consequence:
- Weak-architecture risk:
```

## Thread Lifecycle

Rules:

- Every `pthread_create`, `std::thread`, `std::jthread`, and thread-pool task needs an ownership and shutdown story.
- Join or detach exactly once. Detached threads must not outlive the state they touch.
- `std::thread` must not reach destruction while joinable. Prefer RAII joiners or `std::jthread` where available.
- C++ destructors must not destroy mutexes, condition variables, queues, or buffers while workers can still access them.
- Cancellation paths must release locks and leave shared state consistent.
- For C, document cleanup labels and `pthread_cleanup_push` use where cancellation is possible.
- Thread-local state, per-thread allocator state, and TLS destructors must be checked for shutdown order and reentrancy.

## Signals, Fork, And Loader Reentrancy

Signal handlers:

- Only call async-signal-safe functions. Do not allocate, lock a mutex, log through stdio/iostreams, throw, or touch STL containers.
- Prefer `sig_atomic_t` flags or the self-pipe trick.
- Preserve `errno` when a handler writes to a pipe.

Fork:

- In a multithreaded process, child code after `fork()` may see locks held by threads that no longer exist.
- Prefer `posix_spawn` or `fork` followed immediately by `exec`.
- If `pthread_atfork` is used, acquire all locks in canonical order and reinitialize or release in the child.

Loader and constructors:

- Code reachable from `dlopen`, ELF constructors/destructors, `LD_PRELOAD`, `malloc` hooks, or plugin callbacks must avoid lazy locks, allocation, logging, and callbacks unless the call chain is proven non-reentrant.
- Trace from exported/interposed function to every global initializer and lock.
- Allocator overrides and hooks must be reentrancy-safe: no recursive allocation path, no blocking initialization on hot interposed functions, and no lock taken before the allocator's own state is initialized.
- Plugin and FFI callbacks need a lock contract: locks held on entry, permitted calls, reentrant calls forbidden or allowed, thread affinity, and lifetime of callback data.

## Validation Matrix

| Risk | Required evidence |
|---|---|
| Deadlock/hang | `gdb thread apply all bt`, wait-for graph, lock ownership, reproducer or stress run |
| Data race | TSan when practical; otherwise Helgrind/DRD plus manual happens-before proof |
| Rare interleaving | `rr record --chaos`, stress loop, scheduler perturbation, minimized reproducer |
| Condition variable/lost wakeup | predicate proof, notify ordering, timeout/cancellation test |
| Atomics | release/acquire proof, stale-value consequence, weak-memory review |
| Callback reentrancy | call-chain trace from public entry/callback to locks/allocators |
| Signal/loader | async-signal-safe or loader-safe function list, constructor/destructor order proof |

## Dynamic Gates

Use the strongest practical evidence:

```bash
# ThreadSanitizer
clang++ -fsanitize=thread -g -O1 <sources> -o tsan-target
./tsan-target

# Valgrind thread tools
valgrind --tool=helgrind --error-exitcode=99 <test-or-binary>
valgrind --tool=drd --error-exitcode=99 <test-or-binary>

# Replay and schedule perturbation
rr record --chaos <test-or-binary>
rr replay

# Hung process snapshot
gdb --batch -ex "set pagination off" -ex "thread apply all bt full" -p "$PID"
```

Record unsupported gates explicitly. TSan, ASan, MSan, and some custom allocators are not freely composable.

## False Positives To Avoid

- Do not report a race/deadlock without a concrete interleaving.
- Do not change `memory_order_relaxed` just because it looks weak. First prove stale reads can change correctness.
- Do not flag a consistent total lock order as "risky"; it is the safety proof.
- Do not recommend sleeps for sub-microsecond spin/CAS paths without measuring the expected contention duration.
- Do not flag a signal/loader reentrancy hazard unless that code path can actually be reached from a signal, constructor, destructor, interposed symbol, or callback.
- Do not flag a single-thread/noop mutex build as concurrent unless the documented build configuration can run it concurrently.

## Evidence Packet

```text
Concurrency evidence:
- Scope:
- Thread/ownership map:
- Lock graph:
- Concrete interleavings:
- Data-race/TOCTOU proof:
- Atomic memory-order proofs:
- Condition-variable predicate/notify proof:
- Signal/fork/loader surfaces:
- Callback/FFI/allocator reentrancy:
- Dynamic gates: TSan / Helgrind / DRD / rr / gdb / stress:
- Reproducer/stress command:
- False positives:
- Fixed sibling instances:
- Deferred bead ids:
- Residual risk:
```
