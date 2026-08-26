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
namespace {

/// Namespace applied to every user-facing key before it reaches a backend.
constexpr auto kKeyNamespace = QUtf8StringView("chadvis");
/// Keychain service name / namespace prefix (macOS generic-password items).
constexpr auto kServiceName = QUtf8StringView("chadvis-projectm-qt");

/// Full internal key: "<service>/<namespace>/<user-key>", e.g.
/// "chadvis-projectm-qt/chadvis/suno/default".
QString namespacedKey(const QString& key) {
    if (!key.startsWith(QLatin1String("chadvis/"))) {
        return QStringLiteral("%1/%2/%3")
                .arg(kServiceName.toString(), kKeyNamespace.toString(), key);
    }
    return QStringLiteral("%1/%2").arg(kServiceName.toString(), key);
}

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
// ---------------------------------------------------------------------------

class FileBackend final : public CredentialStore::ISecretBackend {
public:
    explicit FileBackend(QString fileRoot) : root_(std::move(fileRoot)) {}

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
            LOG_WARN("CredentialStore: OS keychain unavailable/disabled; falling back to "
                     "plaintext secret files with 0600 permissions (reduced security)");
        }
    }

    Result<QString> pathFor(const QString& nsKey) {
        auto dir = rootDir();
        if (!dir) return dir;
        if (!QFileInfo::exists(*dir) && !QDir().mkpath(*dir)) {
            return Result<QString>::err(
                    QStringLiteral("Cannot create secret storage directory: %1").arg(*dir).toStdString());
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
    Result<void> store(const QString& nsKey, const QString& secret) override {
        CfGuard account(makeCfString(namespacedKey(nsKey)));
        CfGuard service(makeCfString(kServiceName.toString()));
        if (!account || !service)
            return Result<void>::err(std::string("Failed to allocate keychain strings"));

        const QByteArray bytes = secret.toUtf8();
        CfGuard data(CFDataCreate(kCFAllocatorDefault,
                                  reinterpret_cast<const UInt8*>(bytes.constData()),
                                  static_cast<CFIndex>(bytes.size())));
        if (!data) return Result<void>::err(std::string("Failed to allocate keychain data buffer"));

        // Upsert: try an update first; add a fresh item when none exists yet.
        const void* updateKeys[] = {kSecValueData};
        const void* updateVals[] = {data.get()};
        CFDictionaryRef updateDict = cfDict(updateKeys, updateVals, 1);

        const void* searchKeys[] = {kSecClass, kSecAttrService, kSecAttrAccount};
        const void* searchVals[] = {kSecClassGenericPassword, service.get(), account.get()};
        CFDictionaryRef searchDict = cfDict(searchKeys, searchVals, 3);

        OSStatus status =
                SecItemUpdate(searchDict, updateDict);
        if (status == errSecItemNotFound) {
            const void* addKeys[] = {kSecClass, kSecAttrService, kSecAttrAccount, kSecValueData};
            const void* addVals[] = {kSecClassGenericPassword, service.get(), account.get(),
                                     data.get()};
            CFDictionaryRef addDict = cfDict(addKeys, addVals, 4);
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

        const void* keys[] = {kSecClass,       kSecAttrService,   kSecAttrAccount,
                              kSecReturnData,  kSecMatchLimit};
        const void* vals[] = {kSecClassGenericPassword, service.get(), account.get(),
                              kCFBooleanTrue,           kSecMatchLimitOne};
        CFDictionaryRef query = cfDict(keys, vals, 5);

        CFTypeRef found = nullptr;
        const OSStatus status = SecItemCopyMatching(query, &found);
        CFRelease(query);
        if (status == errSecItemNotFound) {
            return Result<QString>::err(
                    QStringLiteral("No secret stored under '%1'").arg(nsKey).toStdString());
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

        const void* keys[] = {kSecClass, kSecAttrService, kSecAttrAccount};
        const void* vals[] = {kSecClassGenericPassword, service.get(), account.get()};
        CFDictionaryRef query = cfDict(keys, vals, 3);
        const OSStatus status = SecItemDelete(query);
        CFRelease(query);

        if (status != errSecSuccess && status != errSecItemNotFound) {
            return Result<void>::err(keychainFailure("remove", status));
        }
        return Result<void>::ok();
    }

private:
    static CFStringRef makeCfString(const QString& s) {
        return s.toCFString();
    }

    static CFDictionaryRef cfDict(const void** keys, const void** vals, CFIndex count) {
        return CFDictionaryCreate(kCFAllocatorDefault, keys, vals, count,
                                  &kCFTypeDictionaryKeyCallBacks,
                                  &kCFTypeDictionaryValueCallBacks);
    }

    static std::string keychainFailure(const char* op, OSStatus status) {
        return QStringLiteral("CredentialStore keychain %1 failed (OSStatus %2)")
                .arg(QLatin1String(op))
                .arg(static_cast<int>(status))
                .toStdString();
    }
};

#endif // CHADVIS_HAS_KEYCHAIN

std::unique_ptr<CredentialStore::ISecretBackend> makeDefaultBackend(const QString& fileRoot) {
#ifdef CHADVIS_HAS_KEYCHAIN
    return std::make_unique<KeychainBackend>();
#else
    return std::make_unique<FileBackend>(fileRoot);
#endif
}

} // namespace

// ---------------------------------------------------------------------------
// CredentialStore facade
// ---------------------------------------------------------------------------

CredentialStore::CredentialStore(Backend backend, QString fileRoot)
    : backend_([backend, &fileRoot]() -> std::unique_ptr<ISecretBackend> {
          switch (backend) {
          case Backend::File:
              return std::make_unique<FileBackend>(std::move(fileRoot));
          case Backend::Keychain:
#ifdef CHADVIS_HAS_KEYCHAIN
              return std::make_unique<KeychainBackend>();
#else
              LOG_WARN("CredentialStore: keychain backend requested but this build has no "
                       "keychain support; using the file fallback");
              return std::make_unique<FileBackend>(std::move(fileRoot));
#endif
          case Backend::Default:
              break;
          }
          return makeDefaultBackend(fileRoot);
      }()) {}

CredentialStore::~CredentialStore() = default;

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

QString CredentialStore::redact(const QString& secret) {
    return QStringLiteral("****(len %1)").arg(secret.size());
}

} // namespace vc::suno::auth
