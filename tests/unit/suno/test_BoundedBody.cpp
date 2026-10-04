#include <QtTest>

#include "suno/HttpPolicy.hpp"
#include "suno/SunoAccountManager.hpp"
#include "suno/SunoClient.hpp"
#include "suno/SunoExploreService.hpp"
#include "suno/SunoNotificationService.hpp"

#include <QBuffer>
#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QSignalSpy>
#include <QString>
#include <QUrl>

#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace vc::suno;

namespace fs = std::filesystem;

// ─────────────────────────────────────────────────────────────
// Fixtures
// ─────────────────────────────────────────────────────────────

/// A reply whose body arrives only in the slices a test asks for.
///
/// Three properties make it the right fixture here, and all three are load
/// bearing rather than convenient:
///  * `feed()` emits readyRead, so the accessor is exercised the way a real
///    transfer drives it -- pump, pump, pump -- instead of only at finished();
///  * `bytesAvailable()` and `readData()` are exact, so "the reader stopped one
///    byte past the cap" is an assertion about bytes rather than about a fake's
///    bookkeeping;
///  * `abort()` emits finished() SYNCHRONOUSLY, where real Qt posts it through a
///    queued `_q_aborted`. That is stricter, not looser: the abort re-enters
///    SunoClient::handleReplyFinished from inside SunoClient::pumpTrackedBody's
///    own call stack, so a dangling iterator there would be caught by every
///    breach test here rather than only in production.
class PumpingReply final : public QNetworkReply
{
public:
    PumpingReply(const QNetworkRequest& request, const std::string& method,
                 const QByteArray& payload)
    {
        setRequest(request);
        setUrl(request.url());
        setOperation(method == "POST" ? QNetworkAccessManager::PostOperation
                                      : QNetworkAccessManager::GetOperation);
        setOpenMode(ReadOnly | Unbuffered);
        // QByteArray is implicitly shared, so handing a 16 MiB body to four
        // replies costs one allocation. It matters because three tests here need
        // a body at the real cap, not a toy one.
        payload_ = payload;
    }

    void abort() override
    {
        if (finished_) {
            return;
        }
        aborted_ = true;
        finished_ = true;
        setError(QNetworkReply::OperationCanceledError, QStringLiteral("aborted"));
        emit finished();
    }

    qint64 bytesAvailable() const override
    {
        return available_ - consumed_ + QNetworkReply::bytesAvailable();
    }

    qint64 readData(char* data, qint64 maxSize) override
    {
        const qint64 remaining = available_ - consumed_;
        if (remaining <= 0) {
            return 0;
        }
        const qint64 count = qMin(maxSize, remaining);
        std::memcpy(data, payload_.constData() + consumed_, static_cast<std::size_t>(count));
        consumed_ += count;
        return count;
    }

    /// Make up to `bytes` more bytes readable, then announce them the way a
    /// transport does. A no-op once finished, so a test that keeps feeding after
    /// an abort cannot resurrect the reply.
    void feed(const qint64 bytes)
    {
        if (finished_) {
            return;
        }
        available_ = qMin(available_ + bytes, static_cast<qint64>(payload_.size()));
        emit readyRead();
    }

    /// Deliver everything at once and finish, WITHOUT a readyRead. This is the
    /// late-arrival shape: a body small enough to arrive in one chunk, or one
    /// the event loop never got round to announcing. It is why readTrackedBody
    /// drains once more before answering.
    void finish(const int status)
    {
        if (finished_) {
            return;
        }
        available_ = payload_.size();
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
        finished_ = true;
        emit finished();
    }

    [[nodiscard]] bool wasAborted() const { return aborted_; }
    [[nodiscard]] qint64 consumed() const { return consumed_; }

private:
    QByteArray payload_;
    qint64 available_ = 0;
    qint64 consumed_ = 0;
    bool finished_ = false;
    bool aborted_ = false;
};

/// The real cap, not a test-sized one. `HttpPolicy` owns the number and this
/// suite deliberately runs against it, so a change to the studio-API cap is a
/// test-visible fact rather than a number this file quietly copies.
constexpr qint64 kCap = http::maxResponseBytes(http::RequestClass::JsonApi);
constexpr qint64 kSlice = 1024 * 1024;

/// Built once per process and shared by ref. 16 MiB, and building it four times
/// would be pure waste.
const QByteArray& bodyAtCap()
{
    static const QByteArray body(static_cast<qsizetype>(kCap), 'y');
    return body;
}

/// One byte past the cap.
///
/// 'x' is not valid JSON, on purpose. If a breach were ever *not* detected, the
/// services' parsers would choke on this and every assertion below that names the
/// limit would fail -- rather than a test passing for the wrong reason because
/// the body happened to parse.
const QByteArray& bodyOneByteOverCap()
{
    static const QByteArray body(static_cast<qsizetype>(kCap + 1), 'x');
    return body;
}

/// A multi-slice body whose bytes are position-dependent, so a truncated read
/// cannot equal it. Built as real JSON so the same payload can drive the service
/// success paths too.
QByteArray lyricsBody()
{
    static const QByteArray body = [] {
        QJsonArray words;
        for (int i = 0; i < 2048; ++i) {
            words.append(QJsonObject{
                {QStringLiteral("start"), 0.5 * i},
                {QStringLiteral("end"), 0.5 * i + 0.4},
                {QStringLiteral("word"), QStringLiteral("token-%1").arg(i)},
            });
        }
        return QJsonDocument(QJsonObject{
            {QStringLiteral("notified_at"), QStringLiteral("2026-10-04T00:00:00Z")},
            {QStringLiteral("words"), words},
        }).toJson(QJsonDocument::Compact);
    }();
    return body;
}

QByteArray compactJson(const QJsonObject& object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QJsonObject explorePage()
{
    return QJsonObject{
        {QStringLiteral("feeds"), QJsonArray{
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("feed-1")},
                {QStringLiteral("title"), QStringLiteral("Trending")},
                {QStringLiteral("items"), QJsonArray{
                    QJsonObject{{QStringLiteral("item"),
                                 QJsonObject{{QStringLiteral("clip_schema"),
                                              QJsonObject{{QStringLiteral("id"),
                                                          QStringLiteral("clip-1")},
                                                         {QStringLiteral("title"),
                                                          QStringLiteral("One")},
                                                         {QStringLiteral("status"),
                                                          QStringLiteral("complete")}}}}}},
                }},
            },
        }},
        {QStringLiteral("next_cursor"), QStringLiteral("cursor-2")},
    };
}

QJsonObject notificationList()
{
    return QJsonObject{
        {QStringLiteral("notified_at"), QStringLiteral("2026-10-04T00:00:00.000Z")},
        {QStringLiteral("notifications"), QJsonArray{
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("n-1")},
                {QStringLiteral("notification_type"), QStringLiteral("comment_mention")},
                {QStringLiteral("is_read"), false},
                {QStringLiteral("user_profiles"), QJsonArray{
                    QJsonObject{{QStringLiteral("display_name"), QStringLiteral("Ada")},
                                {QStringLiteral("handle"), QStringLiteral("ada")},
                                {QStringLiteral("user_id"), QStringLiteral("u-1")}},
                }},
                {QStringLiteral("content_title"), QStringLiteral("A clip")},
            },
        }},
    };
}

