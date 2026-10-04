#pragma once
// CredentialStore.hpp - secret storage in the OS secret store, fail closed.
//
// Backend selection is a pure function of (platform, compiled support, runtime
// availability); see decideBackend() below. Every backend is an OS secret store:
//
//  - macOS  (CHADVIS_HAS_KEYCHAIN): Security.framework generic-password items
//    (SecItemUpdate / SecItemAdd / SecItemCopyMatching / SecItemDelete).
//  - Windows(CHADVIS_HAS_WIN32_CREDENTIALS): Win32 Credential Manager generic
//    credentials (CredWriteW / CredReadW / CredDeleteW).
//  - Linux  (CHADVIS_HAS_SECRET_SERVICE): freedesktop Secret Service over
//    D-Bus, "plain" session algorithm, no interactive unlock.
//  - Everything else, or any runtime unavailability: **no storage at all**.
//    store() and load() return a named Unavailable error.
//
// The plaintext 0600-file backend is NOT a fallback. It is reachable only by
// explicitly asking for CredentialStore::Backend::File (the loud, opt-in
// escape hatch used by tests and by CHADVIS_NO_KEYCHAIN builds), because a
// silent downgrade is exactly the defect these backends exist to remove: the
// credential being protected is a Clerk cookie carrying a refresh token with
// Max-Age=31536000, i.e. a one-year bearer secret.
//
// Secrets are never logged; use redact() when a value must appear in a log.

#include <QByteArray>
#include <QString>
#include <expected>
#include <limits>
#include <memory>
#include "util/Result.hpp"

namespace vc::suno::auth {

// ---------------------------------------------------------------------------
// Pure backend-selection policy.
//
// Nothing below includes a platform header, so the whole decision is testable
// on any host, including one that has neither keychain nor Secret Service.
// ---------------------------------------------------------------------------

/// Which OS we are on, as a value rather than a preprocessor branch.
enum class SecretStorePlatform {
    Apple,
    Windows,
    Linux,
    Unknown,
};

/// The backend a set of conditions selects.
enum class BackendChoice {
    AppleKeychain,
    WindowsCredentialManager,
    LinuxSecretService,
    /// Never selected by decideBackend(); only by the explicit opt-in ctor arg.
    PlaintextFile,
    /// Fail closed. The caller gets a named error and no secret is written.
    Unavailable,
};

/// Why BackendChoice::Unavailable was selected. Every value names a distinct
/// user-actionable condition - an unnamed "unavailable" is indistinguishable
/// from a bug in a bug report.
enum class UnavailableReason {
    None,
    /// Not one of the three supported desktops.
    PlatformUnsupported,
    /// This platform has a backend but the build did not compile it in.
    BackendNotCompiled,
    /// CHADVIS_NO_KEYCHAIN: the user asked for no OS secret store.
    DisabledByBuild,
    /// Linux: no D-Bus session bus (headless box, container, no desktop).
    NoSessionBus,
    /// Linux: a bus exists but nobody owns org.freedesktop.secrets.
    ServiceNotRunning,
    /// Linux: the service is up but publishes no "default" collection alias.
    NoDefaultCollection,
    /// Linux: the collection exists but is locked. We deliberately never call
    /// Unlock(), because it may raise a GUI prompt and every caller of this
    /// class is on a worker thread.
    CollectionLocked,
    /// macOS: the keychain refused a non-interactive read.
    KeychainAccessDenied,
};

/// Verdict of the pure, no-I/O half of the Linux availability check. The
/// caller fills it in from D-Bus calls it actually made.
struct SecretServiceProbe {
    bool sessionBusConnected{false};
    bool serviceRegistered{false};
    bool defaultCollectionPresent{false};
    bool collectionLocked{false};
};

/// Inputs to the policy. Defaults describe "a build with no backend compiled",
/// which resolves to Unavailable/BackendNotCompiled rather than to a file.
struct SecretStoreInputs {
    SecretStorePlatform platform{SecretStorePlatform::Unknown};
    bool compiledAppleKeychain{false};
    bool compiledWindowsCredentialManager{false};
    bool compiledSecretService{false};
    /// CHADVIS_NO_KEYCHAIN. Disables every OS backend, including the ones this
    /// build does support, and is the only route to PlaintextFile.
    bool keychainDisabledByBuild{false};
    /// Live Secret Service verdict from classifySecretService(). Ignored off Linux.
    SecretServiceProbe secretService{};
};

enum class SecretServiceVerdict {
    Usable,
    NoSessionBus,
    ServiceNotRunning,
    NoDefaultCollection,
    CollectionLocked,
};

/// The Linux availability rule, as a pure function of observed facts.
[[nodiscard]] SecretServiceVerdict classifySecretService(const SecretServiceProbe& probe) noexcept;

/// Maps UnavailableReason onto the SecretServiceVerdict that produced it, so
/// the dispatch and the message cannot drift apart.
[[nodiscard]] UnavailableReason reasonForVerdict(SecretServiceVerdict verdict) noexcept;

/// The backend decision. `isSecure()` is false only for PlaintextFile and
/// Unavailable, which is what CredentialStore::isSecureBackend() reports.
struct BackendDecision {
    BackendChoice choice{BackendChoice::Unavailable};
    UnavailableReason reason{UnavailableReason::BackendNotCompiled};

