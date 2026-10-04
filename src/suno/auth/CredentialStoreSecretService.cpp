// CredentialStoreSecretService.cpp - freedesktop Secret Service backend for Linux.
//
// Sibling of the macOS KeychainBackend and the Win32 Credential Manager
// backend. Same interface, same contract: upsert on store, "absent" distinct
// from "broken" on load, removing an absent key is not an error, never log the
// secret.
//
// WHY RAW D-Bus AND NOT libsecret
// -------------------------------
// libsecret-1 is the obvious choice and was rejected on three grounds:
//
//  1. It is a glib library. This is a Qt application whose entire event loop,
//     signal delivery and object lifetime model is Qt's. libsecret's
//     secret_service_sync() spins a GMainContext, and two event loops have to
//     be pumped in lockstep or a synchronous call deadlocks. Every caller of
//     this class is already on the CredentialStoreWorker thread
//     (SunoClient.cpp runCredentialStoreRequest), so the alternative is a second
//     event loop to own for one round trip.
//  2. It adds a hard link dependency (libsecret-1 + glib + gio) to a project
//     that links Qt, FFmpeg, taglib and projectM. Per AGENTS.md a stripped
//     distro must degrade, not fail to build - and the freedesktop wire protocol
//     is the stable contract anyway.
//  3. The API used here is small and fully specified: OpenSession, CreateItem,
//     SearchItems, GetSecret, Delete. That is about a page of inspectable code
//     rather than an abstraction hidden behind GObject introspection.
//
// libsecret becomes the better answer only if this must talk to something that
// is not a Secret Service (a KWallet quirk, a TPM), because its collection and
// prompting logic is genuinely broad. It is not the better answer for "put one
// string in the OS store and get it back".
//
// WHAT HAPPENS ON A HEADLESS BOX
// -----------------------------
// Nothing is written anywhere. The probe returns an all-false
// SecretServiceProbe, classifySecretService() turns that into
// SecretServiceVerdict::NoSessionBus, and decideBackend() selects
// BackendChoice::Unavailable / UnavailableReason::NoSessionBus. The store then
// refuses every operation with a message naming the condition. There is no
// plaintext fallback - that silence is the bug these backends exist to remove.
//
// Compiled only when CHADVIS_HAS_SECRET_SERVICE is defined; inert otherwise, so
// the translation unit can be listed unconditionally in cmake/Sources.cmake.
//
// NOT COMPILED OR EXECUTED on the machine that wrote it (macOS: no D-Bus session
// bus, no Secret Service, no Linux). The wire-format choices are from the
// freedesktop specification. See the report for what that does and does not
// verify.

#include "CredentialStore.hpp"

#include "core/Logger.hpp"

#include <QString>

#ifdef CHADVIS_HAS_SECRET_SERVICE
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QDBusVariant>
#include <QVariantMap>
#endif