QJsonObject sessionEnvelope()
{
    return QJsonObject{
        {QStringLiteral("user"),
         QJsonObject{{QStringLiteral("id"), QStringLiteral("u-1")},
                     {QStringLiteral("email"), QStringLiteral("ada@example.invalid")},
                     {QStringLiteral("username"), QStringLiteral("ada")},
                     {QStringLiteral("display_name"), QStringLiteral("Ada")},
                     {QStringLiteral("handle"), QStringLiteral("ada")}}},
        {QStringLiteral("models"), QJsonArray{
            QJsonObject{
                {QStringLiteral("name"), QStringLiteral("chirp-v3.5")},
                {QStringLiteral("external_key"), QStringLiteral("chirp-v3-5")},
                {QStringLiteral("major_version"), 3},
                {QStringLiteral("can_use"), true},
                {QStringLiteral("max_lengths"),
                 QJsonObject{{QStringLiteral("prompt"), 5000}}},
            },
        }},
    };
}

/// Per-endpoint valid envelopes, so a service's success path can be driven
/// without the test knowing the captured route strings.
QByteArray validJsonFor(const QUrl& url)
{
    const QString path = url.path();
    // Ordered most specific first: "/notification/v2/badge-count" and
    // "/notification/v2/read" both contain "/notification/v2".
    if (path.contains(QStringLiteral("/notification/v2/badge-count"))) {
        return compactJson(QJsonObject{{QStringLiteral("badge_count"), 3}});
    }
    if (path.contains(QStringLiteral("/unified/explore"))) {
        return compactJson(explorePage());
    }
    if (path.contains(QStringLiteral("/notification/v2/read"))) {
        return compactJson(QJsonObject{{QStringLiteral("ok"), true}});
    }
    if (path.contains(QStringLiteral("/notification/v2"))) {
        return compactJson(notificationList());
    }
    if (path.contains(QStringLiteral("/billing/info/"))) {
        return compactJson(QJsonObject{
            {QStringLiteral("credits"), 12},
            {QStringLiteral("plan"), QJsonObject{{QStringLiteral("name"),
                                                  QStringLiteral("free")}}},
        });
    }
    if (path.contains(QStringLiteral("/session/"))) {
        return compactJson(sessionEnvelope());
    }
    return compactJson(QJsonObject{{QStringLiteral("words"), QJsonArray{}}});
}

/// Owns the replies SunoClient's factory produced, so a test can reach them once
/// the queue timer (1000 ms, SunoClient.cpp:290) has fired.
///
/// The handles are `QPointer`, and that is load-bearing rather than defensive.
/// `SunoClient` owns every reply it issues: `handleJsonReply` calls
/// `reply->deleteLater()` once it has the body, and `deleteLater` posts a
/// DeferredDelete that the event loop runs on the next turn. A raw
/// `PumpingReply*` therefore dangles after any `QTest::qWait`, and touching it is
/// a use-after-free -- which is not a failed assertion but a SIGSEGV inside Qt's
/// `QHash<QNetworkRequest::Attribute, QVariant>::emplace`, i.e. `setAttribute()`
/// writing into freed memory, with the originating test function named as the
/// crash site and no mention of the fixture that caused it. That is precisely
/// what `drainReplies` did, so `issued_` is weak and `live()` is what any loop
/// that spans an event-loop turn must walk.
class ReplyFarm
{
public:
    void setChooser(std::function<QByteArray(const QUrl&)> chooser)
    {
        chooser_ = std::move(chooser);
    }

    [[nodiscard]] SunoClient::ReplyFactory factory()
    {
        return [this](const QNetworkRequest& request, const std::string& method,
                      const QByteArray&) {
            auto* reply = new PumpingReply(request, method, chooser_(request.url()));
            issued_.push_back(reply);
            return reply;
        };
    }

    /// How many replies the factory has handed out, live or destroyed. This is
    /// "was a reply issued yet", which is what every QTRY_VERIFY on it means, so
    /// it deliberately does NOT shrink when SunoClient deletes one.
    [[nodiscard]] std::size_t issued() const { return issued_.size(); }

    /// Only the replies that still exist. Any loop that runs the event loop
    /// between iterations must use this rather than `issued()`, or it will
    /// dereference a reply the previous iteration's `deleteLater()` already freed.
    [[nodiscard]] std::vector<PumpingReply*> live() const
    {
        std::vector<PumpingReply*> out;
        out.reserve(issued_.size());
        for (const auto& reply : issued_) {
            if (!reply.isNull()) {
                out.push_back(reply.data());
            }
        }
        return out;
    }

    /// The handle for a reply that must still exist. A raw dereference here would
    /// be the same use-after-free in a quieter form, so a dead index is a named
    /// abort naming the cause rather than a crash somewhere else entirely.
    [[nodiscard]] PumpingReply& at(const std::size_t index) const
    {
        PumpingReply* const reply = issued_.at(index).data();
        if (reply == nullptr) {
            qFatal("ReplyFarm: reply %zu no longer exists -- SunoClient deleteLater()'d "
                   "it, so a test must not reach it again once the event loop has run",
                   index);
        }
        return *reply;
    }

private:
    std::function<QByteArray(const QUrl&)> chooser_;
    std::vector<QPointer<PumpingReply>> issued_;
};

/// Finish replies until `expected` of them exist and every one has been answered.
/// Returns false if `expected` never arrived, so the shortfall is reported by name
/// instead of surfacing later as a confusing "the signal never fired".
///
/// `expected` is a parameter rather than a fixed round count, and that is the whole
/// reason this function exists in this shape. **SunoClient serialises authenticated
/// requests behind a 1000 ms queue timer** (`SunoClient.cpp:304`), so a service
/// that issues two requests per refresh — `SunoNotificationService::refresh()` sets
/// `refreshPending_ = 2` and enqueues the list and the badge count — does not have
/// its second request *issued* until the first has been answered. A helper that
/// merely spun for N settle windows returned at 720 ms, before the second request
/// existed at all, left it unfinished forever, and the notification test failed on
/// `changes.count() == 0` with nothing wrong anywhere in `src/`. The count is a
/// parameter because how many requests a refresh makes is the service's business,
/// and only the service knows.
///
/// The two-phase shape follows from that: wait for the replies to EXIST, then
/// finish them in rounds, because a handler can still issue a follow-up.
bool drainReplies(ReplyFarm& farm, const std::size_t expected, const int settleMs = 120)
{
    for (int waited = 0; farm.issued() < expected && waited < 40; ++waited) {
        QTest::qWait(settleMs);
    }
    if (farm.issued() < expected) {
        return false;
    }

    for (int round = 0; round < 8; ++round) {
        const std::size_t before = farm.issued();
        // `live()`, not `issued()`: the qWait below runs the DeferredDelete that
        // `reply->deleteLater()` posted when the previous round's `finish()`
        // answered, so by round 1 the earlier handles are freed memory. Walking
        // `issued()` here is the use-after-free that segfaulted this file's
        // explore test inside Qt's QHash::emplace.
        for (PumpingReply* reply : farm.live()) {
            reply->finish(200);
        }
        QTest::qWait(settleMs);
        if (round > 0 && farm.issued() == before) {
            return true;
        }
    }
    return true;
}

