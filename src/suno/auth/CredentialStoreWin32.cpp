// CredentialStoreWin32.cpp - Win32 Credential Manager backend.
//
// Sibling of the macOS KeychainBackend in CredentialStore.cpp. Same interface,
// same upsert shape, same "a missing key is not an error on remove" rule; the
// differences are all forced by the platform API and are called out below.
//
// Compiled only when CHADVIS_HAS_WIN32_CREDENTIALS is defined, which the
// build owner sets for WIN32. The whole file is inert otherwise, so this
// translation unit can be listed unconditionally in cmake/Sources.cmake
// without a Windows toolchain seeing it - or a Linux one.
//
// NOT COMPILED OR EXECUTED on the machine that wrote it (macOS, no mingw-w64
// cross-compiler available). See the report: the arithmetic and the API shapes
// are asserted by the pure policy in CredentialStorePolicy.cpp; the syscall
// path is reviewed, not measured.

#include "CredentialStore.hpp"

#include "core/Logger.hpp"

#include <QString>

#ifdef CHADVIS_HAS_WIN32_CREDENTIALS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
// wincred.h after windows.h: it depends on the types above.
#include <wincred.h>
#endif

namespace vc::suno::auth {

#ifdef CHADVIS_HAS_WIN32_CREDENTIALS

namespace {

// The SDK's own limits, checked against ours. kMaxSecretBlobBytes is what the
// policy enforces; if a future SDK disagrees, this is a compile error rather
// than a runtime surprise on a user's machine.
static_assert(CRED_MAX_CREDENTIAL_BLOB_SIZE == kMaxSecretBlobBytes,
              "the enforced blob ceiling must match the SDK's CRED_MAX_CREDENTIAL_BLOB_SIZE");
static_assert(CRED_MAX_USERNAME_LENGTH == 513, "unexpected CRED_MAX_USERNAME_LENGTH");

/// Win32 wide strings are UTF-16 (wchar_t is 16-bit on Windows). Qt's QString
/// is UTF-16 too, but `toStdWString()` is the wrong tool for two measured
/// reasons, so the conversion goes through utf16() explicitly:
///
///  1. std::wstring's element size is sizeof(wchar_t), which is 16-bit on
///     Windows but **4 bytes on macOS and Linux** (measured on the authoring
///     host). A cross-platform helper that assumes 16-bit wchar_t compiles on
///     the wrong platform and produces a buffer CredWriteW rejects.
///  2. MultiByteToWideChar with the default ANSI code page silently mangles any
///     byte >= 0x80 against the *user's* locale. CP_UTF8 avoids it, but then
///     the round trip needs two calls (size query, then convert) and an invalid
///     sequence is a silent substitution rather than an error.
///
/// QString::utf16() sidesteps all of it: no code page, no wchar_t width
/// assumption, and the result is already the exact UTF-16 that CredWriteW wants,
/// NUL-terminated by Qt.
std::wstring toWide(const QString& s) {
    // utf16() returns const ushort*; wchar_t is 16-bit on Windows, so this cast
    // is layout-correct there. Note it is NOT correct on a host where wchar_t is
    // 4 bytes (macOS/Linux, measured sizeof(wchar_t)==4), which is exactly why
    // this helper lives inside a _WIN32-only file rather than in shared code.
    const ushort* utf16 = s.utf16();
    // Length in code units, excluding the terminator Qt guarantees.
    return std::wstring(reinterpret_cast<const wchar_t*>(utf16),
                        static_cast<std::size_t>(s.size()));
}

/// Release a PCREDENTIALW returned by CredReadW.
struct CredFreer {
    void operator()(CREDENTIALW* cred) const noexcept {
        if (cred) CredFree(cred);
    }
};
using CredGuard = std::unique_ptr<CREDENTIALW, CredFreer>;

/// Zero-initialized CREDENTIALW with our type set. Zeroing is mandatory:
/// CredWriteW reads every field, and an uninitialized Flags or Persist is an
/// undefined value the API will happily act on.
CREDENTIALW makeCredential() {
    CREDENTIALW cred{};
    cred.Type = CRED_TYPE_GENERIC;
    // CRED_PERSIST_LOCAL_MACHINE, deliberately.
    //
    //  CRED_PERSIST_SESSION (1)     dies at logoff. A music player that
    //                                forgets the credential on every reboot is
    //                                not "remembered", it is broken.
    //  CRED_PERSIST_ENTERPRISE (3) roams the secret to every machine the domain
    //                                profile reaches, and it silently *degrades*
    //                                to per-machine when the user is not domain
    //                                joined - so a consumer desktop gets
    //                                domain semantics by accident, and a domain
    //                                desktop gets a one-year refresh token
    //                                copied onto machines the user may not
    //                                control. That is the wrong default for
    //                                this secret.
    //  CRED_PERSIST_LOCAL_MACHINE (2) survives reboot, stays on this machine,
    //                                and stays under this user's profile. It is
    //                                encrypted with DPAPI against the user's
    //                                own key, so another local user cannot read
    //                                it either. That is the correct trade.
    cred.Persist = CRED_PERSIST_LOCAL_MACHINE;
    return cred;
}

Error winCredFailure(const char* op, DWORD code) {
    return Error(QStringLiteral("CredentialStore Win32 %1 failed (GetLastError %2)")
                         .arg(QLatin1String(op))
                         .arg(static_cast<uint>(code))
                         .toStdString(),
                 static_cast<int>(code));
}

} // namespace

/// Generic-credential backend over CredWriteW / CredReadW / CredDeleteW.
class Win32CredentialBackend final : public CredentialStore::ISecretBackend {
public:
    [[nodiscard]] bool isSecure() const noexcept override { return true; }

