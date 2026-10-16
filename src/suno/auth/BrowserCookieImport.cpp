/**
 * @file BrowserCookieImport.cpp
 * @brief macOS implementation: Chromium-family cookie stores.
 *
 * The browser table and the crypto parameters are measured, not invented — see
 * `suno-recon/bin/freshtok.py`, which reads this machine's Brave store today,
 * and the captured `Set-Cookie` headers in
 * `suno-recon-cat-stealth-modekl/suno-oauth-google.md`.
 *
 * @version 1.0.0
 */

#include "BrowserCookieImport.hpp"

#include "core/Logger.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QList>
#include <QPair>
#include <QProcess>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryFile>
#include <QVariant>

#ifdef Q_OS_MACOS
#include <CommonCrypto/CommonCrypto.h>
#endif

#include <algorithm>

namespace vc::suno::auth {
namespace {

/// One Chromium-family browser. The Keychain service name differs per browser
/// ("Brave Safe Storage", "Chrome Safe Storage", ...) and is the only thing
/// that does — the profile layout, the PBKDF2 parameters and the v10 cookie
/// format are identical across them.
struct Browser {
    const char* name;
    const char* applicationSupportDir;
    const char* keychainService;
};

/// Preference order = the order a user's sign-in most likely landed in. Brave
/// is the default browser on this machine (verified via LaunchServices), so it
/// is first; the rest are ordered by install base.
constexpr Browser kBrowsers[] = {
    {"Brave", "BraveSoftware/Brave-Browser", "Brave Safe Storage"},
    {"Chrome", "Google/Chrome", "Chrome Safe Storage"},
    {"Chromium", "Chromium", "Chromium Safe Storage"},
    {"Edge", "Microsoft Edge", "Microsoft Edge Safe Storage"},
    {"Vivaldi", "Vivaldi", "Vivaldi Safe Storage"},
};

/// Cookie names we take. `__client` prefixes `__client_uat` too, which is the
/// one non-secret Clerk cookie — the capture shows it is the plain timestamp
/// `1787705821`. `SunoClient`'s classifier accepts exactly these families.
constexpr const char* kAcceptedPrefixes[] = {"__client", "__session"};

/// The v10 layout: 3 header bytes, 16 IV bytes, then AES-128-CBC ciphertext.
constexpr int kV10HeaderBytes = 3;
constexpr int kIvBytes = 16;
constexpr int kAesKeyBytes = 16;
constexpr int kPbkdf2Iterations = 1003;

/// A byte that can legally appear in a cookie value this module will hand on:
/// printable ASCII, plus the horizontal tab/newline family for safety. Notably
/// EXCLUDES high-bit bytes (>= 0x7f).
///
/// The exclusion is load-bearing, not hygiene: the 16-byte host-binding
/// prefix on this machine's `.suno.com` cookies begins with 0xdd, which a
/// control-character-only check cheerfully classifies as printable -- so the
/// prefix went unstripped and the whole cookie was rejected. A detector and
/// a final check that disagree about what "printable" means was the actual
/// bug; this is the one place that definition now lives.
bool isCookieByte(char c)
{
    const unsigned char u = static_cast<unsigned char>(c);
    return u == 0x09 || (u >= 0x0a && u <= 0x0d) || (u >= 0x20 && u < 0x7f);
}

bool nameIsAccepted(const QString& name)
{
    return std::any_of(std::begin(kAcceptedPrefixes), std::end(kAcceptedPrefixes),
                       [&name](const char* prefix) {
                           return name.startsWith(QLatin1String(prefix));
                       });
}

/// The Keychain secret for a browser's cookie store. Read through the
/// `security` CLI rather than the Security framework: the framework needs a
/// code-signed entitlement and prompts identically, while the CLI is one line
/// and is the same thing the recon's freshtok.py does.
///
/// The first call per browser prompts the user ("... wants to access your
/// confidential information stored in <browser> Safe Storage"). That is the
/// consent moment, and it is why harvest() is only ever called from an
/// explicit button.
QByteArray keychainSecret(const QString& service)
{
    QProcess security;
    security.start(QStringLiteral("security"),
                   {QStringLiteral("find-generic-password"),
                    QStringLiteral("-w"), QStringLiteral("-s"), service});
    // 30 s, not 10 s: the read is the first per-binary consent prompt, and
    // the first shipped version killed `security` after ten seconds — a
    // user who saw the prompt, took a breath, and clicked Allow instead got
    // "no Suno session was found", which is the failure the signed-in
    // report describes. Killing the CLI mid-prompt is the one way a clicked
    // Allow still ends in nothing.
    if (!security.waitForFinished(30000)) {
        LOG_WARN("BrowserCookieImport: keychain read for a browser timed out "
                 "(no value read)");
        return {};
    }
    if (security.exitCode() != 0) {
        // The common cases: the keychain item does not exist, or the user
        // declined the prompt. Service name only — never a secret.
        LOG_INFO("BrowserCookieImport: keychain item not available (service "
                 "present, user may have declined)");
        return {};
    }
    return security.readAllStandardOutput().trimmed();
}

/// Copy the SQLite store to a temp file before opening it. A live Chromium
/// profile keeps the DB in WAL mode, and SQLite must not read a WAL-backed
/// database in place while another process is writing it.
QString copyToTemporary(const QString& source)
{
    QTemporaryFile temp;
    temp.setAutoRemove(false);
    if (!temp.open()) {
        return {};
    }
    const QString path = temp.fileName();
    // QTemporaryFile has already CREATED the file by the time it has a name,
    // and QFile::copy refuses to overwrite an existing destination. Without
    // this remove, the copy fails, the caller returns nothing, and the
    // harvest reads as "no Suno session was found" on a machine that has one
    // -- which is exactly how the synthetic-store test caught it.
    temp.close();
    QFile::remove(path);
    if (!QFile::copy(source, path)) {
        QFile::remove(path);
        return {};
    }
    return path;
}

} // namespace

bool BrowserCookieImport::headerCarriesUsableCredential(const QString& header)
{
    // Only the name matters, never a value. Split on the separators
    // assembleCookieHeader emits and inspect what precedes the first '='.
    for (const QString& part : header.split(QStringLiteral("; "), Qt::SkipEmptyParts)) {
        const QString name = part.section(QLatin1Char('='), 0, 0);
        if (name.startsWith(QLatin1String("__session"))) {
            return true;
        }
        if (name.startsWith(QLatin1String("__client")) &&
            !name.startsWith(QLatin1String("__client_uat"))) {
            return true;
        }
    }
    return false;
}

QString BrowserCookieImport::reasonForStore(const StoreObservation& seen,
                                            const QString& browserName)
{
    if (seen.keyMissing) {
        // The two shapes of "macOS did not let us read the secret": the user
        // declined the prompt, or the prompt outlived the wait. The first
        // shipped version gave both the generic "no Suno session was found"
        // message to a user who WAS signed in — the exact "did not work"
        // report this file now diagnoses.
        return QStringLiteral(
                   "%1 did not hand over its cookie-store key. Press Import "
                   "again and click Allow if macOS asks for permission.")
            .arg(browserName);
    }
    if (seen.sawSignedOut) {
        // The prepared signed-out store: `__client_uat` rows present, every
        // value "0", and no `__client` at all. The old message said "no Suno
        // session was found" — true but blaming, when the store itself is
        // telling us exactly what is wrong.
        return QStringLiteral(
                   "%1 says you are signed out of suno.com. Sign in there "
                   "first, then press Import once more.").arg(browserName);
    }
    if (seen.clerkRows > 0 && seen.decrypted == 0) {
        // Rows exist, none decrypt. A wrong key decrypts to noise the
        // printable check rejects; this is the closest thing to a real
        // signal the store gives us.
        return QStringLiteral(
                   "Suno's session cookies were found in %1 but could not be "
                   "decoded. Press Import again; if it keeps failing, this "
                   "build's format support needs updating.").arg(browserName);
    }
    if (seen.clerkRows > 0 && !seen.hadCredential) {
        // Rows decoded, but the durable credential is absent — a half
        // signed-out store. Same remedy as signed out, worded for it.
        return QStringLiteral(
                   "The Suno cookies in %1 carry no usable session. Sign in "
                   "at suno.com there, then press Import once more.")
            .arg(browserName);
    }
    return QStringLiteral(
               "No Suno cookies were found in %1. Visit suno.com there once, "
               "then press Import again.").arg(browserName);
}

QStringList BrowserCookieImport::candidateBrowserNames()
{
    QStringList names;
    for (const auto& browser : kBrowsers) {
        const QString profileRoot = QDir::home().filePath(
            QStringLiteral("Library/Application Support/") +
            QLatin1String(browser.applicationSupportDir));
        if (QFileInfo::exists(profileRoot)) {
            names.push_back(QLatin1String(browser.name));
        }
    }
    return names;
}

QByteArray BrowserCookieImport::deriveChromiumKey(const QByteArray& keychainSecret)
{
    if (keychainSecret.isEmpty()) {
        return {};
    }
#ifdef Q_OS_MACOS
    QByteArray key(kAesKeyBytes, Qt::Uninitialized);
    // PBKDF2-HMAC-SHA1, "saltysalt", 1003 iterations. These are Chromium's
    // constants on macOS and are the reason the recon's freshtok.py works on
    // this machine; changing any of them yields a key that decrypts to noise.
    const int status = CCKeyDerivationPBKDF(
        kCCPBKDF2,
        keychainSecret.constData(), static_cast<size_t>(keychainSecret.size()),
        reinterpret_cast<const unsigned char*>("saltysalt"), 10,
        kCCPRFHmacAlgSHA1, kPbkdf2Iterations,
        reinterpret_cast<unsigned char*>(key.data()), kAesKeyBytes);
    if (status != kCCSuccess) {
        return {};
    }
    return key;
#else
    Q_UNUSED(keychainSecret);
    return {};
#endif
}

QByteArray BrowserCookieImport::decryptChromiumValue(const QByteArray& encrypted,
                                                    const QByteArray& key)
{
    if (!encrypted.startsWith("v10") || key.size() != kAesKeyBytes) {
        return {};
    }
    if (encrypted.size() <= kV10HeaderBytes + kIvBytes) {
        return {};
    }
#ifdef Q_OS_MACOS
    const char* iv = encrypted.constData() + kV10HeaderBytes;
    const char* cipher = encrypted.constData() + kV10HeaderBytes + kIvBytes;
    const int cipherLen = encrypted.size() - kV10HeaderBytes - kIvBytes;
    QByteArray plain(cipherLen + kAesKeyBytes, Qt::Uninitialized);

    size_t moved = 0;
    // options = 0, deliberately: NO padding semantics in the crypto call.
    //
    // The store is inconsistent about its tail. Some rows end in a valid PKCS7
    // run; others, on the same machine and the same day, do not — and
    // kCCOptionPKCS7Padding then either strips or silently does not, moving
    // `moved` by 15 bytes between two copies of the same cookie as Brave
    // re-encrypts it live. Depending on that is what made the first version
    // reject a valid `__client` and return a `__client_uat` three times too
    // long. The crypto is plain CBC; the tail is handled explicitly below,
    // where it can be seen.
    const int status = CCCrypt(kCCDecrypt, kCCAlgorithmAES128, 0,
                               key.constData(), kAesKeyBytes, iv,
                               cipher, cipherLen,
                               plain.data(), plain.size(), &moved);
    if (status != kCCSuccess) {
        return {};
    }
    plain.truncate(static_cast<int>(moved));

    // Chromium (2023+) prepends a 16-byte binary host-binding prefix to the
    // plaintext before encrypting. It is not a digest we can reproduce — a
    // plain SHA-256 of the host does not match it — and it is not the IV; the
    // IV really is v[3:19], and the value really does begin at offset 16,
    // verified across every Clerk cookie in this machine's store. The prefix's
    // own bytes vary per encryption, so its first bytes are not reliably
    // non-printable either — the detector must scan the whole block.
    if (plain.size() > kIvBytes) {
        bool binary = false;
        for (const char c : plain.left(kIvBytes)) {
            if (!isCookieByte(c)) {
                binary = true;
                break;
            }
        }
        if (binary) {
            plain.remove(0, kIvBytes);
        }
    }

    // The tail, explicitly, in the order the formats actually occur:
    // NULs first (older rows), then one PKCS7 run if the last byte claims one
    // and the bytes behind it agree. Anything else is left alone and the
    // printable check below is what rejects a wrong-key decrypt.
    while (!plain.isEmpty() && plain.back() == '\0') {
        plain.chop(1);
    }
    if (!plain.isEmpty()) {
        const unsigned char last = static_cast<unsigned char>(plain.back());
        if (last >= 1 && last <= kAesKeyBytes && plain.size() >= last) {
            bool run = true;
            for (int i = plain.size() - last; i < plain.size(); ++i) {
                if (static_cast<unsigned char>(plain.at(i)) != last) {
                    run = false;
                    break;
                }
            }
            if (run) {
                plain.chop(last);
            }
        }
    }

    if (plain.isEmpty()) {
        return {};
    }
    for (const char c : plain) {
        if (!isCookieByte(c)) {
            return {};
        }
    }
    return plain;
#else
    Q_UNUSED(key);
    return {};
#endif
}

QString BrowserCookieImport::assembleCookieHeader(
    const QList<QPair<QString, QString>>& pairs)
{
    QList<QPair<QString, QString>> ordered = pairs;
    std::sort(ordered.begin(), ordered.end(),
              [](const QPair<QString, QString>& a, const QPair<QString, QString>& b) {
                  // __client* before __session*; within a family, the unsuffixed
                  // name first, then shorter names before longer, which puts
                  // `__client` ahead of `__client_Jnxw-muT` and
                  // `__client_uat` ahead of `__client_uat_Jnxw-muT`. Fixed
                  // order so the stored header is reproducible.
                  if (a.first != b.first) {
                      if (a.first.startsWith(QLatin1String("__client")) ==
                          b.first.startsWith(QLatin1String("__client"))) {
                          return a.first < b.first;
                      }
                      return a.first.startsWith(QLatin1String("__client"));
                  }
                  return false;
              });

    QStringList parts;
    for (const auto& [name, value] : ordered) {
        if (name.isEmpty() || value.isEmpty()) {
            continue;
        }
        parts.push_back(name + QLatin1Char('=') + value);
    }
    return parts.join(QStringLiteral("; "));
}

QList<QPair<QString, QString>> BrowserCookieImport::readStore(const QString& databasePath,
                                                             const QByteArray& key)
{
    return readStore(databasePath, key, nullptr);
}

QList<QPair<QString, QString>> BrowserCookieImport::readStore(const QString& databasePath,
                                                              const QByteArray& key,
                                                              StoreObservation* observation)
{
    StoreObservation local;
    StoreObservation& seen = observation ? *observation : local;
    seen = {};
    seen.keyMissing = key.isEmpty() || key.size() != kAesKeyBytes;
    QList<QPair<QString, QString>> found;
    if (seen.keyMissing) {
        return found;
    }

    const QString copy = copyToTemporary(databasePath);
    if (copy.isEmpty()) {
        return found;
    }

    // A scoped connection name so repeated calls (multi-profile, multi-browser)
    // cannot leak registrations — the exact defect SunoDatabase's T0-class fix
    // described. Removed unconditionally at the end.
    static int s_connectionSerial = 0;
    const QString connectionName =
        QStringLiteral("browser_cookies_%1").arg(++s_connectionSerial);
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                   connectionName);
        db.setDatabaseName(copy);
        if (!db.open()) {
            LOG_INFO("BrowserCookieImport: a cookie store could not be opened");
        } else {
            QSqlQuery query(db);
            // host-restricted to suno.com in the SQL itself, so the query
            // never materialises the rest of the jar. `_gcl_au`, `__stripe_*`
            // and friends sit on the same rows and stay unread.
            query.prepare(QStringLiteral(
                "SELECT host_key, name, encrypted_value FROM cookies "
                "WHERE (host_key = 'suno.com' OR host_key LIKE '%.suno.com' "
                "       OR host_key LIKE '%suno.com')"));
            if (query.exec()) {
                while (query.next()) {
                    const QString name = query.value(1).toString();
                    if (!nameIsAccepted(name)) {
                        continue;
                    }
                    ++seen.clerkRows;
                    const QByteArray encrypted = query.value(2).toByteArray();
                    const QByteArray plain = decryptChromiumValue(encrypted, key);
                    if (plain.isEmpty()) {
                        continue;
                    }
                    const QString value = QString::fromUtf8(plain);
                    ++seen.decrypted;
                    if (name.startsWith(QLatin1String("__session"))) {
                        seen.hadCredential = true;
                    } else if (name.startsWith(QLatin1String("__client")) &&
                               !name.startsWith(QLatin1String("__client_uat"))) {
                        seen.hadCredential = true;
                    } else if (name.startsWith(QLatin1String("__client_uat"))) {
                        if (value != QLatin1String("0")) {
                            // A signed-in timestamp with no `__client` is a
                            // half signed-out store: not a session (the
                            // durability bar above still gatekeeps), but
                            // also not evidence the user is signed out.
                        } else {
                            seen.sawSignedOut = true;
                        }
                    }
                    found.push_back(qMakePair(name, value));
                }
            } else {
                LOG_INFO("BrowserCookieImport: cookie query refused");
            }
        }
    }
    QSqlDatabase::removeDatabase(connectionName);
    QFile::remove(copy);
    return found;
}