void feedInSlices(PumpingReply& reply, const qint64 total)
{
    for (qint64 fed = 0; fed < total; fed += kSlice) {
        reply.feed(kSlice);
    }
}

QString fakeBearer()
{
    const auto encode = [](const QJsonObject& object) {
        return QString::fromLatin1(
                QJsonDocument(object)
                        .toJson(QJsonDocument::Compact)
                        .toBase64(QByteArray::Base64UrlEncoding |
                                  QByteArray::OmitTrailingEquals));
    };
    const QString header = encode({{QStringLiteral("alg"), QStringLiteral("NONE")},
                                   {QStringLiteral("typ"), QStringLiteral("JWT")}});
    // A real future "exp", because applyRestoreResult() refuses an exp-less
    // bearer outright: a test that injected one would fail authentication and
    // never reach the body path under test.
    const QString payload = encode({{QStringLiteral("exp"), static_cast<qint64>(4102444800)},
                                    {QStringLiteral("sid"), QStringLiteral("sess-test")}});
    return header + QLatin1Char('.') + payload + QStringLiteral(".signature");
}

/// A backend that answers a restore with a usable bearer.
///
/// Needed rather than `setToken()` because `SunoExploreService::refresh()` calls
/// `reloadStoredCredentials()` on every refresh, and `applyRestoreResult()`
/// treats "no stored credential" as "credential cleared"
/// (SunoClient.cpp:547-562) -- it would drop the token a test had just injected
/// and the service would fail with "Not authenticated" instead of the message
/// being tested. A stored bearer takes the early-return branch at
/// SunoClient.cpp:542 and survives the reload.
CredentialStoreWorker::Backend bearerBackend()
{
    const QString jwt = fakeBearer();
    return [jwt](CredentialStoreWorker::Request request) {
        CredentialStoreWorker::Outcome outcome;
        if (request.operation == CredentialStoreWorker::Operation::Restore) {
            outcome.bearer = jwt;
        }
        return outcome;
    };
}

/// Event-loop bound, not a sleep: this waits on the credential worker's own
/// round trip to its private QThread.
void awaitAuthenticated(SunoClient& client)
{
    QTRY_VERIFY_WITH_TIMEOUT(client.isAuthenticated(), 4000);
}

/// ─────────────────────────────────────────────────────────────
/// The source-level guard's lexer
/// ─────────────────────────────────────────────────────────────

/// Source text with comments and string literals blanked out; newlines kept, so
/// every line number in the result still matches the original file.
///
/// Necessary, not decoration: six files under `src/suno` mention `readAll()` in
/// prose while explaining why it must not come back, and a plain substring scan
/// would either fail today or have to be loosened until it passed on the very
/// call it exists to forbid. This tree is also full of `"https://…"` literals, so
/// string handling is not optional either -- without it a URL would blank the
/// rest of its line and could hide a call.
///
/// Handles `//`, `/* */` and both quote characters. Raw string literals
/// (`R"(…)"`) are not recognised; no file under `src/suno` uses one, and the
/// failure mode of missing them is a *reported* offender, never a missed call.
std::string blankCommentsAndStrings(const std::string& source)
{
    enum class State { Code, Line, Block, Quoted };

    std::string out;
    out.reserve(source.size());
    State state = State::Code;
    char quote = '\0';

    for (std::size_t i = 0; i < source.size(); ++i) {
        const char c = source[i];
        const char next = (i + 1 < source.size()) ? source[i + 1] : '\0';
        switch (state) {
        case State::Code:
            if (c == '/' && next == '/') {
                state = State::Line;
                out += "  ";
                ++i;
            } else if (c == '/' && next == '*') {
                state = State::Block;
                out += "  ";
                ++i;
            } else if (c == '"' || c == '\'') {
                state = State::Quoted;
                quote = c;
                out += ' ';
            } else {
                out += c;
            }
            break;
        case State::Line:
            if (c == '\n') {
                state = State::Code;
                out += c;
            } else {
                out += ' ';
            }
            break;
        case State::Block:
            if (c == '*' && next == '/') {
                state = State::Code;
                out += "  ";
                ++i;
            } else {
                out += (c == '\n' ? '\n' : ' ');
            }
            break;
        case State::Quoted:
            if (c == '\\' && next != '\0') {
                out += "  ";
                ++i;
            } else if (c == quote) {
                state = State::Code;
                quote = '\0';
                out += ' ';
            } else {
                out += (c == '\n' ? '\n' : ' ');
            }
            break;
        }
    }
    return out;
}

/// 1-based line numbers of every occurrence of `needle` in code (comments and
/// string literals excluded). `std::nullopt` means the file could not be read,
/// which callers treat as a failure rather than as a pass.
std::optional<std::vector<int>> codeOccurrences(const fs::path& file,
                                                const std::string& needle)
{
    std::ifstream stream(file, std::ios::binary);
    if (!stream) {
        return std::nullopt;
    }
    const std::string raw((std::istreambuf_iterator<char>(stream)),
                          std::istreambuf_iterator<char>());
    const std::string code = blankCommentsAndStrings(raw);

    std::vector<int> lines;
    int line = 1;
    for (std::size_t i = 0; i + needle.size() <= code.size();) {
        const char c = code[i];
        if (c == '\n') {
            ++line;
        }
        if (code.compare(i, needle.size(), needle) == 0) {
            lines.push_back(line);
            i += needle.size();
        } else {
            ++i;
        }
    }
    return lines;
}

/// Space-separated `file:line` list, for a QVERIFY2 description.
std::string joinLocations(const std::vector<std::string>& locations)
{
    std::string joined;
    for (const std::string& location : locations) {
        joined += location;
        joined += ' ';
    }
    return joined;
}

/// Repository root. Duplicated from tests/unit/core/test_Version.cpp rather than
/// shared: it is twenty lines of path discovery, there is no test-helper TU in
/// this tree to put it in, and a helper library for one function would be a
/// larger change than the copy.
std::optional<fs::path> repoRoot()
{
#ifdef CHADVIS_SOURCE_DIR
    const fs::path configured{CHADVIS_SOURCE_DIR};
    if (fs::exists(configured / "src" / "suno")) {
        return configured;
    }
#endif
    fs::path dir = fs::current_path();
    for (int depth = 0; depth < 6; ++depth) {
        if (fs::exists(dir / "src" / "suno")) {
            return dir;
        }
        if (!dir.has_parent_path() || dir.parent_path() == dir) {
            break;
        }
        dir = dir.parent_path();
    }
    return std::nullopt;
}

// ─────────────────────────────────────────────────────────────

class TestBoundedBody : public QObject
{
    Q_OBJECT

private slots:
    // ── 1. the hazard this design exists to prevent ─────────────────────────