    Result<void> store(const QString& nsKey, const QString& secret) override {
        // Plan first: the target name, the exact blob and its length are all
        // decided - and the secret refused if it is corrupt or oversized -
        // before a single API call. planWindowsCredential is pure, so this is
        // the same code path the unit tests assert on.
        const auto planned = planWindowsCredential(nsKey, secret);
        if (!planned) {
            return Result<void>::err(
                    Error(describeEncodeDefect(planned.error(), secret).toStdString()));
        }

        CREDENTIALW cred = makeCredential();
        const std::wstring target = toWide(planned->targetName);
        const std::wstring user = toWide(planned->userName);
        // The blob is opaque bytes. CredentialBlobSize is the length; there is
        // no terminator to add and none should be, because a trailing NUL
        // would be stored and then read back as part of the secret.
        const QByteArray& blob = planned->blob;

        cred.TargetName = const_cast<LPWSTR>(target.c_str());
        cred.UserName = const_cast<LPWSTR>(user.c_str());
        cred.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char*>(blob.constData()));
        cred.CredentialBlobSize = static_cast<DWORD>(planned->blobSize);
        cred.Comment = const_cast<LPWSTR>(L"ChadVis secret storage. Delete with the app.");

        // Upsert in one call. CredWriteW overwrites an existing credential with
        // the same TargetName, so unlike the macOS backend's
        // update-then-add dance this needs no read and no ERROR_NOT_FOUND
        // special case: absent and present both write.
        if (!CredWriteW(&cred, 0)) {
            const DWORD code = GetLastError();
            return Result<void>::err(winCredFailure("store", code));
        }
        return Result<void>::ok();
    }

    Result<QString> load(const QString& nsKey) override {
        const QString targetName = namespacedKeyFor(nsKey);
        const std::wstring target = toWide(targetName);

        PCREDENTIALW raw = nullptr;
        // CRED_PERSIST_LOCAL_MACHINE (fLocalMachine): match the machine-scoped
        // entry that store() wrote. Passing CRED_PERSIST_ANY here would be a
        // silent widening - it would happily read a session- or
        // enterprise-scoped credential some other install created.
        if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, CRED_PERSIST_LOCAL_MACHINE, &raw)) {
            const DWORD code = GetLastError();
            switch (classifyCredReadStatus(code)) {
                case CredReadStatus::NotFound:
                    // The ONLY code that means "absent". Reported as absent, and
                    // the raw code is preserved for diagnostics.
                    return Result<QString>::err(Error(QStringLiteral("No secret stored under '%1'")
                                                              .arg(targetName)
                                                              .toStdString(),
                                                      static_cast<int>(code)));
                case CredReadStatus::AccessDenied:
                    LOG_WARN("CredentialStore: Windows Credential Manager denied a read of '{}' "
                             "(GetLastError {}); unlock the credential store or check the policy "
                             "that "
                             "restricts it (no plaintext fallback)",
                             targetName.toStdString(), static_cast<uint>(code));
                    return Result<QString>::err(winCredFailure("read", code));
                case CredReadStatus::Failed:
                case CredReadStatus::Success:
                    break;
            }
            return Result<QString>::err(winCredFailure("load", code));
        }
        CredGuard cred(raw);

        // CredentialBlobSize is authoritative. Do NOT strlen the blob and do
        // not assume a terminator: the blob is binary, may legitimately contain
        // NUL bytes, and its true length is the field we wrote.
        const auto* bytes = reinterpret_cast<const char*>(cred->CredentialBlob);
        const int size = static_cast<int>(cred->CredentialBlobSize);
        return Result<QString>::ok(QString::fromUtf8(bytes, size));
    }

    Result<void> remove(const QString& nsKey) override {
        const std::wstring target = toWide(namespacedKeyFor(nsKey));
        if (CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0)) {
            return Result<void>::ok();
        }
        const DWORD code = GetLastError();
        // Removing an absent key is not an error - same contract as the macOS
        // backend's errSecItemNotFound arm.
        if (classifyCredReadStatus(code) == CredReadStatus::NotFound) {
            return Result<void>::ok();
        }
        return Result<void>::err(winCredFailure("remove", code));
    }
};

#endif // CHADVIS_HAS_WIN32_CREDENTIALS

namespace detail {

std::unique_ptr<CredentialStore::ISecretBackend> makeWindowsCredentialBackend() {
#ifdef CHADVIS_HAS_WIN32_CREDENTIALS
    return std::make_unique<Win32CredentialBackend>();
#else
    // Compiled out on this platform. Returning nullptr (rather than defining a
    // stub) keeps a Windows credential manager from ever being constructed
    // where wincred.h does not exist.
    return nullptr;
#endif
}

// probeSecretServiceOverDBus() is deliberately NOT defined here. It belongs to
// CredentialStoreSecretService.cpp alone; defining it in both would be a
// duplicate symbol on any build that compiles both files.

} // namespace detail
} // namespace vc::suno::auth