/**
 * @file BrowserCookieImport.hpp
 * @brief Read the Clerk session cookies for suno.com / suno.ai out of a local browser's
 *        on-disk cookie store.
 *
 * T0199. "Sign in with Google" opens the system browser and then extracts
 * nothing, because Clerk's OAuth delivers the code to
 * `auth.suno.com/social/complete/` and never to a loopback address — a desktop
 * client has no callback to catch. For a user who is already signed in the
 * browser simply opens on a logged-in page and the app waits for a paste the
 * UI only mentions in a collapsed field.
 *
 * The credential exists, in the right place, the whole time. The recon answers
 * this end to end (`suno-recon-cat-stealth-modekl/suno-oauth-google.md`,
 * `suno-recon/reports/endpoints.md`):
 *
 *   - `POST /v1/client/sign_ins` sets `__client` — a refresh JWT, `HttpOnly`,
 *     `Secure`, `SameSite=None`, **Max-Age 31536000 (≈1 year)**, payload
 *     `"token_type":"refresh"`.
 *   - `POST /v1/client/sessions/{sid}/tokens` mints a fresh `__session`
 *     bearer from it. `SunoClient` drives that through
 *     `ClerkAuthClient::mintBearerViaTokens()`, falling back to `touch()`
 *     when tokens fails with a protocol or shape error.
 *   - `HttpOnly` stops `document.cookie` (which is why the T0182 bookmarklet
 *     was dropped) but does not protect the on-disk SQLite store.
 *
 * Chromium-family browsers on macOS encrypt that store with a key derived from
 * a per-browser Keychain secret ("Brave Safe Storage" &c), and the recon's own
 * `bin/freshtok.py` already reads it that way: PBKDF2-HMAC-SHA1, 1003
 * iterations, `saltysalt`, 16-byte key; cookie values are `v10` + 16-byte IV +
 * AES-128-CBC ciphertext.
 *
 * Scope rules, in the order a reviewer will look for them:
 *
 *   1. **suno.com and suno.ai hosts only.** The SQL restricts on `host_key`;
 *      nothing else is read out of the store. suno.ai is included because
 *      it 308-redirects to suno.com (checked 2026-10-17), so a browser
 *      that first landed on suno.ai may hold the Clerk cookies under
 *      suno.ai host keys. Each domain is an exact match plus a
 *      dot-prefixed subdomain match — never a bare suffix match, which
 *      would also catch lookalikes like `evilsuno.com`. This module
 *      never enumerates a cookie jar.
 *   2. **The Clerk family only.** `__client` (which prefixes `__client_uat`)
 *      and `__session`. Stripe, analytics and Django session cookies are left
 *      alone even when they sit on the same rows.
 *   3. **No value is ever logged**, and no value appears in a QString passed
 *      outside this module except the assembled header, which goes straight
 *      to `SunoClient::setCookie()` — the same call the paste path makes, and
 *      the one that persists through `CredentialStore`.
 *   4. **Explicit user action only.** The first read of a browser's Safe
 *      Storage secret makes macOS ask the user to allow it, which is exactly
 *      the consent moment this feature needs; it is not called on startup.
 *
 * @version 1.0.0
 */

#pragma once

#include <QString>
#include <QStringList>

namespace vc::suno::auth {

/// Result of a harvest attempt. Diagnostic fields carry no secrets and no
/// filesystem paths — they exist so the UI can say what happened.
struct BrowserCookieHarvest {
    /// "name=value; name=value" for the Clerk family, in a fixed order.
    /// Empty when nothing was found; never null.
    QString cookieHeader;
    /// Human-facing browser name ("Brave"), empty on failure.
    QString browserName;
    /// Which installed browsers were tried and had nothing to offer.
    QStringList browsersSearched;
    /// What went wrong, user-facing, no secrets. Empty on success.
    QString reason;
};

/// What one browser's cookie store attempt actually saw, recorded with names
/// and counts only — no values, no paths. This exists because T0199's first
/// failure report ("the button did not work", user signed IN) collapsed four
/// different causes into one message: a declined keychain prompt, a keychain
/// prompt that outlived its wait, a store with only the signed-out marker
/// (`__client_uat = 0`, which is exactly what a logged-out Clerk store holds),
/// and a genuine decrypt failure. Each of those needs a different thing from
/// the user, so the harvest must be able to tell them apart.
struct StoreObservation {
    bool keyMissing = false;