    [[nodiscard]] bool isSecure() const noexcept {
        return choice == BackendChoice::AppleKeychain ||
               choice == BackendChoice::WindowsCredentialManager ||
               choice == BackendChoice::LinuxSecretService;
    }
    [[nodiscard]] bool isAvailable() const noexcept { return choice != BackendChoice::Unavailable; }
};

/// THE policy function. Pure, no platform headers, no I/O.
[[nodiscard]] BackendDecision decideBackend(const SecretStoreInputs& inputs) noexcept;

/// Human-readable, actionable text for a refusal. Contains no secret.
[[nodiscard]] QString describeUnavailable(UnavailableReason reason);

// ---------------------------------------------------------------------------
// Credential-encode policy: the bytes and the names, decided before any syscall.
//
// A one-year refresh credential must be stored byte-exact or not at all, so the
// encode is done once, here, where it can be asserted on exactly - and where a
// corrupt string is refused rather than silently transformed.
// ---------------------------------------------------------------------------

/// Why a secret was refused before it reached a platform API.
enum class SecretEncodeDefect {
    None,
    /// QString contains an unpaired UTF-16 surrogate, so it has no UTF-8
    /// encoding. Measured on Qt 6.11.1: toUtf8() *drops* the lone surrogate
    /// entirely (QString(QChar(0xD800)) + "abc" -> 3 bytes "abc", and the
    /// result does not contain U+FFFD), so encoding would silently store a
    /// DIFFERENT credential than the caller handed us and read() would then
    /// return something no one ever stored. Refusing is the only honest option.
    UnpairedSurrogate,
    /// Exceeds the largest blob any supported OS secret store accepts. See
    /// kMaxSecretBlobBytes for the measured per-platform numbers.
    TooLarge,
};

/// CRED_MAX_CREDENTIAL_BLOB_SIZE, from <wincred.h>. Measured: a 2560-character
/// ASCII secret encodes to exactly 2560 bytes, so this is the true ceiling and
/// not a rounded one. CredentialStoreWin32.cpp static_asserts its own copy of
/// the constant against the SDK's.
inline constexpr int kMaxSecretBlobBytes = 2560;

/// Human-readable text for an encode refusal. Takes the secret only to report
/// its length; must never include its contents, because it is built to be
/// logged verbatim.
[[nodiscard]] QString describeEncodeDefect(SecretEncodeDefect defect, const QString& secret);

/// UTF-8 encodes `secret` for storage, or explains why it will not.
///
/// The returned QByteArray is NOT NUL-terminated and its size is the stored
/// length; every backend must write that length rather than a terminator.
[[nodiscard]] std::expected<QByteArray, SecretEncodeDefect> encodeSecret(const QString& secret);

/// The macOS keychain account / Windows TargetName / Secret Service label for a
/// namespaced key. One function so the three platforms cannot drift.
[[nodiscard]] QString namespacedKeyFor(const QString& key);

/// Everything the Win32 credential manager needs, decided without a syscall.
struct WindowsCredentialPlan {
    /// CREDENTIALW::TargetName. Stable across machines and releases, so a
    /// future rename cannot orphan a stored credential.
    QString targetName;
    /// CREDENTIALW::UserName. Presentational only; Credential Manager lists it.
    QString userName;
    /// CREDENTIALW::CredentialBlob. Not NUL-terminated.
    QByteArray blob;
    /// CREDENTIALW::CredentialBlobSize. Always blob.size() - never +1. The blob
    /// is binary and a trailing NUL would become part of the stored secret,
    /// which then differs from the caller's string on read-back.
    int blobSize{0};
};

/// Plans one Win32 generic credential. Pure; refuses before any API call.
[[nodiscard]] std::expected<WindowsCredentialPlan, SecretEncodeDefect>
planWindowsCredential(const QString& key, const QString& secret);

/// Platform-neutral classification of a Win32 GetLastError() from CredReadW.
enum class CredReadStatus {
    Success,
    /// ERROR_NOT_FOUND: the credential is genuinely absent. Distinct from every
    /// other failure, which must surface as an error rather than as "no
    /// credential" - conflating them turns a locked or corrupt store into a
    /// silently signed-out user.
    NotFound,
    /// ERROR_ACCESS_DENIED / ERROR_NO_LOGON_SESSION.
    AccessDenied,
    Failed,
};

/// ERROR_SUCCESS as returned by CredReadW/CredDeleteW. Spelled out rather than
/// taken from <winerror.h> so this classifier compiles - and is therefore
/// testable - on a host with no Windows SDK at all.
inline constexpr unsigned long kWinErrorSuccess = 0;
/// ERROR_NOT_FOUND as returned by CredReadW/CredDeleteW.
inline constexpr unsigned long kWinErrorNotFound = 1168;
inline constexpr unsigned long kWinErrorAccessDenied = 5;
inline constexpr unsigned long kWinErrorNoLogonSession = 1312;

/// Unit-testable mapping; load() also preserves the raw Win32 code in
/// vc::Error::code.
[[nodiscard]] CredReadStatus classifyCredReadStatus(unsigned long lastError) noexcept;

/// True when the platform's blob ceiling is known to be smaller than ours.
/// Windows is the binding constraint today; asserted rather than assumed so a
/// future backend with a tighter limit cannot pass silently.
[[nodiscard]] constexpr int secretBlobCeilingFor(SecretStorePlatform platform) noexcept {
    return platform == SecretStorePlatform::Windows ? kMaxSecretBlobBytes
                                                    : std::numeric_limits<int>::max();
}

class CredentialStore {
public:
    /// Which storage backend to use.
    enum class Backend {
        Default,  ///< decideBackend(): the OS secret store, or fail closed.
        Keychain, ///< The OS secret store for this platform; fail closed if none.
        File,     ///< Explicitly opt in to the plaintext 0600-file backend.
    };