namespace vc::suno::auth {
namespace {

// ---------------------------------------------------------------------------
// Protocol constants (freedesktop Secret Service specification).
//
// Pinned as constants because they ARE the protocol: a typo in one is a runtime
// failure on a machine that cannot be tested from here, and a grep for the
// literal is how a future reader audits the wire format.
// ---------------------------------------------------------------------------

constexpr auto kSecretServiceName = "org.freedesktop.secrets";
constexpr auto kSecretServicePath = "/org/freedesktop/secrets";
constexpr auto kServiceInterface = "org.freedesktop.Secret.Service";
constexpr auto kCollectionInterface = "org.freedesktop.Secret.Collection";
constexpr auto kItemInterface = "org.freedesktop.Secret.Item";
constexpr auto kPropertiesInterface = "org.freedesktop.DBus.Properties";

/// Attribute keys. A schema-less item is a "generic" secret; Label is what the
/// user sees in their keyring UI, so it carries the namespaced key.
constexpr auto kAttrLabel = "org.freedesktop.Secret.Item.Label";
constexpr auto kAttrDescription = "org.freedesktop.Secret.Item.Description";

/// Content type for a stored UTF-8 string. Advisory - the keyring does not
/// enforce it - but it is what makes the entry readable in Seahorse/Keyer.
constexpr auto kContentType = "text/plain; charset=utf8";

/// "plain" means no session encryption: the secret crosses the bus as exactly
/// the bytes encodeSecret() produced. That is the right choice, not a shortcut:
///
///  - The bus is a *session* bus, reachable only by processes in the user's own
///    session. The payload is protected at rest by the keyring's own encryption
///    (login keyring, KWallet, KeePassXC), which is the property we want.
///    A second layer over a session bus defends against nothing that is not
///    already defended.
///  - "dh" requires a crypto backend plus a key exchange to be implemented and
///    audited here, to protect a string a co-process on the same bus can
///    already observe by other means.
constexpr auto kPlainAlgorithm = "plain";

/// D-Bus round-trip budget. This runs on a worker thread during startup, so the
/// wait must be bounded: an unbounded call on a thread that is joined at
/// shutdown is a hang, not a slow start. 5s is long enough for a loaded keyring
/// and short enough that a wedged service is a diagnosable error.
constexpr int kCallTimeoutMs = 5000;

} // namespace

#ifdef CHADVIS_HAS_SECRET_SERVICE

namespace {

QString qstr(const char* s) { return QString::fromLatin1(s); }

/// Build a method call on the Secret Service, addressed by interface + method.
QDBusMessage serviceCall(const QString& path, const char* interface, const QString& method) {
    return QDBusMessage::createMethodCall(qstr(kSecretServiceName), path, qstr(interface), method);
}

/// The D-Bus error name a failing call returned, for an error message.
/// Never includes arguments, so a service cannot echo a secret into our log by
/// returning one.
QString wireError(const QDBusMessage& reply) {
    if (reply.errorName().isEmpty()) return QStringLiteral("no error name");
    return reply.errorName();
}

/// Read a collection's "Locked" property.
bool collectionIsLocked(QDBusConnection& bus, const QString& collection, bool* readable) {
    QDBusMessage msg = serviceCall(collection, kPropertiesInterface, QStringLiteral("Get"));
    msg << qstr(kCollectionInterface) << QStringLiteral("Locked");
    const QDBusMessage reply = bus.call(msg, QDBus::Block, kCallTimeoutMs);
    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
        *readable = true;
        return qdbus_cast<QVariant>(reply.arguments().first()).toBool();
    }
    // An implementation that will not report Locked is treated as LOCKED. Failing
    // open here would mean silently writing a secret somewhere the user believes
    // is protected; failing closed costs one retry after they unlock. The
    // asymmetry is the whole point.
    LOG_WARN("CredentialStore: cannot read the 'Locked' property of the default keyring "
             "collection ({}) - treating it as locked and refusing to store the credential",
             wireError(reply).toStdString());
    *readable = false;
    return true;
}

/// Marshal the spec's (oayays) Secret structure:
///   session object path, algorithm parameters (empty for "plain"),
///   the value bytes, content type.
QDBusArgument secretStructure(const QString& sessionPath, const QByteArray& value) {
    QDBusArgument arg;
    arg.beginStructure();
    arg << QDBusObjectPath(sessionPath);
    arg << QByteArray(); // parameters: empty for "plain"
    arg << value;
    arg << qstr(kContentType);
    arg.endStructure();
    return arg;
}

} // namespace

/// Secret Service backend: OpenSession / CreateItem / SearchItems / GetSecret /
/// Delete over the session bus.
class SecretServiceBackend final : public CredentialStore::ISecretBackend {
public:
    [[nodiscard]] bool isSecure() const noexcept override { return true; }

    Result<void> store(const QString& nsKey, const QString& secret) override {
        const auto encoded = encodeSecret(secret);
        if (!encoded) {
            return Result<void>::err(
                    Error(describeEncodeDefect(encoded.error(), secret).toStdString()));
        }
        const QString collection = defaultCollection();
        if (collection.isEmpty()) {
            return Result<void>::err(Error(
                    describeUnavailable(UnavailableReason::NoDefaultCollection).toStdString()));
        }
        auto session = openPlainSession();
        if (!session) {
            return Result<void>::err(session.error());
        }

        QVariantMap properties;
        properties.insert(qstr(kAttrLabel), namespacedKeyFor(nsKey));
        properties.insert(qstr(kAttrDescription),
                          QStringLiteral("ChadVis secret storage (delete with the app)"));

        QDBusMessage msg =
                serviceCall(collection, kCollectionInterface, QStringLiteral("CreateItem"));
        msg << properties;
        // The Secret structure is marshalled by hand: QtDBus has no C++ type for
        // a D-Bus tuple, so it is built into a QDBusArgument and handed over in
        // a QVariant - which is the only overload QDBusMessage::operator<< takes.
        // Wrapping it as a plain QByteArray would send the *bytes* rather than the
        // struct, and the service would reject the call.
        msg << QVariant::fromValue(secretStructure(*session, *encoded));
        // replace = true makes this an upsert in ONE call, like CredWriteW. The
        // macOS backend needs update-then-add because SecItemAdd rejects an
        // existing item; the Secret Service has a flag for exactly this.
        msg << true;

        const QDBusMessage reply = bus_.call(msg, QDBus::Block, kCallTimeoutMs);
        if (reply.type() != QDBusMessage::ReplyMessage) {
            return Result<void>::err(
                    Error(QStringLiteral("CredentialStore Secret Service CreateItem failed: %1")
                                  .arg(wireError(reply))
                                  .toStdString()));
        }
        // A non-empty prompt path would mean the keyring wants to ask the user
        // something. We never show it: this is a worker thread.
        const QString prompt = promptPath(reply);
        if (!prompt.isEmpty()) {
            LOG_WARN("CredentialStore: Secret Service CreateItem returned an unlock prompt; "
                     "unlock your keyring in your desktop session and retry");
            return Result<void>::err(
                    Error(describeUnavailable(UnavailableReason::CollectionLocked).toStdString()));
        }
        return Result<void>::ok();
    }

