#pragma once
// Signal.hpp - Lightweight signals for non-Qt classes
// Because sometimes you don't want QObject overhead

#include <functional>
#include <vector>
#include <algorithm>
#include <mutex>
#include <type_traits>
#include <utility>

namespace vc {

template<typename... Args>
class Signal {
public:
    using Slot = std::function<void(Args...)>;
    using SlotId = std::size_t;

    // Fan-out is a compile-time property of the signal, not a per-emit runtime
    // branch. `Slot`'s invoker takes every argument by value, so a payload that
    // cannot be copied cannot be delivered to two subscribers: the second one
    // would receive the same xvalue the first already moved out of. Saying so
    // here is a compile error at the declaration, whereas checking it in
    // `emitSignal` would be an assert that compiles out under NDEBUG -- and
    // the distributable builds are Release, so that check would leave a
    // release-only silent-corruption path. References are judged on the
    // pointee, so `Signal<const std::vector<SunoClip>&>` is judged on the
    // vector.
    static_assert(
            (std::is_copy_constructible_v<std::remove_cvref_t<Args>> && ... && true),
            "Signal payload must be copy-constructible: every subscriber but the "
            "last receives a copy, and a runtime check for that would compile out "
            "of Release builds");
    
private:
    struct Connection {
        SlotId id;
        Slot callback;
        bool active{true};
    };
    
    std::vector<Connection> slots_;
    SlotId nextId_{0};
    mutable std::mutex mutex_;
    /// Depth of the current emit chain. A slot may emit this same signal --
    /// layered bridges make that easy -- so `emitSignal` is re-entrant, and only
    /// the outermost emit may touch `slots_`. A plain bool was wrong here: the
    /// inner emit's `false` cleared the flag the outer one still depended on,
    /// which both ran the sweep early and routed the outer emit's remaining
    /// `disconnect` calls straight to `erase_if` on the live connection list.
    std::size_t emitDepth_{0};
    /// Set only when a mark actually changed a connection, so the outermost
    /// emit's sweep is skipped on the overwhelmingly common path where nothing
    /// was disconnected. Cleared by that sweep, and never anywhere else, so it
    /// cannot be lost before the emit chain that set it unwinds.
    bool cleanupPending_{false};
    
    /// Leaves the emit chain: only the emit whose decrement lands on zero owns
    /// the sweep, and a subscriber can throw, so this runs from a destructor
    /// (`EmitGuard`) where it is the only code that gets to run. A depth
    /// stranded above zero would disable the deferred cleanup for the rest of
    /// the Signal's life: every later `disconnect` would take the mark-only
    /// path, so dead connections would be called forever and never swept.
    ///
    /// `noexcept` is sound because the mutex is never held across a slot
    /// invocation -- `connect`, `disconnect` and `disconnectAll` only touch
    /// `slots_` -- so the lock here cannot deadlock, and `std::erase_if` on a
    /// vector of `std::function` cannot throw.
    void leaveEmit() noexcept {
        std::lock_guard lock(mutex_);
        if (--emitDepth_ > 0) {
            // An outer emit is still iterating its own snapshot; it owns the
            // sweep, and the flag stays set for it.
            return;
        }
        if (cleanupPending_) {
            // Cleanup inactive connections
            std::erase_if(slots_, [](const Connection& c) { return !c.active; });
            cleanupPending_ = false;
        }
    }
    
    /// RAII owner of one level of the emit chain. The destructor is the only
    /// thing that can run when a subscriber throws, so the decrement lives there
    /// rather than at the end of `emitSignal`.
    class EmitGuard {
    public:
        explicit EmitGuard(Signal& owner) noexcept : owner_(&owner) {}
        ~EmitGuard() { owner_->leaveEmit(); }
        
        EmitGuard(const EmitGuard&) = delete;
        EmitGuard& operator=(const EmitGuard&) = delete;
        EmitGuard(EmitGuard&&) = delete;
        EmitGuard& operator=(EmitGuard&&) = delete;
    
    private:
        Signal* owner_;
    };
    
public:
    Signal() = default;
    ~Signal() = default;
    
    // Non-copyable, moveable
    Signal(const Signal&) = delete;
    Signal& operator=(const Signal&) = delete;
    Signal(Signal&&) noexcept = default;
    Signal& operator=(Signal&&) noexcept = default;
    
    // Connect a callback, returns ID for disconnection
    SlotId connect(Slot callback) {
        std::lock_guard lock(mutex_);
        SlotId id = nextId_++;
        slots_.push_back({id, std::move(callback), true});
        return id;
    }
    