    /// Platform-neutral classification of a macOS Security.framework status.
    enum class KeychainReadStatus {
        Success,
        NotFound,
        AccessDenied,
        Failed,
    };

    /// Unit-testable mapping; `load()` also preserves the raw OSStatus in
    /// vc::Error::code for keychain reads.
    [[nodiscard]] static KeychainReadStatus classifyKeychainReadStatus(int osStatus);

    /// @param backend   storage backend selection (see enum).
    /// @param fileRoot  override for the file backend's root directory; empty
    ///                  uses AppDataLocation/"secrets". Used by unit tests to
    ///                  stay hermetic.
    explicit CredentialStore(Backend backend = Backend::Default, QString fileRoot = {});
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

    /// True only when this instance is backed by the OS keychain, not the
    /// plaintext 0600-file fallback.
    [[nodiscard]] bool isSecureBackend() const;

    /// Log-safe representation of any secret: "****(len N)".
    [[nodiscard]] static QString redact(const QString& secret);

    /// Internal storage interface. Public only so the backends (defined in
    /// their own translation units) can implement it; not part of the API.
    struct ISecretBackend {
        virtual ~ISecretBackend() = default;
        virtual Result<void> store(const QString& nsKey, const QString& secret) = 0;
        virtual Result<QString> load(const QString& nsKey) = 0;
        virtual Result<void> remove(const QString& nsKey) = 0;
        /// True only for real OS secret stores. A backend that answers true
        /// and is not one is a security bug, so each implementation returns a
        /// literal rather than consulting a flag.
        [[nodiscard]] virtual bool isSecure() const noexcept = 0;
    };

private:
    static std::unique_ptr<ISecretBackend> makeBackend(Backend backend, const QString& fileRoot);

    std::unique_ptr<ISecretBackend> backend_;
};

namespace detail {

/// Live availability of the Secret Service, filled in from real D-Bus calls.
/// Returns an all-false probe when CHADVIS_HAS_SECRET_SERVICE is not compiled,
/// so the caller has exactly one code path.
[[nodiscard]] SecretServiceProbe probeSecretService();

/// Constructors for the platform backends, defined in their own translation
/// units. Each returns nullptr when the platform's backend was not compiled in,
/// which is how the dispatcher stays free of platform headers:
///   CredentialStoreWin32.cpp        -> makeWindowsCredentialBackend()
///   CredentialStoreSecretService.cpp-> makeSecretServiceBackend()
///
/// Both are declared unconditionally and defined unconditionally, so the
/// dispatch never needs a matching #ifdef on the *other* platform's build.
[[nodiscard]] std::unique_ptr<CredentialStore::ISecretBackend> makeWindowsCredentialBackend();

/// Linux Secret Service backend. Performs its own availability probe on
/// construction and returns nullptr when no usable unlocked collection exists.
[[nodiscard]] std::unique_ptr<CredentialStore::ISecretBackend> makeSecretServiceBackend();

/// The D-Bus half of the Linux availability check, defined in
/// CredentialStoreSecretService.cpp. Returns an all-false probe when the backend
/// is not compiled, so the caller never needs a second branch.
[[nodiscard]] SecretServiceProbe probeSecretServiceOverDBus();

} // namespace detail
} // namespace vc::suno::auth