BrowserCookieHarvest BrowserCookieImport::harvest()
{
    BrowserCookieHarvest result;
    QString firstReason;
    int firstReasonRank = -1;
    for (const auto& browser : kBrowsers) {
        const QString name = QLatin1String(browser.name);
        const QString profileRoot = QDir::home().filePath(
            QStringLiteral("Library/Application Support/") +
            QLatin1String(browser.applicationSupportDir));
        if (!QFileInfo::exists(profileRoot)) {
            continue;
        }
        result.browsersSearched.push_back(name);

        // "Default" then "Profile 1", "Profile 2" ... in numeric order, so the
        // user's main profile is read first and the order is stable.
        QStringList profiles{QStringLiteral("Default")};
        QDir root(profileRoot);
        QStringList numbered =
            root.entryList(QStringList{QStringLiteral("Profile *")},
                           QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        std::sort(numbered.begin(), numbered.end(),
                  [](const QString& a, const QString& b) {
                      const int na = a.right(a.size() - 8).toInt();
                      const int nb = b.right(b.size() - 8).toInt();
                      return na < nb;
                  });
        profiles.append(numbered);

        for (const QString& profile : profiles) {
            const QString store = profileRoot + QLatin1Char('/') + profile +
                                 QStringLiteral("/Cookies");
            if (!QFileInfo::exists(store)) {
                continue;
            }
            // The Keychain read happens here, once per browser, on an explicit
            // user action. readStore() takes the derived key rather than
            // reading it so tests never trigger the consent prompt.
            const QString service = QLatin1String(browser.keychainService);
            StoreObservation seen;
            const QByteArray key = deriveChromiumKey(keychainSecret(service));
            const auto pairs = readStore(store, key, &seen);

            // The durability bar: a header without a `__client` (non-`uat`)
            // or `__session` member carries no credential. The first shipped
            // version treated ANY decodable Clerk cookie as success, so a
            // signed-out store yielded `__client_uat=0`, was silently stored
            // through setCookie(), and made the whole button read as broken.
            const QString header = assembleCookieHeader(pairs);
            if (!header.isEmpty() && headerCarriesUsableCredential(header)) {
                result.cookieHeader = header;
                result.browserName = name;
                // One durable credential is the whole goal. The first store
                // that yields it wins; later browsers are not read.
                LOG_INFO("BrowserCookieImport: session imported from a browser "
                         "(name and profile count only)");
                return result;
            }

            // No credential here. Keep this store's diagnosis only if it is
            // more informative than anything recorded so far. A store with
            // no suno.com rows at all (the browser never visited) is the
            // least informative; a signed-out store is the most — its message
            // is the one the user can act on directly.
            const int rank = [&seen] {
                if (seen.sawSignedOut) return 5;
                if (seen.keyMissing) return 4;
                if (seen.clerkRows > 0 && seen.decrypted == 0) return 3;
                if (seen.clerkRows > 0) return 2; // rows, no usable credential
                return 1;                          // nothing Clerk-named
            }();
            if (rank > firstReasonRank) {
                firstReasonRank = rank;
                firstReason = reasonForStore(seen, name);
            }
        }
    }

    if (result.browsersSearched.isEmpty()) {
        result.reason = QStringLiteral("No installed browser was found to import from");
    } else {
        result.reason = firstReason;
    }
    return result;
}

} // namespace vc::suno::auth