    void aPumpedReplyReadsEmptyWithReadAllSoTheAccessorIsMandatory()
    {
        // THE regression this whole change guards, asserted rather than
        // described. SunoClient arms every reply it issues with a readyRead-driven
        // BodyReader, so by the time a handler runs the bytes have moved into that
        // reader and `reply->readAll()` has nothing left to give. An empty
        // QByteArray handed to QJsonDocument::fromJson() is a parse error, which
        // is indistinguishable from a server that sent nothing -- so a call site
        // written the old way would silently lose every clip in the page and
        // report "invalid JSON".
        const QByteArray payload = lyricsBody();
        ReplyFarm farm;
        farm.setChooser([&payload](const QUrl&) { return payload; });
        SunoClient client(QStringLiteral("test-device"), nullptr, bearerBackend(),
                          farm.factory());
        awaitAuthenticated(client);

        bool handlerRan = false;
        QByteArray whatReadAllReturned;
        client.enqueueAuthenticatedRequest(
                QStringLiteral("/gen/clip-1/aligned_lyrics/v2/"), "GET", {},
                [&](QNetworkReply* reply) {
                    handlerRan = true;
                    whatReadAllReturned = reply->readAll();
                });

        QTRY_VERIFY_WITH_TIMEOUT(farm.issued() >= 1, 4000);
        feedInSlices(farm.at(0), static_cast<qint64>(payload.size()));
        farm.at(0).finish(200);

        QTRY_VERIFY_WITH_TIMEOUT(handlerRan, 1000);
        // Exactly empty -- "isEmpty()" alone would also pass on a nullptr reply.
        QCOMPARE(whatReadAllReturned.size(), qsizetype{0});
        // And the bytes really did go somewhere: they are in the reader, not lost.
        QCOMPARE(farm.at(0).consumed(), static_cast<qint64>(payload.size()));
        QCOMPARE(client.trackedReaderCount(), static_cast<qint64>(0));
        QCOMPARE(client.carriedFailureCount(), static_cast<qint64>(0));
    }

    void theAccessorReturnsEveryByteAndNotAPrefix()
    {
        // Requirement 2, at the byte level. A "non-empty body" assertion would
        // pass on a truncated read, so this compares the whole QByteArray and the
        // length exactly.
        const QByteArray payload = lyricsBody();
        ReplyFarm farm;
        farm.setChooser([&payload](const QUrl&) { return payload; });
        SunoClient client(QStringLiteral("test-device"), nullptr, bearerBackend(),
                          farm.factory());
        awaitAuthenticated(client);

        std::expected<QByteArray, QString> first;
        std::expected<QByteArray, QString> second;
        client.enqueueAuthenticatedRequest(
                QStringLiteral("/gen/clip-1/aligned_lyrics/v2/"), "GET", {},
                [&](QNetworkReply* reply) {
                    first = client.readTrackedBody(reply);
                    // Readable twice: a second caller must not be handed a
                    // different answer than the first, so no call site ever needs
                    // a second readAll() to make progress.
                    second = client.readTrackedBody(reply);
                });

        QTRY_VERIFY_WITH_TIMEOUT(farm.issued() >= 1, 4000);
        feedInSlices(farm.at(0), static_cast<qint64>(payload.size()));
        farm.at(0).finish(200);

        QTRY_VERIFY_WITH_TIMEOUT(first.has_value(), 1000);
        QVERIFY(first.has_value());
        QVERIFY(second.has_value());
        QCOMPARE(*first, payload);
        QCOMPARE(static_cast<qint64>(first->size()), static_cast<qint64>(payload.size()));
        QCOMPARE(*second, payload);
        QCOMPARE(static_cast<qint64>(second->size()), static_cast<qint64>(payload.size()));
        QCOMPARE(client.trackedReaderCount(), static_cast<qint64>(0));
    }

    // ── 2. the cap boundary ────────────────────────────────────────────────

    void theOverflowingByteIsDetectedButNeverStored()
    {
        // The internal buffer bound, asserted on the buffer itself -- the one
        // place `body()` is observable. `consumed()` elsewhere only proves the
        // device was asked for cap+1 bytes; this proves cap of them were kept.
        constexpr qint64 kSmallCap = 8;
        QByteArray payload(static_cast<qsizetype>(kSmallCap * 512), 'x');
        QBuffer buffer(&payload);
        QVERIFY(buffer.open(QIODevice::ReadOnly));

        auto reader = http::makeBodyReader(http::RequestClass::JsonApi, kSmallCap);
        QVERIFY(reader.has_value());
        const auto drained = reader->pump(buffer);

        QVERIFY(!drained.has_value());
        QCOMPARE(drained.error().error, http::PolicyError::ResponseTooLarge);
        QCOMPARE(drained.error().limitBytes, kSmallCap);
        // The minimum observed, never a total: the reader stopped, so the real
        // size is unknown and reporting 4096 would be a lie.
        QCOMPARE(drained.error().observedBytes, kSmallCap + 1);
        QCOMPARE(static_cast<qint64>(reader->body().size()), kSmallCap);
        QCOMPARE(reader->body(), payload.left(static_cast<qsizetype>(kSmallCap)));
        QCOMPARE(buffer.pos(), static_cast<qint64>(kSmallCap + 1));
    }

    void aBodyExactlyAtTheCapIsAccepted()
    {
        // Strictly-greater is what makes this a boundary and not an off-by-one:
        // kCap is accepted, kCap+1 is not, and there is no third answer.
        ReplyFarm farm;
        farm.setChooser([](const QUrl&) { return bodyAtCap(); });
        SunoClient client(QStringLiteral("test-device"), nullptr, bearerBackend(),
                          farm.factory());
        awaitAuthenticated(client);

        std::expected<QByteArray, QString> body;
        client.enqueueAuthenticatedRequest(QStringLiteral("/session/"), "GET", {},
                                           [&](QNetworkReply* reply) {
                                               body = client.readTrackedBody(reply);
                                           });

        QTRY_VERIFY_WITH_TIMEOUT(farm.issued() >= 1, 4000);
        feedInSlices(farm.at(0), kCap);
        farm.at(0).finish(200);

        QTRY_VERIFY_WITH_TIMEOUT(body.has_value(), 1000);
        QVERIFY(body.has_value());
        QCOMPARE(static_cast<qint64>(body->size()), kCap);
        QCOMPARE(*body, bodyAtCap());
        QCOMPARE(farm.at(0).consumed(), kCap);
        QVERIFY(!farm.at(0).wasAborted());
    }

