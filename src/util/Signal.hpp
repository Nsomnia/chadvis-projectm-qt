#pragma once
// Signal.hpp - Lightweight signals for non-Qt classes
// Because sometimes you don't want QObject overhead

#include <functional>
#include <vector>
#include <algorithm>
#include <mutex>
#include <utility>

namespace vc {

template<typename... Args>
class Signal {
public:
    using Slot = std::function<void(Args...)>;
    using SlotId = std::size_t;
    
private:
    struct Connection {
        SlotId id;
        Slot callback;
        bool active{true};
    };
    
    std::vector<Connection> slots_;
    SlotId nextId_{0};
    mutable std::mutex mutex_;
    bool emitting_{false};
    
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
        if (emitting_) {
            // Mark as inactive, cleanup later
            for (auto& conn : slots_) {
                if (conn.id == id) conn.active = false;
            }
        } else {
            std::erase_if(slots_, [id](const Connection& c) { return c.id == id; });
        }
    }
    
    // Disconnect all
    void disconnectAll() {
        std::lock_guard lock(mutex_);
        if (emitting_) {
            for (auto& conn : slots_) conn.active = false;
        } else {
            slots_.clear();
        }
    }
    
    // Emit signal to all connected slots
    void emitSignal(Args... args) {
        std::lock_guard lock(mutex_);
        emitting_ = true;
        for (const auto& conn : slots_) {
            if (conn.active) {
                conn.callback(args...);
            }
        }
        emitting_ = false;
        
        // Cleanup inactive connections
        std::erase_if(slots_, [](const Connection& c) { return !c.active; });
    }
    
    // Operator() shorthand
    void operator()(Args... args) { emitSignal(std::forward<Args>(args)...); }
    
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

// RAII connection guard
template<typename... Args>
class ScopedConnection {
    Signal<Args...>* signal_{nullptr};
    typename Signal<Args...>::SlotId id_{0};
    
public:
    ScopedConnection() = default;
    
    ScopedConnection(Signal<Args...>& signal, typename Signal<Args...>::Slot callback)
        : signal_(&signal)
        , id_(signal.connect(std::move(callback))) {}
    
    ~ScopedConnection() {
        if (signal_) signal_->disconnect(id_);
    }
    
    // Non-copyable, moveable
    ScopedConnection(const ScopedConnection&) = delete;
    ScopedConnection& operator=(const ScopedConnection&) = delete;
    
    ScopedConnection(ScopedConnection&& other) noexcept
        : signal_(std::exchange(other.signal_, nullptr))
        , id_(other.id_) {}
    
    ScopedConnection& operator=(ScopedConnection&& other) noexcept {
        if (this != &other) {
            if (signal_) signal_->disconnect(id_);
            signal_ = std::exchange(other.signal_, nullptr);
            id_ = other.id_;
        }
        return *this;
    }
    
    void disconnect() {
        if (signal_) {
            signal_->disconnect(id_);
            signal_ = nullptr;
        }
    }
};

} // namespace vc