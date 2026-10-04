#include "CredentialStore.hpp"

#include "core/Logger.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUtf8StringView>

#ifdef CHADVIS_HAS_KEYCHAIN
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#endif

namespace vc::suno::auth {

// ---------------------------------------------------------------------------
// Pure policy. No platform headers, no I/O: every function here is callable on
// a machine that has neither a keychain nor a Secret Service, which is the only
// way the selection rule can be tested rather than asserted.
// ---------------------------------------------------------------------------

SecretServiceVerdict classifySecretService(const SecretServiceProbe& probe) noexcept {
    // Order is the order in which each fact is discovered on the wire, so the
    // first failure is the most specific one the user can act on. Notably a
    // headless box reports NoSessionBus and not ServiceNotRunning, because
    // there is no bus on which to look for the service at all.
    if (!probe.sessionBusConnected) return SecretServiceVerdict::NoSessionBus;
    if (!probe.serviceRegistered) return SecretServiceVerdict::ServiceNotRunning;
    if (!probe.defaultCollectionPresent) return SecretServiceVerdict::NoDefaultCollection;
    if (probe.collectionLocked) return SecretServiceVerdict::CollectionLocked;
    return SecretServiceVerdict::Usable;
}

UnavailableReason reasonForVerdict(SecretServiceVerdict verdict) noexcept {
    switch (verdict) {
        case SecretServiceVerdict::Usable:
            return UnavailableReason::None;
        case SecretServiceVerdict::NoSessionBus:
            return UnavailableReason::NoSessionBus;
        case SecretServiceVerdict::ServiceNotRunning:
            return UnavailableReason::ServiceNotRunning;
        case SecretServiceVerdict::NoDefaultCollection:
            return UnavailableReason::NoDefaultCollection;
        case SecretServiceVerdict::CollectionLocked:
            return UnavailableReason::CollectionLocked;
    }
    return UnavailableReason::ServiceNotRunning;
}

BackendDecision decideBackend(const SecretStoreInputs& inputs) noexcept {
    // CHADVIS_NO_KEYCHAIN outranks every other input. It is the one condition
    // under which a plaintext file is the *correct* answer rather than the
    // bug, because somebody asked for exactly that at configure time.
    if (inputs.keychainDisabledByBuild) {
        return {BackendChoice::PlaintextFile, UnavailableReason::None};
    }

    switch (inputs.platform) {
        case SecretStorePlatform::Apple:
            if (inputs.compiledAppleKeychain) {
                return {BackendChoice::AppleKeychain, UnavailableReason::None};
            }
            return {BackendChoice::Unavailable, UnavailableReason::BackendNotCompiled};

        case SecretStorePlatform::Windows:
            if (inputs.compiledWindowsCredentialManager) {
                return {BackendChoice::WindowsCredentialManager, UnavailableReason::None};
            }
            return {BackendChoice::Unavailable, UnavailableReason::BackendNotCompiled};

        case SecretStorePlatform::Linux:
            if (!inputs.compiledSecretService) {
                return {BackendChoice::Unavailable, UnavailableReason::BackendNotCompiled};
            }
            // Only now does the runtime probe matter; off Linux it is ignored.
            if (const auto verdict = classifySecretService(inputs.secretService);
                verdict != SecretServiceVerdict::Usable) {
                return {BackendChoice::Unavailable, reasonForVerdict(verdict)};
            }
            return {BackendChoice::LinuxSecretService, UnavailableReason::None};

        case SecretStorePlatform::Unknown:
            break;
    }
    return {BackendChoice::Unavailable, UnavailableReason::PlatformUnsupported};
}

QString describeUnavailable(UnavailableReason reason) {
    switch (reason) {
        case UnavailableReason::None:
            return QStringLiteral("secret storage is available");
        case UnavailableReason::PlatformUnsupported:
            return QStringLiteral(
                    "no OS secret store for this platform; refusing to write the credential to a "
                    "plaintext file. Re-paste it each session, or run a build with a keychain.");
        case UnavailableReason::BackendNotCompiled:
            return QStringLiteral("this build has no OS secret store compiled in; refusing to "
                                  "write the credential "
                                  "to a plaintext file (set CHADVIS_NO_KEYCHAIN=ON to opt in to "
                                  "plaintext storage)");
        case UnavailableReason::DisabledByBuild:
            return QStringLiteral("CHADVIS_NO_KEYCHAIN disabled the OS keychain");
        case UnavailableReason::NoSessionBus:
            return QStringLiteral(
                    "no D-Bus session bus (headless session, container, or no desktop login); "
                    "unlock your keyring and start ChadVis from a desktop session");
        case UnavailableReason::ServiceNotRunning:
            return QStringLiteral(
                    "no Secret Service on the session bus (gnome-keyring / keepassxc / kwallet not "
                    "running); start your keyring, or install one");
        case UnavailableReason::NoDefaultCollection:
            return QStringLiteral("the Secret Service publishes no 'default' collection; unlock "
                                  "your keyring once "
                                  "in your desktop session so it creates one");
        case UnavailableReason::CollectionLocked:
            return QStringLiteral("the default keyring is locked; unlock it in your desktop "
                                  "session. ChadVis will "
                                  "not prompt, because it stores secrets on a background thread");
        case UnavailableReason::KeychainAccessDenied:
            return QStringLiteral(
                    "the OS keychain denied a non-interactive read; unlock the keychain and allow "
                    "this build in Keychain Access");
    }
    return QStringLiteral("secret storage unavailable");
}

QString describeEncodeDefect(SecretEncodeDefect defect, const QString& secret) {
    // Deliberately reports the LENGTH only. This string is designed to be
    // logged, and the value being refused is a one-year bearer credential.
    const QString shape = QStringLiteral("****(len %1)").arg(secret.size());
    switch (defect) {
        case SecretEncodeDefect::None:
            return QStringLiteral("no encode defect");
        case SecretEncodeDefect::UnpairedSurrogate:
            return QStringLiteral("refusing to store %1: it contains an unpaired UTF-16 surrogate, "
                                  "which has no "
                                  "UTF-8 encoding. Storing it would silently write a DIFFERENT "
                                  "credential than the "
                                  "one supplied.")
                    .arg(shape);
        case SecretEncodeDefect::TooLarge:
            return QStringLiteral("refusing to store %1: the UTF-8 form exceeds the %2-byte limit "
                                  "every supported "
                                  "OS secret store imposes. Re-paste the credential after clearing "
                                  "stale cookies.")
                    .arg(shape)
                    .arg(kMaxSecretBlobBytes);
    }
    return QStringLiteral("refusing to store %1").arg(shape);
}

std::expected<QByteArray, SecretEncodeDefect> encodeSecret(const QString& secret) {
    // Measured on Qt 6.11.1: QString::toUtf8() silently DROPS an unpaired
    // surrogate rather than substituting U+FFFD. A 4-char string
    // (U+D800 "abc") encodes to the 3 bytes "abc", so a store()/load() pair
    // would round-trip to a credential that differs from the one the caller
    // handed us - a silent substitution of a one-year bearer secret. Detect it
    // here, where the refusal is reportable, rather than after it is on disk.
    for (qsizetype i = 0; i < secret.size(); ++i) {
        const char16_t unit = secret.at(i).unicode();
        if (unit >= 0xD800 && unit <= 0xDBFF) {
            // High surrogate: valid only when followed by a low surrogate.
            if (i + 1 >= secret.size() || secret.at(i + 1).unicode() < 0xDC00 ||
                secret.at(i + 1).unicode() > 0xDFFF) {
                return std::unexpected(SecretEncodeDefect::UnpairedSurrogate);
            }
            ++i; // consume the pair
        } else if (unit >= 0xDC00 && unit <= 0xDFFF) {
            // Low surrogate with no preceding high one.
            return std::unexpected(SecretEncodeDefect::UnpairedSurrogate);
        }
    }

    QByteArray encoded = secret.toUtf8();
    if (encoded.size() > kMaxSecretBlobBytes) {
        return std::unexpected(SecretEncodeDefect::TooLarge);
    }
    return encoded;
}

QString namespacedKeyFor(const QString& key) {
    // Matches the historical internal key exactly: "chadvis-projectm-qt/chadvis/suno/default".
    // Changing this orphans every credential already on a user's machine, so it
    // is versioned by habit rather than by a constant that someone will helpfully
    // rename.
    if (key.startsWith(QLatin1String("chadvis/"))) {
        return QStringLiteral("chadvis-projectm-qt/%1").arg(key);
    }
    return QStringLiteral("chadvis-projectm-qt/chadvis/%1").arg(key);
}

std::expected<WindowsCredentialPlan, SecretEncodeDefect>
planWindowsCredential(const QString& key, const QString& secret) {
    auto blob = encodeSecret(secret);
    if (!blob) return std::unexpected(blob.error());

    WindowsCredentialPlan plan;
    plan.targetName = namespacedKeyFor(key);
    // Credential Manager groups by UserName in its UI. Presentational only -
    // it is not part of the lookup, which is TargetName alone.
    plan.userName = QStringLiteral("chadvis-projectm-qt");
    plan.blob = *std::move(blob);
    // Explicitly NOT blob.size() + 1. CredWriteW takes a length, and a generic
    // credential's blob is opaque bytes: appending a NUL would store a secret
    // that differs from the caller's string, and read() would then return it
    // with a trailing NUL that fails the next comparison.
    plan.blobSize = static_cast<int>(plan.blob.size());
    return plan;
}

CredReadStatus classifyCredReadStatus(unsigned long lastError) noexcept {
    if (lastError == kWinErrorSuccess) return CredReadStatus::Success;
    // ERROR_NOT_FOUND is the ONLY code that means "absent". Everything else is
    // a real failure and must stay a failure: collapsing a locked or corrupt
    // store into "no credential" produces a silently signed-out user with no
    // error anywhere, which is the exact failure mode named in UnavailableReason.
    if (lastError == kWinErrorNotFound) return CredReadStatus::NotFound;
    if (lastError == kWinErrorAccessDenied || lastError == kWinErrorNoLogonSession) {
        return CredReadStatus::AccessDenied;
    }
    return CredReadStatus::Failed;
}

namespace {

/// Keychain service name (macOS generic-password items).
/// The "chadvis" namespace and this prefix are both applied by
/// namespacedKeyFor(), which is public so all three platforms share it.
constexpr auto kServiceName = QUtf8StringView("chadvis-projectm-qt");

// Security.framework status values are stable API constants. Keep the public
// classification seam buildable on non-Apple test hosts as well.
constexpr int kErrSecAuthFailed = -25293;
constexpr int kErrSecItemNotFound = -25300;
constexpr int kErrSecInteractionNotAllowed = -25308;

/// Full internal key: "<service>/<namespace>/<user-key>", e.g.
/// "chadvis-projectm-qt/chadvis/suno/default". Now a public policy function
/// (namespacedKeyFor) so the three platforms cannot drift on it.
QString namespacedKey(const QString& key) { return namespacedKeyFor(key); }

/// Keep only filesystem-safe characters so keys like "chadvis/suno/default"
/// map to a flat, predictable file name.
QString sanitizedFileName(const QString& nsKey) {
    QString out;
    out.reserve(nsKey.size());
    for (const QChar c : nsKey) {
        const char16_t u = c.unicode();
        const bool safe = (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') ||
                          (u >= '0' && u <= '9') || u == '.' || u == '-' || u == '_';
        out.append(safe ? c : QLatin1Char('_'));
    }
    return out;
}

// ---------------------------------------------------------------------------
// File backend - plaintext with 0600 permissions, atomic replace on write.
//
// NOT A FALLBACK. Reachable only via CredentialStore::Backend::File, which is
// an explicit opt-in (tests, and CHADVIS_NO_KEYCHAIN=ON builds). The dispatch
// below never selects it on its own.
// ---------------------------------------------------------------------------

class FileBackend final : public CredentialStore::ISecretBackend {
public:
    explicit FileBackend(QString fileRoot) : root_(std::move(fileRoot)) {}

    [[nodiscard]] bool isSecure() const noexcept override { return false; }

    Result<void> store(const QString& nsKey, const QString& secret) override {
        auto path = pathFor(nsKey);
        if (!path) return Result<void>::err(path.error());

        QSaveFile file(*path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            return Result<void>::err(QStringLiteral("Cannot open secret file for writing: %1")
                                             .arg(file.errorString())
                                             .toStdString());
        }
        file.write(secret.toUtf8());
        if (!file.commit()) {
            return Result<void>::err(QStringLiteral("Failed to commit secret file: %1")
                                             .arg(file.errorString())
                                             .toStdString());
        }
        // 0600: owner read/write only (no-op where permissions unsupported).
        QFile::setPermissions(*path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        return Result<void>::ok();
    }

    Result<QString> load(const QString& nsKey) override {
        auto path = pathFor(nsKey);
        if (!path) return Result<QString>::err(path.error());

        QFile file(*path);
        if (!file.open(QIODevice::ReadOnly)) {
            return Result<QString>::err(
                    QStringLiteral("No secret stored under '%1'").arg(nsKey).toStdString());
        }
        return Result<QString>::ok(QString::fromUtf8(file.readAll()));
    }

    Result<void> remove(const QString& nsKey) override {
        auto path = pathFor(nsKey);
        if (!path) return Result<void>::err(path.error());
        QFile::remove(*path); // removing an absent key is not an error
        return Result<void>::ok();
    }

private:
    /// Root directory for secret files, creating it on first use.
    Result<QString> rootDir() {
        if (!root_.isEmpty()) return root_;
        if (resolved_.isEmpty()) {
            resolved_ = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
                        QStringLiteral("/secrets");
            warnPlaintextOnce();
        }
        if (resolved_.isEmpty()) {
            return Result<QString>::err(
                    std::string("AppDataLocation unavailable; cannot resolve secret storage dir"));
        }
        return resolved_;
    }

    static void warnPlaintextOnce() {
        static bool warned = false;
        if (!warned) {
            warned = true;
            LOG_WARN("CredentialStore: storing secrets in a PLAINTEXT file with 0600 "
                     "permissions (reduced security). This was explicitly requested; the stored "
                     "cookie carries a one-year refresh token");
        }
    }

    Result<QString> pathFor(const QString& nsKey) {
        auto dir = rootDir();
        if (!dir) return dir;
        if (!QFileInfo::exists(*dir) && !QDir().mkpath(*dir)) {
            return Result<QString>::err(QStringLiteral("Cannot create secret storage directory: %1")
                                                .arg(*dir)
                                                .toStdString());
        }
        return Result<QString>(*dir + QStringLiteral("/") + sanitizedFileName(nsKey));
    }

    QString root_;     // explicit override (tests), checked first
    QString resolved_; // lazily resolved default location
};

#ifdef CHADVIS_HAS_KEYCHAIN

// ---------------------------------------------------------------------------
// macOS keychain backend - Security.framework generic-password items via the
// SecItem* APIs on plain CoreFoundation dictionaries (no ObjC++ required).
// ---------------------------------------------------------------------------

/// Minimal RAII guard for CF type references.
struct CfReleaser {
    void operator()(const void* ref) const {
        if (ref) CFRelease(ref);
    }
};
using CfGuard = std::unique_ptr<const void, CfReleaser>;

class KeychainBackend final : public CredentialStore::ISecretBackend {
public:
    [[nodiscard]] bool isSecure() const noexcept override { return true; }

    Result<void> store(const QString& nsKey, const QString& secret) override {
        // Same encode policy as the Windows and Linux siblings, so a corrupt
        // string is refused identically on all three platforms instead of
        // being silently transformed on one of them.
        const auto encoded = encodeSecret(secret);
        if (!encoded) {
            return Result<void>::err(
                    Error(describeEncodeDefect(encoded.error(), secret).toStdString()));
        }

        CfGuard account(makeCfString(namespacedKey(nsKey)));
        CfGuard service(makeCfString(kServiceName.toString()));
        if (!account || !service)
            return Result<void>::err(std::string("Failed to allocate keychain strings"));

        const QByteArray& bytes = *encoded;
        CfGuard data(CFDataCreate(kCFAllocatorDefault,
                                  reinterpret_cast<const UInt8*>(bytes.constData()),
                                  static_cast<CFIndex>(bytes.size())));
        if (!data) return Result<void>::err(std::string("Failed to allocate keychain data buffer"));

        // Upsert: try an update first; add a fresh item when none exists yet.
        const void* updateKeys[] = {kSecValueData};
        const void* updateVals[] = {data.get()};
        CFDictionaryRef updateDict = cfDict(updateKeys, updateVals, 1);

        const void* searchKeys[] = {kSecClass, kSecAttrService, kSecAttrAccount,
                                    kSecUseDataProtectionKeychain};
        const void* searchVals[] = {kSecClassGenericPassword, service.get(), account.get(),
                                    kCFBooleanTrue};
        CFDictionaryRef searchDict = cfDict(searchKeys, searchVals, 4);

        OSStatus status = SecItemUpdate(searchDict, updateDict);
        if (status == errSecItemNotFound) {
            const void* addKeys[] = {kSecClass, kSecAttrService, kSecAttrAccount, kSecValueData,
                                     kSecUseDataProtectionKeychain};
            const void* addVals[] = {kSecClassGenericPassword, service.get(), account.get(),
                                     data.get(), kCFBooleanTrue};
            CFDictionaryRef addDict = cfDict(addKeys, addVals, 5);
            status = SecItemAdd(addDict, nullptr);
            CFRelease(addDict);
        }
        CFRelease(updateDict);
        CFRelease(searchDict);

        if (status != errSecSuccess) {
            return Result<void>::err(keychainFailure("store", status));
        }
        return Result<void>::ok();
    }

    Result<QString> load(const QString& nsKey) override {
        CfGuard account(makeCfString(namespacedKey(nsKey)));
        CfGuard service(makeCfString(kServiceName.toString()));
        if (!account || !service)
            return Result<QString>::err(std::string("Failed to allocate keychain strings"));

        // Use the data-protection keychain explicitly; legacy CSSM items can
        // enter a blocking SecurityServer decrypt path before ACL/UI policy is
        // evaluated. Never allow a GUI authentication prompt to block startup.
        // A denied read remains a keychain failure; it never falls back to file.
        const void* keys[] = {kSecClass,
                              kSecAttrService,
                              kSecAttrAccount,
                              kSecReturnData,
                              kSecMatchLimit,
                              kSecUseAuthenticationUI,
                              kSecUseDataProtectionKeychain};
        const void* vals[] = {kSecClassGenericPassword,
                              service.get(),
                              account.get(),
                              kCFBooleanTrue,
                              kSecMatchLimitOne,
                              kSecUseAuthenticationUIFail,
                              kCFBooleanTrue};
        CFDictionaryRef query = cfDict(keys, vals, 7);

        CFTypeRef found = nullptr;
        const OSStatus status = SecItemCopyMatching(query, &found);
        CFRelease(query);
        if (status == errSecItemNotFound) {
            return Result<QString>::err(
                    Error(QStringLiteral("No secret stored under '%1'").arg(nsKey).toStdString(),
                          static_cast<int>(status)));
        }
        if (status == errSecInteractionNotAllowed || status == errSecAuthFailed) {
            LOG_WARN("CredentialStore: macOS Keychain denied non-interactive read for service "
                     "'{}'; unlock the login keychain, allow this build in Keychain Access, then "
                     "restart (no plaintext fallback)",
                     kServiceName.toString().toStdString());
            return Result<QString>::err(keychainFailure("read", status));
        }
        if (status != errSecSuccess) {
            return Result<QString>::err(keychainFailure("load", status));
        }

        CfGuard item(found);
        const CFDataRef data = static_cast<CFDataRef>(found);
        return Result<QString>(
                QString::fromUtf8(reinterpret_cast<const char*>(CFDataGetBytePtr(data)),
                                  static_cast<qsizetype>(CFDataGetLength(data))));
    }

    Result<void> remove(const QString& nsKey) override {
        CfGuard account(makeCfString(namespacedKey(nsKey)));
        CfGuard service(makeCfString(kServiceName.toString()));
        if (!account || !service)
            return Result<void>::err(std::string("Failed to allocate keychain strings"));

        const void* keys[] = {kSecClass, kSecAttrService, kSecAttrAccount,
                              kSecUseDataProtectionKeychain};
        const void* vals[] = {kSecClassGenericPassword, service.get(), account.get(),
                              kCFBooleanTrue};
        CFDictionaryRef query = cfDict(keys, vals, 4);
        const OSStatus status = SecItemDelete(query);
        CFRelease(query);

        if (status != errSecSuccess && status != errSecItemNotFound) {
            return Result<void>::err(keychainFailure("remove", status));
        }
        return Result<void>::ok();
    }

private:
    static CFStringRef makeCfString(const QString& s) { return s.toCFString(); }

    static CFDictionaryRef cfDict(const void** keys, const void** vals, CFIndex count) {
        return CFDictionaryCreate(kCFAllocatorDefault, keys, vals, count,
                                  &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    }

    static Error keychainFailure(const char* op, OSStatus status) {
        return Error(QStringLiteral("CredentialStore keychain %1 failed (OSStatus %2)")
                             .arg(QLatin1String(op))
                             .arg(static_cast<int>(status))
                             .toStdString(),
                     static_cast<int>(status));
    }
};

#endif // CHADVIS_HAS_KEYCHAIN

/// Compile-time facts, assembled into the pure policy's input struct. This is
/// the ONLY place that reads the CHADVIS_HAS_* macros for selection; the policy
/// itself never sees a preprocessor symbol, which is what makes it testable on
/// a machine that is none of these platforms.
SecretStoreInputs probeBuildInputs() {
    SecretStoreInputs inputs;
#ifdef Q_OS_MACOS
    inputs.platform = SecretStorePlatform::Apple;
#elif defined(Q_OS_WIN)
    inputs.platform = SecretStorePlatform::Windows;
#elif defined(Q_OS_LINUX)
    inputs.platform = SecretStorePlatform::Linux;
#else
    inputs.platform = SecretStorePlatform::Unknown;
#endif
#ifdef CHADVIS_HAS_KEYCHAIN
    inputs.compiledAppleKeychain = true;
#endif
#ifdef CHADVIS_HAS_WIN32_CREDENTIALS
    inputs.compiledWindowsCredentialManager = true;
#endif
#ifdef CHADVIS_HAS_SECRET_SERVICE
    inputs.compiledSecretService = true;
#endif
#ifdef CHADVIS_NO_KEYCHAIN
    inputs.keychainDisabledByBuild = true;
#endif
    return inputs;
}

/// Fulfils ISecretBackend by refusing, so that an unavailable store is an
/// ordinary failed Result rather than a null dereference or - far worse - a
/// silent drop into the plaintext file. The reason is carried so the user sees
/// WHICH condition applies; every message is written to be actionable and none
/// contains a secret.
class RefusingBackend final : public CredentialStore::ISecretBackend {
public:
    explicit RefusingBackend(UnavailableReason reason) : reason_(reason) {}

    [[nodiscard]] bool isSecure() const noexcept override { return false; }

    Result<void> store(const QString&, const QString&) override {
        return Result<void>::err(message());
    }
    Result<QString> load(const QString&) override { return Result<QString>::err(message()); }
    Result<void> remove(const QString&) override { return Result<void>::err(message()); }

private:
    [[nodiscard]] std::string message() const { return describeUnavailable(reason_).toStdString(); }
    UnavailableReason reason_;
};

std::unique_ptr<CredentialStore::ISecretBackend> makeDefaultBackend(const QString& fileRoot) {
    SecretStoreInputs inputs = probeBuildInputs();

    // The Linux probe is the one input that needs real I/O, so it is the one
    // thing done outside the pure policy. It is asked only when the build could
    // actually use the answer.
    if (inputs.platform == SecretStorePlatform::Linux && inputs.compiledSecretService &&
        !inputs.keychainDisabledByBuild) {
        inputs.secretService = detail::probeSecretService();
    }

    const BackendDecision decision = decideBackend(inputs);
    switch (decision.choice) {
        case BackendChoice::PlaintextFile:
            LOG_WARN("CredentialStore: CHADVIS_NO_KEYCHAIN is set; storing secrets in a plaintext "
                     "0600 file. The stored cookie carries a one-year refresh token");
            return std::make_unique<FileBackend>(fileRoot);

        case BackendChoice::AppleKeychain:
#ifdef CHADVIS_HAS_KEYCHAIN
            return std::make_unique<KeychainBackend>();
#else
            return std::make_unique<RefusingBackend>(UnavailableReason::BackendNotCompiled);
#endif

        case BackendChoice::WindowsCredentialManager:
            if (auto backend = detail::makeWindowsCredentialBackend()) return backend;
            return std::make_unique<RefusingBackend>(UnavailableReason::BackendNotCompiled);

        case BackendChoice::LinuxSecretService:
            if (auto backend = detail::makeSecretServiceBackend()) return backend;
            return std::make_unique<RefusingBackend>(UnavailableReason::ServiceNotRunning);

        case BackendChoice::Unavailable:
            return std::make_unique<RefusingBackend>(decision.reason);
    }
    return std::make_unique<RefusingBackend>(decision.reason);
}

} // namespace

namespace detail {

/// Live availability of the freedesktop Secret Service, in the pure struct the
/// policy consumes. Defined here (not in the Linux backend TU) so that
/// CredentialStore.cpp stays free of QtDBus while still being the only place
/// that assembles SecretStoreInputs - and so that the D-Bus probe has exactly
/// one caller to keep honest.
SecretServiceProbe probeSecretService() {
#ifdef CHADVIS_HAS_SECRET_SERVICE
    return probeSecretServiceOverDBus();
#else
    // Compiled out: report "no bus" rather than a fabricated success, so the
    // resulting message names the real problem (this build cannot use the
    // Secret Service) instead of implying the user's keyring is missing.
    return SecretServiceProbe{};
#endif
}

} // namespace detail

// ---------------------------------------------------------------------------
// CredentialStore facade
// ---------------------------------------------------------------------------

CredentialStore::CredentialStore(Backend backend, QString fileRoot) {
    switch (backend) {
        case Backend::File:
            // The one loud, explicit route to plaintext. Never chosen by policy.
            backend_ = std::make_unique<FileBackend>(std::move(fileRoot));
            return;

        case Backend::Keychain:
        case Backend::Default:
            break;
    }
    backend_ = makeDefaultBackend(fileRoot);
}

CredentialStore::~CredentialStore() = default;

CredentialStore::KeychainReadStatus CredentialStore::classifyKeychainReadStatus(int osStatus) {
    if (osStatus == 0) return KeychainReadStatus::Success;
    if (osStatus == kErrSecItemNotFound) return KeychainReadStatus::NotFound;
    if (osStatus == kErrSecInteractionNotAllowed || osStatus == kErrSecAuthFailed) {
        return KeychainReadStatus::AccessDenied;
    }
    return KeychainReadStatus::Failed;
}

Result<void> CredentialStore::store(const QString& key, const QString& secret) {
    if (key.isEmpty()) return Result<void>::err(std::string("CredentialStore: empty key"));
    return backend_->store(namespacedKey(key), secret);
}

Result<QString> CredentialStore::load(const QString& key) {
    if (key.isEmpty()) return Result<QString>::err(std::string("CredentialStore: empty key"));
    return backend_->load(namespacedKey(key));
}

Result<void> CredentialStore::remove(const QString& key) {
    if (key.isEmpty()) return Result<void>::err(std::string("CredentialStore: empty key"));
    return backend_->remove(namespacedKey(key));
}

bool CredentialStore::isSecureBackend() const {
    // The backend answers for itself. A dynamic_cast against a platform type
    // would need a #ifdef per platform and would silently report false on any
    // backend added later; the virtual makes forgetting to implement it a
    // compile error instead.
    return backend_->isSecure();
}

QString CredentialStore::redact(const QString& secret) {
    return QStringLiteral("****(len %1)").arg(secret.size());
}

} // namespace vc::suno::auth