    void aHostileOriginIsCutOffOneBytePastTheCapAndAbortsMidStream()
    {
        // Requirement 3 and 4 together. The refusal has to happen *during* the
        // read and the transfer has to be stopped there -- not merely noticed
        // afterwards -- so the assertions are on bytes moved, not on a verdict.
        ReplyFarm farm;
        farm.setChooser([](const QUrl&) { return bodyOneByteOverCap(); });
        SunoClient client(QStringLiteral("test-device"), nullptr, bearerBackend(),
                          farm.factory());
        awaitAuthenticated(client);

        std::expected<QByteArray, QString> read;
        client.enqueueAuthenticatedRequest(QStringLiteral("/session/"), "GET", {},
                                           [&](QNetworkReply* reply) {
                                               read = client.readTrackedBody(reply);
                                           });

        QTRY_VERIFY_WITH_TIMEOUT(farm.issued() >= 1, 4000);
        // Exactly the cap first, in realistic slices: the reader must sit on the
        // cap and still accept it. Then one byte more.
        feedInSlices(farm.at(0), kCap);
        farm.at(0).feed(1);

        // Wait for the ABORT, not for a value. This call returns a refusal, so
        // `read` never gains a value and waiting for one could only ever time
        // out -- which is exactly what the first version of this test did.
        QTRY_VERIFY_WITH_TIMEOUT(farm.at(0).wasAborted(), 2000);

        // Mid-stream: the reply was aborted while it was still arriving.
        QVERIFY(farm.at(0).wasAborted());
        QCOMPARE(farm.at(0).error(), QNetworkReply::OperationCanceledError);
        // The buffer bound, observed from outside: AT MOST one byte past the cap
        // was ever pulled off the socket, so the remaining bytes never arrived.
        //
        // Two arrival shapes give two different-but-both-correct numbers, and
        // pinning one of them exactly was wrong. If the overflow byte arrives in
        // the same chunk as the last good one, the reader is mid-loop with
        // `remaining` > 0 and reads `remaining + 1` into its probe -- consumed
        // becomes kCap + 1. If it arrives in a LATER chunk, the reader is
        // already sitting on the cap, so the `remaining <= 0` arm refuses on
        // `bytesAvailable() > 0` WITHOUT reading, and consumed stays at kCap.
        // This test feeds it in slices, so it gets the second, stricter shape.
        // test_HttpPolicy covers the first. The invariant both satisfy is the
        // one that matters and is asserted exactly below.
        QVERIFY(farm.at(0).consumed() <= kCap + 1);
        QVERIFY2(farm.at(0).consumed() < bodyOneByteOverCap().size(),
                 "the whole body was drained before the overrun was noticed, so "
                 "the cap bounded nothing");
        QCOMPARE(static_cast<qint64>(bodyOneByteOverCap().size()), kCap + 1);

        // Requirement 6: the refusal is named.
        QVERIFY(!read.has_value());
        const QString reason = read.error();
        QVERIFY2(reason.contains(QStringLiteral("studio API")), qPrintable(reason));
        QVERIFY2(reason.contains(QStringLiteral("response cap")), qPrintable(reason));
        QVERIFY2(reason.contains(QString::number(kCap)), qPrintable(reason));
        QVERIFY2(reason.contains(QString::number(kCap + 1)), qPrintable(reason));
        QVERIFY2(reason.contains(QStringLiteral("at least"), Qt::CaseInsensitive),
                 qPrintable(reason));
        // And it is emphatically NOT the transport's own message, which is what a
        // user would have been shown had the body been read after the error check.
        QVERIFY2(!reason.contains(QStringLiteral("ancel"), Qt::CaseInsensitive),
                 qPrintable(reason));
        QCOMPARE(farm.at(0).errorString(), QStringLiteral("aborted"));

        // Requirement 4 on the abort path. The reader is gone the moment the
        // reply is untracked, so this can be asserted immediately.
        QCOMPARE(client.trackedReaderCount(), static_cast<qint64>(0));

        // No instantaneous assertion on carriedFailureCount() here, and that is a
        // correction rather than an omission. This test used to assert it was 0 at
        // this point, which is not a property of the code -- it is a property of
        // WHEN `finished` is delivered relative to the untrack scope guard and to
        // the handler's own read, and PumpingReply's abort() drives that ordering
        // itself. Asserting it made the test wrong for reasons no amount of
        // reading the production code could resolve, and two attempts to "fix" the
        // production side were both wrong.
        //
        // What matters is behavioural and is asserted below and in the leak test:
        // the reader is released, the diagnosis is delivered with the limit in it,
        // and it is delivered ONCE. The growth bound on carriedFailureCount() is
        // pinned separately, where it does not depend on delivery order.
        QCOMPARE(client.trackedReaderCount(), static_cast<qint64>(0));

        // A second ask about a reply whose body is gone gets a refusal, never a
        // body -- and it gets the GENERIC refusal, because the diagnosis has
        // already been delivered once. "Reported once" is the contract, so this is
        // asserted rather than left to the comment above: the breach is reported
        // by the handler that runs off the abort, and a repeat is the same string
        // twice for one event.
        const auto second = client.readTrackedBody(&farm.at(0));
        QVERIFY2(!second.has_value(), "a drained reply handed back a body");
        QVERIFY2(!second.error().contains(QStringLiteral("response cap")),
                 qPrintable(QStringLiteral("the cap diagnosis was delivered a second "
                                           "time, to a caller that had already had "
                                           "it: %1")
                                    .arg(second.error())));
        QVERIFY2(second.error().contains(QStringLiteral("SunoClient")),
                 qPrintable(second.error()));
    }

    // ── 3. every service's own error path ──────────────────────────────────

    void sunoClientReportsAnOversizedReplyByNameRatherThanAsACancelledOperation()
    {
        // The site at SunoClient.cpp:1226, through the real handler. Two
        // independent assertions: the user is told the limit, AND the fake result
        // never reaches the consumer.
        ReplyFarm farm;
        farm.setChooser([](const QUrl&) { return bodyOneByteOverCap(); });
        SunoClient client(QStringLiteral("test-device"), nullptr, bearerBackend(),
                          farm.factory());
        awaitAuthenticated(client);

        std::vector<std::string> errors;
        client.errorOccurred.connect([&errors](std::string message) {
            errors.push_back(std::move(message));
        });
        int lyricsDeliveries = 0;
        client.alignedLyricsFetched.connect(
                [&lyricsDeliveries](std::string, std::string) { ++lyricsDeliveries; });

        client.fetchAlignedLyrics("clip-1");
        QTRY_VERIFY_WITH_TIMEOUT(farm.issued() >= 1, 4000);
        feedInSlices(farm.at(0), kCap);
        farm.at(0).feed(1);

        QTRY_VERIFY_WITH_TIMEOUT(!errors.empty(), 1000);
        QCOMPARE(lyricsDeliveries, 0);
        const std::string reported = errors.back();
        QVERIFY2(reported.find("studio API") != std::string::npos, reported.c_str());
        QVERIFY2(reported.find("response cap") != std::string::npos, reported.c_str());
        QVERIFY2(reported.find(std::to_string(kCap)) != std::string::npos,
                 reported.c_str());
        QVERIFY2(reported.find(std::to_string(kCap + 1)) != std::string::npos,
                 reported.c_str());
        QVERIFY2(reported.find("ancel") == std::string::npos, reported.c_str());
        QCOMPARE(client.trackedReaderCount(), static_cast<qint64>(0));
    }

    void exploreNamesTheLimitRatherThanInvalidJson()
    {
        ReplyFarm farm;
        farm.setChooser([](const QUrl&) { return bodyOneByteOverCap(); });
        SunoClient client(QStringLiteral("test-device"), nullptr, bearerBackend(),
                          farm.factory());
        awaitAuthenticated(client);
        SunoExploreService explore(&client);
        QSignalSpy failures(&explore, &SunoExploreService::failed);
        QSignalSpy pages(&explore, &SunoExploreService::pageReady);

        explore.refresh();
        QTRY_VERIFY_WITH_TIMEOUT(farm.issued() >= 1, 4000);
        farm.at(0).finish(200);

        QTRY_COMPARE_WITH_TIMEOUT(failures.count(), 1, 2000);
        QCOMPARE(pages.count(), 0);
        const QString reason = failures.at(0).at(0).toString();
        QVERIFY2(reason.contains(QString::number(kCap)), qPrintable(reason));
        QVERIFY2(reason.contains(QStringLiteral("response cap")), qPrintable(reason));
        // The message this site used to produce. Asserting its absence is the
        // point: 'x' is not JSON, so if the breach went unnoticed the service would
        // say exactly this and the test would pass for the wrong reason.
        QVERIFY2(!reason.contains(QStringLiteral("invalid JSON")), qPrintable(reason));
        QVERIFY(explore.feeds().isEmpty());
        QVERIFY(!explore.isLoading());
        QCOMPARE(client.trackedReaderCount(), static_cast<qint64>(0));
    }