    // Disconnect by ID
    void disconnect(SlotId id) {
        std::lock_guard lock(mutex_);
        if (emitDepth_ == 0) {
            std::erase_if(slots_, [id](const Connection& c) { return c.id == id; });
        } else {
            // Mark as inactive, cleanup later. The flag records that something
            // actually changed, so an outer emit that never has anything to
            // sweep skips the sweep.
            for (auto& conn : slots_) {
                if (conn.id == id && conn.active) {
                    conn.active = false;
                    cleanupPending_ = true;
                }
            }
        }
    }
    
    // Disconnect all
    void disconnectAll() {
        std::lock_guard lock(mutex_);
        if (emitDepth_ == 0) {
            slots_.clear();
        } else {
            for (auto& conn : slots_) {
                if (conn.active) {
                    conn.active = false;
                    cleanupPending_ = true;
                }
            }
        }
    }
    
    // Emit signal to all connected slots
    //
    // Perfect forwarding, deliberately. The old signature took the payload by
    // value and passed it on as an lvalue, so every subscriber
    // copy-constructed it: on `frameCaptured` that is ~8 MB of memcpy per frame
    // per subscriber on the GUI thread, immediately after a GPU readback that
    // already cost the same again. A `std::vector<u8>` move is a 24-byte buffer
    // steal, so copies and moves are not comparable costs -- the defect was
    // always measured in bytes, and a copy is what it was.
    //
    // `Ts&&...` is a *deduced* forwarding reference and not `Args&&...` on
    // purpose. `Args` is pinned by the class template parameter list, which
    // makes `Args&&` an rvalue reference to a fixed type rather than a
    // forwarding reference: it would reject every non-const lvalue caller
    // (currentPos_, currentIndex_, state_, index, name, ...), of which this
    // tree has about twenty. Deducting per call accepts lvalues, rvalues and
    // const lvalues and forwards the value category exactly. The price is that
    // deduction constrains nothing against the declared payload, so the
    // static_assert at the top of `emitSignal` restores that check.
    //
    // A move is destructive, so the *last* active subscriber takes the
    // forwarded value and every earlier one takes a copy of the still-intact
    // payload; forwarding to all of them would hand subscribers 2..N a
    // moved-from object. `frameCaptured` has exactly one production subscriber
    // (VisualizerWindow.cpp:83; the only other connects are in tests), so the
    // hot path takes the forwarded branch and pays zero copies -- which is the
    // entire point of the change. Measured, old to new:
    //
    //     1 sub, rvalue    copies 1 -> 0    moves 2 -> 2
    //     1 sub, lvalue    copies 2 -> 1    moves 1 -> 1
    //     2 sub, lvalue    copies 3 -> 2    moves 2 -> 2
    //     3 sub, rvalue    copies 3 -> 2    moves 4 -> 4
    //
    // Strictly copy-reducing and move-neutral in every case. Note the old code
    // was never "N copies + 1 move": the by-value parameter meant N copies and
    // N+1 moves.
    template<typename... Ts>
    void emitSignal(Ts&&... args) {
        // The call site no longer constrains anything by itself, so constrain it
        // here -- otherwise a wrong-typed call would fail deep inside
        // `std::function`'s invoker, naming the subscriber's lambda parameter
        // instead of the signal's payload.
        static_assert(
                sizeof...(Ts) == sizeof...(Args)
                        && (std::is_convertible_v<Ts, Args> && ... && true),
                "emitSignal arguments must match the signal's declared payload");

        std::vector<Slot> slotsToCall;
        {
            std::lock_guard lock(mutex_);
            slotsToCall.reserve(slots_.size());
            for (const auto& conn : slots_) {
                if (conn.active) {
                    slotsToCall.push_back(conn.callback);
                }
            }
            // Entered last, inside the same critical section: if building the
            // snapshot throws, the depth is left untouched, and nothing between
            // this increment and the guard can throw either -- so the guard
            // below owns the decrement unconditionally.
            ++emitDepth_;
        }
        const EmitGuard guard{*this};

        for (std::size_t i = 0, n = slotsToCall.size(); i < n; ++i) {
            if (i + 1 == n) {
                slotsToCall[i](std::forward<Ts>(args)...);
            } else {
                slotsToCall[i](args...);
            }
        }
    }
    
    // Operator() shorthand
    template<typename... Ts>
    void operator()(Ts&&... args) { emitSignal(std::forward<Ts>(args)...); }
    
    // Check if any slots connected
    [[nodiscard]] bool hasConnections() const {
        std::lock_guard lock(mutex_);
        return !slots_.empty();
    }
    
    [[nodiscard]] std::size_t connectionCount() const {
        std::lock_guard lock(mutex_);
        return slots_.size();
    }
};

} // namespace vc