    Result<QString> load(const QString& nsKey) override {
        const QString collection = defaultCollection();
        if (collection.isEmpty()) {
            return Result<QString>::err(Error(
                    describeUnavailable(UnavailableReason::NoDefaultCollection).toStdString()));
        }
        auto session = openPlainSession();
        if (!session) {
            return Result<QString>::err(session.error());
        }

        // SearchItems by Label. The spec takes an attribute map and returns
        // (unlocked, locked); we ask for our own key and take the first unlocked
        // hit. A locked hit is NOT unlocked by us - see the collectionLocked note.
        QVariantMap attributes;
        attributes.insert(qstr(kAttrLabel), namespacedKeyFor(nsKey));

        QDBusMessage search =
                serviceCall(collection, kCollectionInterface, QStringLiteral("SearchItems"));
        search << attributes;
        const QDBusMessage found = bus_.call(search, QDBus::Block, kCallTimeoutMs);
        if (found.type() != QDBusMessage::ReplyMessage) {
            return Result<QString>::err(
                    Error(QStringLiteral("CredentialStore Secret Service SearchItems failed: %1")
                                  .arg(wireError(found))
                                  .toStdString()));
        }

        QStringList unlocked;
        QStringList locked;
        splitSearchResults(found, &unlocked, &locked);
        if (unlocked.isEmpty()) {
            if (!locked.isEmpty()) {
                LOG_WARN("CredentialStore: the stored credential is in a LOCKED keyring "
                         "collection; unlock it in your desktop session (ChadVis will not prompt "
                         "from a background thread)");
                return Result<QString>::err(Error(
                        describeUnavailable(UnavailableReason::CollectionLocked).toStdString()));
            }
            // Genuinely absent. Distinct from every failure above, which is the
            // whole reason the split exists.
            return Result<QString>::err(
                    Error(QStringLiteral("No secret stored under '%1'").arg(nsKey).toStdString(),
                          static_cast<int>(kWinErrorNotFound)));
        }

        QDBusMessage get =
                serviceCall(unlocked.first(), kItemInterface, QStringLiteral("GetSecret"));
        get << QDBusObjectPath(*session);
        const QDBusMessage reply = bus_.call(get, QDBus::Block, kCallTimeoutMs);
        if (reply.type() != QDBusMessage::ReplyMessage) {
            return Result<QString>::err(
                    Error(QStringLiteral("CredentialStore Secret Service GetSecret failed: %1")
                                  .arg(wireError(reply))
                                  .toStdString()));
        }
        if (!promptPath(reply).isEmpty()) {
            return Result<QString>::err(
                    Error(describeUnavailable(UnavailableReason::CollectionLocked).toStdString()));
        }
        return Result<QString>::ok(decodeSecretStructure(reply, nsKey));
    }

    Result<void> remove(const QString& nsKey) override {
        const QString collection = defaultCollection();
        if (collection.isEmpty()) {
            return Result<void>::err(Error(
                    describeUnavailable(UnavailableReason::NoDefaultCollection).toStdString()));
        }
        QVariantMap attributes;
        attributes.insert(qstr(kAttrLabel), namespacedKeyFor(nsKey));

        QDBusMessage search =
                serviceCall(collection, kCollectionInterface, QStringLiteral("SearchItems"));
        search << attributes;
        const QDBusMessage found = bus_.call(search, QDBus::Block, kCallTimeoutMs);
        if (found.type() != QDBusMessage::ReplyMessage) {
            return Result<void>::err(
                    Error(QStringLiteral("CredentialStore Secret Service SearchItems failed: %1")
                                  .arg(wireError(found))
                                  .toStdString()));
        }
        QStringList unlocked;
        QStringList locked;
        splitSearchResults(found, &unlocked, &locked);
        if (unlocked.isEmpty()) {
            // Nothing to delete, including "only a locked copy exists". Removing
            // an absent key is not an error - same contract as the other two
            // backends.
            return Result<void>::ok();
        }

        QDBusMessage del = serviceCall(unlocked.first(), kItemInterface, QStringLiteral("Delete"));
        const QDBusMessage reply = bus_.call(del, QDBus::Block, kCallTimeoutMs);
        if (reply.type() != QDBusMessage::ReplyMessage) {
            return Result<void>::err(Error(QStringLiteral("CredentialStore Secret Service Delete "
                                                          "failed: %1")
                                                   .arg(wireError(reply))
                                                   .toStdString()));
        }
        return Result<void>::ok();
    }

private:
    explicit SecretServiceBackend(QDBusConnection bus) : bus_(std::move(bus)) {}