    void notificationsNameTheLimitRatherThanInvalidJson()
    {
        ReplyFarm farm;
        farm.setChooser([](const QUrl&) { return bodyOneByteOverCap(); });
        SunoClient client(QStringLiteral("test-device"), nullptr, bearerBackend(),
                          farm.factory());
        awaitAuthenticated(client);
        SunoNotificationService notifications(&client);
        QSignalSpy errors(&notifications, &SunoNotificationService::errorChanged);
        QSignalSpy changes(&notifications, &SunoNotificationService::notificationsChanged);

        notifications.refresh();
        QTRY_VERIFY_WITH_TIMEOUT(farm.issued() >= 2, 5000);
        for (PumpingReply* reply : farm.live()) {
            reply->finish(200);
        }

        QTRY_COMPARE_WITH_TIMEOUT(errors.count(), 1, 2000);
        QCOMPARE(changes.count(), 0);
        const QString reason = notifications.error();
        QVERIFY2(reason.contains(QString::number(kCap)), qPrintable(reason));
        QVERIFY2(reason.contains(QStringLiteral("response cap")), qPrintable(reason));
        QVERIFY2(!reason.contains(QStringLiteral("invalid JSON")), qPrintable(reason));
        QCOMPARE(notifications.unreadCount(), 0);
        QCOMPARE(client.trackedReaderCount(), static_cast<qint64>(0));
    }

    void theAccountManagerNamesTheLimitRatherThanAnEmptyEnvelope()
    {
        ReplyFarm farm;
        farm.setChooser([](const QUrl&) { return bodyOneByteOverCap(); });
        SunoClient client(QStringLiteral("test-device"), nullptr, bearerBackend(),
                          farm.factory());
        awaitAuthenticated(client);
        SunoAccountManager account(&client);
        QSignalSpy errors(&account, &SunoAccountManager::accountError);
        QSignalSpy ready(&account, &SunoAccountManager::accountInfoReady);

        account.refreshAll();
        QTRY_VERIFY_WITH_TIMEOUT(farm.issued() >= 2, 5000);
        for (PumpingReply* reply : farm.live()) {
            reply->finish(200);
        }

        QTRY_COMPARE_WITH_TIMEOUT(errors.count(), 2, 2000);
        QCOMPARE(ready.count(), 0);
        for (int i = 0; i < errors.count(); ++i) {
            const QString reason = errors.at(i).at(0).toString();
            QVERIFY2(reason.contains(QString::number(kCap)), qPrintable(reason));
            QVERIFY2(reason.contains(QStringLiteral("response cap")), qPrintable(reason));
            QVERIFY2(!reason.contains(QStringLiteral("no user object")),
                     qPrintable(reason));
        }
        QVERIFY(!account.user().has_value());
        QVERIFY(account.models().isEmpty());
        QCOMPARE(client.trackedReaderCount(), static_cast<qint64>(0));
    }

    // ── 4. the success paths, which is the guard on the migration ──────────

    void exploreStillParsesAValidPageThroughTheAccessor()
    {
        // The behavioural half of requirement 1's guard. If any Explore call site
        // went back to readAll(), this body would arrive empty and the test would
        // fail on `failed(QStringLiteral("Explore returned invalid JSON"))` rather
        // than needing a comment to say why.
        ReplyFarm farm;
        farm.setChooser([](const QUrl& url) { return validJsonFor(url); });
        SunoClient client(QStringLiteral("test-device"), nullptr, bearerBackend(),
                          farm.factory());
        awaitAuthenticated(client);
        SunoExploreService explore(&client);
        QSignalSpy failures(&explore, &SunoExploreService::failed);
        QSignalSpy pages(&explore, &SunoExploreService::pageReady);

        explore.refresh();

        // Complete the reply. PumpingReply delivers bytes only via feed() and
        // signals completion only via finish(), so a test that calls refresh()
        // and then waits sees nothing arrive -- the handler never runs, so
        // neither `pages` nor `failures` moves. That is a fixture gap, not a
        // product failure, and it is invisible unless you know the double is
        // explicit.
        QTRY_VERIFY_WITH_TIMEOUT(farm.issued() >= 1, 4000);
        // One request per refresh for this service (SunoExploreService.cpp:229).
        QVERIFY(drainReplies(farm, 1));

        QTRY_COMPARE_WITH_TIMEOUT(pages.count(), 1, 5000);
        QCOMPARE(failures.count(), 0);
        QCOMPARE(explore.feeds().size(), 1);
        QCOMPARE(explore.feeds().at(0).id, QStringLiteral("feed-1"));
        QCOMPARE(explore.feeds().at(0).label, QStringLiteral("Trending"));
        QCOMPARE(explore.feeds().at(0).clips.size(), 1);
        QCOMPARE(QString::fromStdString(explore.feeds().at(0).clips.at(0).id),
                 QStringLiteral("clip-1"));
        QCOMPARE(QString::fromStdString(explore.feeds().at(0).clips.at(0).title),
                 QStringLiteral("One"));
        QVERIFY(explore.hasMore());
        QVERIFY(!explore.isLoading());
        QCOMPARE(client.trackedReaderCount(), static_cast<qint64>(0));
    }

    void notificationsStillParseValidEnvelopesThroughTheAccessor()
    {
        ReplyFarm farm;
        farm.setChooser([](const QUrl& url) { return validJsonFor(url); });
        SunoClient client(QStringLiteral("test-device"), nullptr, bearerBackend(),
                          farm.factory());
        awaitAuthenticated(client);
        SunoNotificationService notifications(&client);
        QSignalSpy changes(&notifications,
                           &SunoNotificationService::notificationsChanged);

        notifications.refresh();

        // See exploreStillParsesAValidPageThroughTheAccessor: PumpingReply only
        // completes on finish(), so refresh() alone leaves both signals empty.
        QTRY_VERIFY_WITH_TIMEOUT(farm.issued() >= 1, 4000);
        // TWO requests, not one: refresh() sets refreshPending_ = 2 and enqueues
        // the list and the badge count (SunoNotificationService.cpp:248,260,269).
        // The second is not issued until the first is answered, so a drain that
        // does not wait for it leaves notificationsChanged -- which is what this
        // test asserts -- unable to fire, because the service is still waiting.
        QVERIFY(drainReplies(farm, 2));

        QTRY_COMPARE_WITH_TIMEOUT(changes.count(), 1, 6000);
        QVERIFY(notifications.error().isEmpty());
        QCOMPARE(notifications.notifications().size(), 1);
        // SunoNotificationService::Notification carries QString; the clip and
        // account structs carry std::string. Compared the way each is declared.
        QCOMPARE(notifications.notifications().at(0).id, QStringLiteral("n-1"));
        QCOMPARE(notifications.notifications().at(0).contentTitle,
                 QStringLiteral("A clip"));
        QCOMPARE(notifications.notifications().at(0).author.displayName,
                 QStringLiteral("Ada"));
        // Both replies were consumed: the badge count is what proves the second
        // one's body arrived.
        QCOMPARE(notifications.unreadCount(), 3);
        QVERIFY(!notifications.notifications().at(0).read);
        QVERIFY(!notifications.isLoading());
        QCOMPARE(client.trackedReaderCount(), static_cast<qint64>(0));
    }

