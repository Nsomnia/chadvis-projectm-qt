#pragma once
/**
 * @file QmlSingletonBridge.hpp
 * @file Purpose: CRTP boilerplate removal for QML bridge singletons
 *                (uniform instance()/create() with per-bridge ownership policy).
 *                Does NOT perform QML type registration (see BridgeRegistration)
 *                and does NOT manage engine/backend wiring (bridges keep their
 *                own static backend pointers).
 *
 * @version 1.0.0 - 2026-08-25
 */

#include <QObject>
#include <QQmlEngine>
#include <QtQml/qqml.h>

namespace qml_bridge {

/// Ownership/lifetime policy applied by QmlSingletonBridge::create().
enum class SingletonPolicy {
    /// Every create() call yields a fresh instance (JS ownership by QML engine).
    NewInstancePerCall,
    /// Lazily created once, parented to the QML engine (leaks by design until app exit).
    CachedQmlParented,
    /// Lazily created once, no parent (caller/QML engine governs lifetime).
    CachedUnparented,
    /// Fresh instance parented to the QML engine with explicit CppOwnership.
    CppOwnership,
};

/**
 * @brief CRTP mixin providing the standard QML singleton plumbing.
 *
 * Replaces the per-bridge `static T* s_instance` + `static QObject* create(...)`
 * duplication. Derived classes keep their normal QObject/QAbstractListModel base;
 * this mixin adds no base class and no virtuals.
 */
template <typename Derived, SingletonPolicy Policy = SingletonPolicy::NewInstancePerCall>
class QmlSingletonBridge {
public:
    static QObject* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine) {
        Q_UNUSED(jsEngine)
        if constexpr (Policy == SingletonPolicy::CppOwnership) {
            auto* bridge = new Derived(qmlEngine);
            QQmlEngine::setObjectOwnership(bridge, QQmlEngine::CppOwnership);
            return bridge;
        } else if constexpr (Policy == SingletonPolicy::CachedQmlParented) {
            if (!instance_) {
                instance_ = new Derived(qmlEngine);
            }
            return instance_;
        } else if constexpr (Policy == SingletonPolicy::CachedUnparented) {
            if (!instance_) {
                instance_ = new Derived();
            }
            return instance_;
        } else {
            return new Derived();
        }
    }

    /// Currently-live instance, or nullptr before first construction.
    static Derived* instance() { return instance_; }

protected:
    ~QmlSingletonBridge() = default;

    /// Derived constructors record themselves here (replaces `s_instance = this`).
    static void setInstance(Derived* d) { instance_ = d; }

private:
    static inline Derived* instance_ = nullptr;
};

} // namespace qml_bridge