    /// The "default" collection alias. Re-read per operation rather than cached
    /// at construction: a keyring can appear or lock between two calls in one
    /// startup, and a cached path would keep reporting a stale answer.
    [[nodiscard]] QString defaultCollection() const {
        QDBusMessage msg = serviceCall(qstr(kSecretServicePath), kServiceInterface,
                                       QStringLiteral("ReadAlias"));
        msg << QStringLiteral("default");
        const QDBusMessage reply = bus_.call(msg, QDBus::Block, kCallTimeoutMs);
        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
            return {};
        }
        return qdbus_cast<QDBusObjectPath>(reply.arguments().first()).path();
    }

    /// OpenSession("plain"). The spec expects the client to own the returned
    /// session path so the service can call back on it (Prompt, Close). We
    /// deliberately do NOT export it:
    ///
    ///  - The only callback that matters is Prompt, and we never run a prompt
    ///    (see the store() comment), so there is nothing to receive.
    ///  - Exporting would mean giving this non-QObject class a QObject stand-in
    ///    purely so a callback that cannot happen lands somewhere harmless.
    ///
    /// The cost is that a service which calls Prompt anyway logs its own
    /// unhandled-message warning. That is the correct failure: we want the
    /// keyring to report that it wanted a prompt, not to have us answer one from
    /// a worker thread during startup.
    Result<QString> openPlainSession() {
        QDBusMessage msg = serviceCall(qstr(kSecretServicePath), kServiceInterface,
                                       QStringLiteral("OpenSession"));
        msg << qstr(kPlainAlgorithm) << QVariant(); // empty input for "plain"
        const QDBusMessage reply = bus_.call(msg, QDBus::Block, kCallTimeoutMs);
        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().size() < 2) {
            return Result<QString>::err(
                    Error(QStringLiteral("CredentialStore Secret Service OpenSession failed: %1")
                                  .arg(wireError(reply))
                                  .toStdString()));
        }
        // Arguments are (output variant, result object path).
        const QString path = qdbus_cast<QDBusObjectPath>(reply.arguments().at(1)).path();
        if (path.isEmpty()) {
            return Result<QString>::err(
                    std::string("CredentialStore: Secret Service returned an empty session path"));
        }
        return Result<QString>::ok(path);
    }

    /// SearchItems returns (ao unlocked, ao locked). Both are read with the
    /// const reader API - in Qt6 the traversal methods (beginArray/atEnd) are
    /// const members, so they take a const QDBusArgument and yield values
    /// through operator>>.
    static void splitSearchResults(const QDBusMessage& reply, QStringList* unlocked,
                                   QStringList* locked) {
        if (reply.arguments().size() < 2) return;
        *unlocked = objectPaths(reply.arguments().at(0));
        *locked = objectPaths(reply.arguments().at(1));
    }

    static QStringList objectPaths(const QVariant& value) {
        QStringList out;
        const QDBusArgument arg = qdbus_cast<QDBusArgument>(value);
        // currentSignature() distinguishes a bare 'o' (a single path, which is
        // what a one-element reply can look like after demarshalling) from 'ao'.
        if (arg.currentSignature().startsWith(QLatin1Char('o'))) {
            out << qdbus_cast<QDBusObjectPath>(arg).path();
            return out;
        }
        arg.beginArray();
        while (!arg.atEnd()) {
            QDBusObjectPath path;
            arg >> path;
            out << path.path();
        }
        arg.endArray();
        return out;
    }

    /// The prompt path is the LAST argument of a spec reply when non-empty. It is
    /// only ever read to decide whether the keyring wants to ask the user
    /// something - never followed.
    static QString promptPath(const QDBusMessage& reply) {
        if (reply.arguments().isEmpty()) return {};
        const QVariant last = reply.arguments().last();
        // Qt6: userType() is gone; metaType() is the type identity. A DBus
        // Properties.Get reply boxes its value in a QDBusVariant.
        if (last.metaType() != QMetaType::fromType<QDBusVariant>()) return {};
        const QVariant boxed = qdbus_cast<QVariant>(last);
        if (!boxed.canConvert<QDBusObjectPath>()) return {};
        const QString path = qdbus_cast<QDBusObjectPath>(boxed).path();
        return path.isEmpty() ? QString() : path;
    }

    /// Unpack GetSecret's (oayays) Secret structure back into a QString. The
    /// value's byte length is authoritative and the bytes are decoded as UTF-8;
    /// nothing here writes the secret to a log.
    static QString decodeSecretStructure(const QDBusMessage& reply, const QString& nsKey) {
        if (reply.arguments().isEmpty()) {
            return {};
        }
        const QDBusArgument arg = qdbus_cast<QDBusArgument>(reply.arguments().first());
        if (!arg.currentSignature().startsWith(QLatin1Char('('))) {
            return {};
        }
        arg.beginStructure();
        QDBusObjectPath session; // unused on read
        QByteArray params;       // empty for "plain"
        QByteArray value;
        arg >> session >> params >> value;
        arg.endStructure();
        return QString::fromUtf8(value);
    }

    QDBusConnection bus_;
};