    /// Clerk-family rows present in the store, by name.
    int clerkRows = 0;

    /// Of those rows, the ones that decrypted to printable text.
    int decrypted = 0;

    /// A `__client` (non-`uat`) or `__session` member was among them. This is
    /// the durability bar: a header without one carries no credential, and
    /// `__client_uat` alone is neither signed-in proof nor a credential.
    bool hadCredential = false;

    /// Every decrypted value that was an `__client_uat` member read as "0" —
    /// Clerk's signed-out marker, observed live in this machine's prepared
    /// signed-out store. Distinct from "no Clerk rows at all", which means the
    /// browser has never visited suno.com.
    bool sawSignedOut = false;
};

class BrowserCookieImport {
public:
    /// Browsers this build knows how to read, in preference order: the
    /// default browser first is the whole point, since that is where the
    /// sign-in the user just performed actually landed.
    static QStringList candidateBrowserNames();

    /// Read the suno.com / suno.ai Clerk cookies out of the first candidate that has
    /// them. Never throws; failure is reported through `reason`.
    static BrowserCookieHarvest harvest();

    // ── Test seams ────────────────────────────────────────────────────────
    // The seams exist because the two things worth testing — the decryption
    // and the header assembly — are pure, while the keychain read is
    // environmental and the store query needs a real file. Tests drive the
    // seams; production goes through harvest().

    /// Decrypt one Chromium-family `v10` cookie value. Returns the plaintext,
    /// empty when the value is not a v10 blob or does not decrypt to
    /// printable text.
    static QByteArray decryptChromiumValue(const QByteArray& encrypted,
                                           const QByteArray& key);

    /// Derive the AES key for a browser's cookie store from its Keychain
    /// secret. Empty on failure.
    static QByteArray deriveChromiumKey(const QByteArray& keychainSecret);

    /// Assemble "name=value; ..." from (name, value) pairs in a fixed order:
    /// `__client*` before `__session*`, unsuffixed before suffixed, so the
    /// header is byte-identical across runs and machines.
    static QString assembleCookieHeader(const QList<QPair<QString, QString>>& pairs);

    /// True when the header carries something a bearer fetch can actually
    /// use: a `__client` member that is not `__client_uat`, or any `__session`
    /// member. A `__client_uat = 0` header is a signed-out store, not a
    /// session — the first shipped version accepted it, stored it through
    /// `setCookie()`, and reported nothing, which is how "the button did not
    /// work" reached a user who was signed out.
    static bool headerCarriesUsableCredential(const QString& header);

    /// Query a cookie database file for the suno.com / suno.ai Clerk family, decrypting
    /// with `key`. The key is a parameter rather than an internal keychain
    /// read so a test can drive this with its own key: shells out to the
    /// Keychain, which prompts, and a test that prompts interrupts whoever
    /// ran it. Production obtains the key in harvest().
    static QList<QPair<QString, QString>> readStore(const QString& databasePath,
                                                    const QByteArray& key);

    /// The same read, recording what it saw into `observation` so the failure
    /// message can name the actual cause. `observation` may be null.
    static QList<QPair<QString, QString>> readStore(const QString& databasePath,
                                                    const QByteArray& key,
                                                    StoreObservation* observation);

    /// Turn one browser's observation into the user-facing sentence for it.
    /// Pure and named per-cause so a change to one message cannot silently
    /// bend the others.
    static QString reasonForStore(const StoreObservation& seen,
                                  const QString& browserName);
};

} // namespace vc::suno::auth