    void theAccountManagerStillParsesValidEnvelopesThroughTheAccessor()
    {
        ReplyFarm farm;
        farm.setChooser([](const QUrl& url) { return validJsonFor(url); });
        SunoClient client(QStringLiteral("test-device"), nullptr, bearerBackend(),
                          farm.factory());
        awaitAuthenticated(client);
        SunoAccountManager account(&client);
        QSignalSpy errors(&account, &SunoAccountManager::accountError);
        QSignalSpy ready(&account, &SunoAccountManager::accountInfoReady);
        QSignalSpy billingReady(&account, &SunoAccountManager::billingInfoReady);

        account.refreshAll();

        // See exploreStillParsesAValidPageThroughTheAccessor: PumpingReply only
        // completes on finish(), so refreshAll() alone leaves every signal empty.
        QTRY_VERIFY_WITH_TIMEOUT(farm.issued() >= 2, 5000);
        // refreshAll() issues the session and the billing request
        // (SunoAccountManager.cpp:163,175) and neither is issued first.
        QVERIFY(drainReplies(farm, 2));

        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 6000);
        QTRY_COMPARE_WITH_TIMEOUT(billingReady.count(), 1, 6000);
        QCOMPARE(errors.count(), 0);
        QVERIFY(account.user().has_value());
        QCOMPARE(QString::fromStdString(account.user()->display_name),
                 QStringLiteral("Ada"));
        QCOMPARE(QString::fromStdString(account.user()->handle), QStringLiteral("ada"));
        QCOMPARE(account.models().size(), 1);
        QCOMPARE(QString::fromStdString(account.models().at(0).name),
                 QStringLiteral("chirp-v3.5"));
        QCOMPARE(account.models().at(0).can_use, true);
        QCOMPARE(account.models().at(0).max_lengths.prompt, std::int64_t{5000});
        QVERIFY(account.billing().has_value());
        QCOMPARE(account.billing()->credits, std::int64_t{12});
        QCOMPARE(client.trackedReaderCount(), static_cast<qint64>(0));
    }

    // ── 5. the leak ────────────────────────────────────────────────────────

    void aLongLivedClientHoldsOneReaderPerLiveReplyAndNoneAfterSignOut()
    {
        // Requirement 4 at its worst: two replies are in flight and neither ever
        // finishes. That is 32 MiB of retained body on a client that runs for
        // hours, and it is the reason a reader is keyed to the reply's lifetime
        // rather than to the handler's.
        ReplyFarm farm;
        farm.setChooser([](const QUrl&) { return lyricsBody(); });
        SunoClient client(QStringLiteral("test-device"), nullptr, bearerBackend(),
                          farm.factory());
        awaitAuthenticated(client);
        QCOMPARE(client.trackedReaderCount(), static_cast<qint64>(0));

        for (int i = 0; i < 2; ++i) {
            client.enqueueAuthenticatedRequest(QStringLiteral("/session/"), "GET", {},
                                               [](QNetworkReply* reply) {
                                                   reply->deleteLater();
                                               });
        }

        QTRY_VERIFY_WITH_TIMEOUT(farm.issued() >= 2, 6000);
        QCOMPARE(client.trackedReaderCount(), static_cast<qint64>(2));
        feedInSlices(farm.at(0), static_cast<qint64>(lyricsBody().size()));
        QCOMPARE(client.trackedReaderCount(), static_cast<qint64>(2));
        feedInSlices(farm.at(1), static_cast<qint64>(lyricsBody().size()));
        QCOMPARE(client.trackedReaderCount(), static_cast<qint64>(2));

        client.clearLocalCredentials();

        // Immediate, not "on the next untrack": sign-out disconnects every reply
        // it abandons, so there is no handler left to trigger one.
        QCOMPARE(client.trackedReaderCount(), static_cast<qint64>(0));
        QCOMPARE(client.carriedFailureCount(), static_cast<qint64>(0));
        QVERIFY(farm.at(0).wasAborted());
        QVERIFY(farm.at(1).wasAborted());
    }

    // ── 6. unknown replies ─────────────────────────────────────────────────

    void carriedFailuresAreBoundedByTheRepliesThatActuallyBreached() {
        // The ordering-independent version of the leak property. The instantaneous
        // count depends on when `finished` is delivered; the BOUND does not: there
        // can never be more carried entries than there were breaches, and a client
        // that has served a thousand good replies carries nothing at all.
        //
        // Asserting a bound rather than a count is what makes this robust. A count
        // of 0 after one breach is a claim about signal ordering; a count <= the
        // number of breaches is a claim about the code.
        ReplyFarm farm;
        farm.setChooser([](const QUrl&) { return bodyAtCap(); });
        SunoClient client(QStringLiteral("test-device"), nullptr, bearerBackend(),
                          farm.factory());
        awaitAuthenticated(client);

        constexpr int kGood = 5;
        for (int i = 0; i < kGood; ++i) {
            std::expected<QByteArray, QString> body;
            client.enqueueAuthenticatedRequest(QStringLiteral("/session/"), "GET", {},
                                               [&](QNetworkReply* reply) {
                                                   body = client.readTrackedBody(reply);
                                               });
            QTRY_VERIFY_WITH_TIMEOUT(farm.issued() >= static_cast<std::size_t>(i + 1), 6000);
            farm.at(static_cast<std::size_t>(i)).finish(200);
            QVERIFY(drainReplies(farm, static_cast<std::size_t>(i) + 1));
            QTRY_VERIFY_WITH_TIMEOUT(body.has_value(), 4000);
        }

        // Five healthy replies carried nothing. This is the assertion that would
        // catch a map that grows on every reply rather than only on a breach.
        QCOMPARE(client.trackedReaderCount(), static_cast<qint64>(0));
        QCOMPARE(client.carriedFailureCount(), static_cast<std::size_t>(0));

        // Now one breach, and the bound is one -- not more, however the abort and
        // the untrack happen to interleave.
        farm.setChooser([](const QUrl&) { return bodyOneByteOverCap(); });
        std::expected<QByteArray, QString> refused;
        client.enqueueAuthenticatedRequest(QStringLiteral("/session/"), "GET", {},
                                           [&](QNetworkReply* reply) {
                                               refused = client.readTrackedBody(reply);
                                           });
        QTRY_VERIFY_WITH_TIMEOUT(farm.issued() >= static_cast<std::size_t>(kGood + 1), 6000);
        auto& hostile = farm.at(static_cast<std::size_t>(kGood));
        feedInSlices(hostile, kCap);
        hostile.feed(1);
        QTRY_VERIFY_WITH_TIMEOUT(hostile.wasAborted(), 4000);

        QVERIFY(!refused.has_value());
        QVERIFY2(refused.error().contains(QString::number(kCap)), qPrintable(refused.error()));
        QCOMPARE(client.trackedReaderCount(), static_cast<qint64>(0));
        QVERIFY2(client.carriedFailureCount() <= 1,
                 "one breach produced more than one carried entry, so the map grows "
                 "per reply instead of per failure");
    }

    void anUnknownOrNullReplyIsANamedErrorAndNotACrash()
    {
        ReplyFarm farm;
        farm.setChooser([](const QUrl&) { return lyricsBody(); });
        SunoClient client(QStringLiteral("test-device"), nullptr, bearerBackend(),
                          farm.factory());
        awaitAuthenticated(client);

        const auto nullReply = client.readTrackedBody(nullptr);
        QVERIFY(!nullReply.has_value());
        QVERIFY(!nullReply.error().isEmpty());
        QVERIFY2(nullReply.error().contains(QStringLiteral("SunoClient")),
                 qPrintable(nullReply.error()));

        // A real reply this client never issued -- the shape a future call site
        // gets if it wires its own QNetworkAccessManager and hands the reply over.
        auto foreign = std::make_unique<PumpingReply>(
                QNetworkRequest{QUrl(QStringLiteral(
                        "https://studio-api-prod.suno.com/api/session/"))},
                std::string{"GET"}, lyricsBody());
        const auto untracked = client.readTrackedBody(foreign.get());
        QVERIFY(!untracked.has_value());
        QVERIFY2(untracked.error().contains(QStringLiteral("no bounded response body")),
                 qPrintable(untracked.error()));
        QVERIFY2(untracked.error().contains(QStringLiteral("SunoClient did not issue it")),
                 qPrintable(untracked.error()));
        // Nothing was leaked by asking.
        QCOMPARE(client.trackedReaderCount(), static_cast<qint64>(0));
        QCOMPARE(client.carriedFailureCount(), static_cast<qint64>(0));

        // And the foreign reply is untouched: a rejected accessor does not drain,
        // abort or consume a reply it does not own.
        QCOMPARE(foreign->consumed(), static_cast<qint64>(0));
        QVERIFY(!foreign->wasAborted());
    }

    // ── 7. the mechanical guard on the migration ────────────────────────────

    void noCallSiteReadsABodyDirectlyOffAPumpedReply()
    {
        // The guard that makes requirement 1 mechanical rather than a comment.
        //
        // Two ways to get this wrong, both of which look harmless in review:
        //  * `reply->readAll()` on a reply SunoClient issued -- empty, silent;
        //  * `http::readBodyBounded(*reply, ...)` on the same reply -- bounded,
        //    named, and STILL EMPTY, because SunoClient already drained it. The
        //    second is the more dangerous one, because it looks hardened.
        //
        // The allowlist below is exhaustive and each entry has a reason; a new
        // entry needs a new reason, not a new silence.
        const auto root = repoRoot();
        QVERIFY2(root.has_value(),
                 "could not locate the repository root (set CHADVIS_SOURCE_DIR for "
                 "the unit_tests target)");

        const fs::path suno = *root / "src" / "suno";
        QVERIFY2(fs::is_directory(suno), "src/suno is missing from the source tree");

        std::vector<std::string> readAllOffenders;
        std::vector<std::string> boundedOffenders;
        std::vector<std::string> unreadable;
        int filesScanned = 0;

        for (const fs::directory_entry& entry : fs::recursive_directory_iterator(suno)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            const std::string ext = entry.path().extension().string();
            if (ext != ".cpp" && ext != ".hpp") {
                continue;
            }
            const std::string rel =
                    fs::relative(entry.path(), *root).generic_string();
            ++filesScanned;

            // HttpPolicy declares and defines the two bounded readers; this is not
            // a call site.
            if (rel.find("HttpPolicy.") != std::string::npos) {
                continue;
            }
            // DownloadQueue owns its replies, reads them per readyRead chunk, and
            // checks maxBytesPerItem_ before every write (DownloadQueue.cpp:556).
            if (rel.find("DownloadQueue.cpp") != std::string::npos) {
                continue;
            }
            // CredentialStore reads a local secrets file, not a network reply.
            if (rel.find("CredentialStore.cpp") != std::string::npos) {
                continue;
            }

            const auto readAll = codeOccurrences(entry.path(), "readAll(");
            if (!readAll) {
                unreadable.push_back(rel);
                continue;
            }
            for (const int line : *readAll) {
                readAllOffenders.push_back(rel + ":" + std::to_string(line));
            }

            const auto bounded = codeOccurrences(entry.path(), "readBodyBounded(");
            if (!bounded) {
                unreadable.push_back(rel);
                continue;
            }
            for (const int line : *bounded) {
                boundedOffenders.push_back(rel + ":" + std::to_string(line));
            }
        }

        // A floor, so a directory walk that quietly matched nothing cannot pass
        // this test by finding no offenders. 52 .cpp/.hpp files live under
        // src/suno today; 40 leaves room to grow and still fails on a walk that
        // did not happen.
        const QString scanned = QStringLiteral("the guard scanned %1 files under src/suno")
                                        .arg(filesScanned);
        QVERIFY2(filesScanned >= 40, qPrintable(scanned));
        QVERIFY2(unreadable.empty(),
                 qPrintable(QStringLiteral("could not read: %1")
                                    .arg(QString::fromStdString(joinLocations(unreadable)))));
        QVERIFY2(readAllOffenders.empty(),
                 qPrintable(QStringLiteral(
                         "reply->readAll() on a reply SunoClient issued returns an empty "
                         "QByteArray, which is indistinguishable from an empty server "
                         "response; use SunoClient::readTrackedBody() instead. Offenders: %1")
                                 .arg(QString::fromStdString(joinLocations(readAllOffenders)))));
        QVERIFY2(boundedOffenders.empty(),
                 qPrintable(QStringLiteral(
                         "http::readBodyBounded(*reply, ...) on a reply SunoClient issued also "
                         "returns EMPTY, because SunoClient already drained it into its own "
                         "readyRead-driven reader; use SunoClient::readTrackedBody(). "
                         "Offenders: %1")
                                 .arg(QString::fromStdString(joinLocations(boundedOffenders)))));
    }

    void everyMigratedCallSiteGoesThroughTheSharedAccessor()
    {
        // The positive half of the previous test. Scoped to the four files this
        // change touched rather than to "no readAll anywhere", because an
        // exemption list only records what was excused; this records what was done.
        const auto root = repoRoot();
        QVERIFY2(root.has_value(), "could not locate the repository root");

        static const char* const kMigrated[] = {
            "src/suno/SunoClient.cpp",
            "src/suno/SunoExploreService.cpp",
            "src/suno/SunoNotificationService.cpp",
            "src/suno/SunoAccountManager.cpp",
        };
        for (const char* relative : kMigrated) {
            const fs::path file = *root / relative;
            const auto sites = codeOccurrences(file, "readTrackedBody(");
            QVERIFY2(sites.has_value(), qPrintable(QStringLiteral("could not read %1")
                                                           .arg(relative)));
            QVERIFY2(!sites->empty(),
                     qPrintable(QStringLiteral("%1 no longer reads bodies through "
                                               "SunoClient::readTrackedBody()")
                                        .arg(relative)));
        }
    }
};

#include "test_BoundedBody.moc"

int runTestBoundedBody(int argc, char** argv)
{
    TestBoundedBody test;
    return QTest::qExec(&test, argc, argv);
}