#pragma once
// CredentialStore.hpp - secret storage with OS keychain where available.
//
// Backend selection:
//  - macOS (and !CHADVIS_NO_KEYCHAIN): Security.framework generic-password
//    keychain items (SecItemAdd / SecItemCopyMatching / SecItemDelete),
//    service name "chadvis-projectm-qt", account = namespaced key.
//  - Everything else, and builds compiled with CHADVIS_NO_KEYCHAIN:
//    plaintext file under QStandardPaths::AppDataLocation/"secrets/<key>"
//    written with 0600 permissions. Reduced security - a one-time warning is
//    logged on first use.
//
// Secrets are never logged; use redact() when a value must appear in a log.

#include "util/Result.hpp"
#include <QString>
#include <memory>

namespace vc::suno::auth {

class CredentialStore {
public:
    /// Which storage backend to use.
    enum class Backend {
        Default,  ///< Keychain when compiled support exists, file fallback otherwise.
        Keychain, ///< Force the macOS keychain (falls back to file if unsupported).
        File,     ///< Force the plaintext-file backend (tests / CHADVIS_NO_KEYCHAIN).
    };

    /// @param backend   storage backend selection (see enum).
    /// @param fileRoot  override for the file backend's root directory; empty
    ///                  uses AppDataLocation/"secrets". Used by unit tests to
    ///                  stay hermetic.
    explicit CredentialStore(Backend backend = Backend::Default,
                             QString fileRoot = {});
    ~CredentialStore();
    CredentialStore(const CredentialStore&) = delete;
    CredentialStore& operator=(const CredentialStore&) = delete;

    /// Persist `secret` under `key` (upsert). Keys look like "suno/default";
    /// the "chadvis" namespace is applied internally.
    [[nodiscard]] Result<void> store(const QString& key, const QString& secret);

    /// Load the secret previously stored under `key`; error when absent.
    [[nodiscard]] Result<QString> load(const QString& key);

    /// Delete the secret under `key`. Removing an absent key is not an error.
    [[nodiscard]] Result<void> remove(const QString& key);

    /// Log-safe representation of any secret: "****(len N)".
    [[nodiscard]] static QString redact(const QString& secret);

    /// Internal storage interface. Public only so the backends (defined in
    /// the .cpp translation unit) can implement it; not part of the API.
    struct ISecretBackend {
        virtual ~ISecretBackend() = default;
        virtual Result<void> store(const QString& nsKey, const QString& secret) = 0;
        virtual Result<QString> load(const QString& nsKey) = 0;
        virtual Result<void> remove(const QString& nsKey) = 0;
    };

private:
    static std::unique_ptr<ISecretBackend> makeBackend(Backend backend, const QString& fileRoot);

    std::unique_ptr<ISecretBackend> backend_;
};

} // namespace vc::suno::auth