#endif // CHADVIS_HAS_SECRET_SERVICE

namespace detail {

SecretServiceProbe probeSecretServiceOverDBus() {
#ifdef CHADVIS_HAS_SECRET_SERVICE
    SecretServiceProbe probe;

    // Step 1: the session bus. On a headless box, inside a container, or over
    // plain ssh, sessionBus() fails and there is nothing on which to look for a
    // service - which is why NoSessionBus is reported rather than
    // ServiceNotRunning.
    QDBusConnection bus = QDBusConnection::sessionBus();
    probe.sessionBusConnected = bus.isConnected();
    if (!probe.sessionBusConnected) {
        return probe;
    }

    // Step 2: is anybody actually providing the service? This is a real round
    // trip to the bus, so a stale name in the local cache cannot make us claim a
    // keyring that is not there.
    if (QDBusConnectionInterface* iface = bus.interface()) {
        const QDBusReply<bool> registered =
                iface->isServiceRegistered(QString::fromLatin1(kSecretServiceName));
        if (registered.isValid()) {
            probe.serviceRegistered = registered.value();
        }
    }
    if (!probe.serviceRegistered) {
        return probe;
    }

    // Step 3: the default collection alias. A missing alias is a real, common
    // state: gnome-keyring only has one after the user has set a login password,
    // and a fresh container has none at all.
    QDBusMessage alias = serviceCall(QString::fromLatin1(kSecretServicePath), kServiceInterface,
                                     QStringLiteral("ReadAlias"));
    alias << QStringLiteral("default");
    const QDBusMessage aliasReply = bus.call(alias, QDBus::Block, kCallTimeoutMs);
    if (aliasReply.type() != QDBusMessage::ReplyMessage || aliasReply.arguments().isEmpty()) {
        return probe; // defaultCollectionPresent stays false
    }
    probe.defaultCollectionPresent = true;

    // Step 4: locked or not. Read from the collection's "Locked" property and
    // then REFUSE if locked - Unlock() is never called, because it may raise a
    // GUI prompt on the agent and this runs on a worker thread with no business
    // putting a modal dialog on someone's screen during startup.
    const QString collection = qdbus_cast<QDBusObjectPath>(aliasReply.arguments().first()).path();
    bool readable = false;
    probe.collectionLocked = collectionIsLocked(bus, collection, &readable);
    if (!readable) {
        probe.collectionLocked = true; // collectionIsLocked already logged why
    }
    return probe;
#else
    // Not compiled on this platform. An all-false probe reports "no bus" rather
    // than fabricating a success, so the message the user sees names the real
    // problem (this build has no Secret Service support) instead of implying
    // their keyring is at fault.
    return SecretServiceProbe{};
#endif
}

std::unique_ptr<CredentialStore::ISecretBackend> makeSecretServiceBackend() {
#ifdef CHADVIS_HAS_SECRET_SERVICE
    // Re-probe rather than trusting a cached probe: decideBackend() already
    // asked whether the store is usable, and between that decision and this
    // construction the user may have started their keyring. Constructing only
    // when the answer is still yes keeps the two in agreement.
    if (classifySecretService(probeSecretServiceOverDBus()) != SecretServiceVerdict::Usable) {
        return nullptr;
    }
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        return nullptr;
    }
    return std::make_unique<SecretServiceBackend>(std::move(bus));
#else
    return nullptr;
#endif
}

} // namespace detail
} // namespace vc::suno::auth