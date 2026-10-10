#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "suno/DownloadQueue.hpp"

#include <QDateTime>
#include <QFile>
#include <QNetworkRequest>
#include <QTimeZone>
#include <algorithm>
#include <filesystem>
#include <memory>
#include <random>
#include <vector>

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <unistd.h>
#endif

using namespace vc::suno;

namespace fs = std::filesystem;

namespace {

/// Scriptable QNetworkReply stand-in. Tests drive it directly (succeed/fail);
/// no packets ever leave the machine.
class FakeReply : public QNetworkReply {
public:
    explicit FakeReply(const QNetworkRequest& request) {
        setRequest(request);
        setUrl(request.url());
        setOperation(QNetworkAccessManager::GetOperation);
        setOpenMode(ReadOnly | Unbuffered);
    }

    void abort() override {
        if (finishing_) return;
        finishing_ = true;
        setError(QNetworkReply::OperationCanceledError, QStringLiteral("aborted"));
        emit finished();
    }

    qint64 bytesAvailable() const override {
        return (payload_.size() - offset_) + QNetworkReply::bytesAvailable();
    }

    qint64 readData(char* data, qint64 maxLen) override {
        if (offset_ >= payload_.size()) return 0;
        const qint64 n = qMin(maxLen, static_cast<qint64>(payload_.size()) - offset_);
        std::memcpy(data, payload_.constData() + offset_, static_cast<size_t>(n));
        offset_ += n;
        return n;
    }

    // ── test drivers ──
    void succeed(QByteArray body, int httpStatus = 200, bool acceptRanges = false) {
        openStream(std::move(body), acceptRanges, httpStatus);
        finishing_ = true;
        emit finished();
    }

    void fail(QNetworkReply::NetworkError err, int httpStatus = 0, bool acceptRanges = false,
              QByteArray body = {}) {
        setHeaders(httpStatus, acceptRanges);
        setError(err, QStringLiteral("scripted failure"));
        payload_ = std::move(body);
        offset_ = 0;
        if (!payload_.isEmpty()) emit readyRead();
        finishing_ = true;
        emit finished();
    }

    /// Fail with an explicit Retry-After, so the hint path is reachable.
    void failWithRetryAfter(QNetworkReply::NetworkError err, int httpStatus, QByteArray hint) {
        setHeaders(httpStatus, false);
        setRawHeader("Retry-After", hint);
        setError(err, QStringLiteral("scripted failure"));
        payload_.clear();
        offset_ = 0;
        finishing_ = true;
        emit finished();
    }

    /// Deliver bytes mid-stream and keep the reply open (caller may then
    /// fail() it or trigger abort() via queue cancellation).
    void openStream(QByteArray prefix, bool acceptRanges = false, int httpStatus = 200) {
        setHeaders(httpStatus, acceptRanges);
        payload_ = std::move(prefix);
        offset_ = 0;
        if (!payload_.isEmpty()) emit readyRead();
        emit downloadProgress(payload_.size(), payload_.size());
    }

private:
    void setHeaders(int httpStatus, bool acceptRanges) {
        setOpenMode(ReadOnly | Unbuffered);
        if (httpStatus > 0) {
            setAttribute(QNetworkRequest::HttpStatusCodeAttribute, httpStatus);
        }
        if (acceptRanges) setRawHeader("Accept-Ranges", "bytes");
    }

    QByteArray payload_;
    qint64 offset_ = 0;
    bool finishing_ = false;
};

/// Reply factory capturing every request and handing back paired fake replies.
struct FakeServer {
    std::vector<QNetworkRequest> requests;
    std::vector<FakeReply*> replies;

    ReplyFactory factory() {
        return [this](const QNetworkRequest& request) {
            requests.push_back(request);
            auto* reply = new FakeReply(request);
            replies.push_back(reply);
            return reply;
        };
    }
};

/// Sink whose write result is budget-limited. A short or refused write is not
/// reachable any other way in a unit test -- filling a real filesystem is not a
/// thing a test may do, and /dev/full cannot be opened WriteOnly|NewOnly because
/// O_EXCL refuses the pre-existing device node. `mirror` outlives the device so
/// the test can see exactly how many bytes landed after the queue destroyed it.
class BudgetedSink : public QIODevice {
public:
    /// budget >= 0: accept at most that many bytes, then refuse.
    /// budget < 0: refuse every write outright (-1, i.e. ENOSPC-shaped).
    BudgetedSink(const qint64 budget, QByteArray* mirror) : budget_(budget), mirror_(mirror) {
        setOpenMode(QIODevice::WriteOnly);
    }

    qint64 accepted() const { return accepted_; }

protected:
    qint64 writeData(const char* data, qint64 maxLen) override {
        const qint64 room = budget_ < 0 ? 0 : budget_ - accepted_;
        if (room <= 0) {
            setErrorString(QStringLiteral("No space left on device"));
            return -1;
        }
        const qint64 n = qMin(maxLen, room);
        if (n < maxLen) setErrorString(QStringLiteral("No space left on device"));
        mirror_->append(data, static_cast<qsizetype>(n));
        accepted_ += n;
        return n;
    }

    qint64 readData(char*, qint64) override { return -1; }

private:
    qint64 budget_;
    QByteArray* mirror_;
    qint64 accepted_{0};
};

bool makeSymlink(const QString& target, const QString& link) {
#ifdef Q_OS_WIN
    return CreateSymbolicLinkW(reinterpret_cast<LPCWSTR>(link.utf16()),
                               reinterpret_cast<LPCWSTR>(target.utf16()),
                               SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE) != FALSE;
#else
    return ::symlink(target.toLocal8Bit().constData(), link.toLocal8Bit().constData()) == 0;
#endif
}

/// Write a file whole, for exact-byte setup assertions.
qint64 writeFile(const QString& path, const QByteArray& body) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return -1;
    return file.write(body);
}

/// Read a file whole, for exact-content assertions.
QByteArray readAll(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}

} // namespace

class TestDownloadQueue : public QObject {
    Q_OBJECT

private slots:
    // Pure decision logic — no event loop needed.

    void classifyFailureTable() {
        using NE = QNetworkReply::NetworkError;
        QCOMPARE(classifyFailure(NE::NoError, 0), FailureKind::None);
        QCOMPARE(classifyFailure(NE::OperationCanceledError, 0), FailureKind::Cancelled);

        // HTTP status dominates when present.
        QCOMPARE(classifyFailure(NE::ProtocolFailure, 500), FailureKind::Retryable);
        QCOMPARE(classifyFailure(NE::ProtocolFailure, 503), FailureKind::Retryable);
        QCOMPARE(classifyFailure(NE::ProtocolFailure, 404), FailureKind::Permanent);
        QCOMPARE(classifyFailure(NE::ProtocolFailure, 403), FailureKind::Permanent);
        QCOMPARE(classifyFailure(NE::ProtocolFailure, 408), FailureKind::Retryable);
        QCOMPARE(classifyFailure(NE::ProtocolFailure, 429), FailureKind::Retryable);

        // Transport errors without a status are retryable by default...
        QCOMPARE(classifyFailure(NE::TimeoutError, 0), FailureKind::Retryable);
        QCOMPARE(classifyFailure(NE::ConnectionRefusedError, 0), FailureKind::Retryable);
        // ...except auth/permission denials.
        QCOMPARE(classifyFailure(NE::AuthenticationRequiredError, 0), FailureKind::Permanent);
        QCOMPARE(classifyFailure(NE::ContentAccessDenied, 0), FailureKind::Permanent);
        QCOMPARE(classifyFailure(NE::NoError, 302), FailureKind::Permanent);
    }

    void backoffLadderAndJitter() {
        QCOMPARE(backoffBaseMs(0), qint64{1000});
        QCOMPARE(backoffBaseMs(1), qint64{4000});
        QCOMPARE(backoffBaseMs(2), qint64{16000});
        QCOMPARE(backoffBaseMs(9), qint64{16000}); // capped
        QCOMPARE(backoffBaseMs(-3), qint64{1000}); // clamped

        std::mt19937 rng{42};
        for (const int attempt : {0, 1, 2}) {
            const auto base = backoffBaseMs(attempt);
            for (int i = 0; i < 200; ++i) {
                const auto delay = backoffWithJitterMs(attempt, rng);
                QVERIFY2(delay >= base - base / 5 && delay <= base + base / 5,
                         qPrintable(QStringLiteral("attempt %1 delay %2").arg(attempt).arg(delay)));
            }
        }
    }

    // Queue behaviour through the injected-factory seam.

    void successWritesFileAtomically() {
        FakeServer server;
        DownloadQueue queue(server.factory());
        QTemporaryDir dir;

        const QByteArray body = "chadvis audio bytes";
        QVERIFY(queue.enqueue("clip-1", "https://fake.cdn/clip-1.mp3",
                              dir.filePath("song.mp3").toStdString()));
        QCOMPARE(static_cast<int>(server.replies.size()), 1);

        QSignalSpy idle(&queue, &DownloadQueue::queueIdle);
        server.replies[0]->succeed(body);
        QTest::qWait(10); // drain deleteLater

        QFile out(dir.filePath("song.mp3"));
        QVERIFY(out.open(QIODevice::ReadOnly));
        QCOMPARE(out.readAll(), body);
        QVERIFY(!QFile::exists(dir.filePath("song.mp3.part"))); // .part renamed away
        QCOMPARE(idle.count(), 1);
    }

    void redirectsAreNotFollowed() {
        FakeServer server;
        DownloadQueue queue(server.factory());
        QTemporaryDir dir;

        QVERIFY(queue.enqueue("clip-redirect", "https://fake.cdn/redirect.mp3",
                              dir.filePath("redirect.mp3").toStdString()));
        QCOMPARE(server.requests[0].attribute(QNetworkRequest::RedirectPolicyAttribute).toInt(),
                 static_cast<int>(QNetworkRequest::ManualRedirectPolicy));

        server.replies[0]->succeed({}, 302);
        QTest::qWait(50);
        QCOMPARE(static_cast<int>(server.requests.size()), 1);
        QVERIFY(queue.isEmpty());
    }

    void retryableFailureRetriesWithBackoff() {
        FakeServer server;
        DownloadQueue queue(server.factory());
        QTemporaryDir dir;

        const QByteArray body = "second time is the charm";
        QVERIFY(queue.enqueue("clip-r", "https://fake.cdn/r.mp3",
                              dir.filePath("r.mp3").toStdString()));
        server.replies[0]->fail(QNetworkReply::TimeoutError);

        QTRY_COMPARE(static_cast<int>(server.requests.size()), 2); // waits out ~1s backoff
        server.replies[1]->succeed(body);
        QTest::qWait(10);

        QFile out(dir.filePath("r.mp3"));
        QVERIFY(out.open(QIODevice::ReadOnly));
        QCOMPARE(out.readAll(), body);
        QCOMPARE(queue.activeCount(), 0);
        QCOMPARE(queue.queuedCount(), 0);
    }

    void permanentFailureDoesNotRetry() {
        FakeServer server;
        DownloadQueue queue(server.factory());
        QTemporaryDir dir;

        std::vector<int> states;
        QObject::connect(&queue, &DownloadQueue::itemStateChanged,
                         [&](const QString&, int state, int) { states.push_back(state); });

        QVERIFY(queue.enqueue("clip-404", "https://fake.cdn/nope.mp3",
                              dir.filePath("n.mp3").toStdString()));
        server.replies[0]->fail(QNetworkReply::ContentNotFoundError, 404);
        QTest::qWait(150); // generous window: nothing may come back

        QCOMPARE(static_cast<int>(server.requests.size()), 1); // single attempt only
        QVERIFY(!states.empty());
        QCOMPARE(states.back(), static_cast<int>(DownloadState::FailedPermanent));
        QVERIFY(queue.isEmpty());
    }

    void exhaustedRetriesEndFailedRetryable() {
        FakeServer server;
        DownloadQueue queue(server.factory());
        QTemporaryDir dir;

        QVERIFY(queue.enqueue("clip-x", "https://fake.cdn/x.mp3",
                              dir.filePath("x.mp3").toStdString()));

        int lastState = -1;
        QObject::connect(&queue, &DownloadQueue::itemStateChanged,
                         [&](const QString&, int state, int) { lastState = state; });

        // Attempts 1..3 all timeout; the queue must stop after three.
        for (int i = 0; i < 3; ++i) {
            QTRY_COMPARE(static_cast<int>(server.replies.size()), i + 1);
            server.replies[i]->fail(QNetworkReply::TimeoutError);
        }
        QTest::qWait(50);
        QCOMPARE(lastState, static_cast<int>(DownloadState::FailedRetryable));
        QCOMPARE(static_cast<int>(server.replies.size()), 3);
        QVERIFY(queue.isEmpty());
    }

    void retryRestartsWithoutRange() {
        FakeServer server;
        DownloadQueue queue(server.factory());
        QTemporaryDir dir;

        const QByteArray first = "halfway";
        const QByteArray full = first + "-and-done";
        QVERIFY(queue.enqueue("clip-retry", "https://fake.cdn/retry.mp3",
                              dir.filePath("retry.mp3").toStdString()));

        server.replies[0]->openStream(first, /*acceptRanges*/ true);
        server.replies[0]->fail(QNetworkReply::TimeoutError);
        QVERIFY(!QFile::exists(dir.filePath("retry.mp3.part")));

        QTRY_COMPARE(static_cast<int>(server.requests.size()), 2);
        QVERIFY(server.requests[1].rawHeader("Range").isEmpty());
        server.replies[1]->succeed(full);
        QTest::qWait(10);

        QFile out(dir.filePath("retry.mp3"));
        QVERIFY(out.open(QIODevice::ReadOnly));
        QCOMPARE(out.readAll(), full);
    }

    void concurrencyIsBoundedFifo() {
        FakeServer server;
        DownloadQueue queue(server.factory());
        queue.setMaxConcurrent(2);
        QTemporaryDir dir;

        for (int i = 0; i < 4; ++i) {
            QVERIFY(queue.enqueue(QString("fifo-%1").arg(i).toStdString(),
                                  QStringLiteral("https://fake.cdn/%1").arg(i).toStdString(),
                                  dir.filePath(QString("f%1.bin").arg(i)).toStdString()));
        }
        QCOMPARE(queue.activeCount(), 2);
        QCOMPARE(queue.queuedCount(), 2);
        QCOMPARE(server.requests[0].url().toString(), QStringLiteral("https://fake.cdn/0"));
        QCOMPARE(server.requests[1].url().toString(), QStringLiteral("https://fake.cdn/1"));

        // Completing one frees a slot; the next FIFO item starts immediately.
        server.replies[0]->succeed("a");
        QCOMPARE(queue.activeCount(), 2);
        QCOMPARE(static_cast<int>(server.requests.size()), 3);
        QCOMPARE(server.requests[2].url().toString(), QStringLiteral("https://fake.cdn/2"));
        QTest::qWait(10);
    }

    void cancelQueuedItemEmitsCancelled() {
        FakeServer server;
        DownloadQueue queue(server.factory());
        queue.setMaxConcurrent(1);
        QTemporaryDir dir;

        for (int i = 0; i < 3; ++i) {
            QVERIFY(queue.enqueue(QString("c-%1").arg(i).toStdString(),
                                  QStringLiteral("https://fake.cdn/c%1").arg(i).toStdString(),
                                  dir.filePath(QString("c%1.bin").arg(i)).toStdString()));
        }

        std::vector<int> states;
        QObject::connect(&queue, &DownloadQueue::itemStateChanged,
                         [&](const QString& id, int state, int) {
                             if (id == QLatin1String("c-2")) states.push_back(state);
                         });

        QVERIFY(queue.cancel("c-2")); // still queued
        QCOMPARE(states, std::vector<int>{static_cast<int>(DownloadState::Cancelled)});
        QCOMPARE(queue.queuedCount(), 1);

        // Unknown / terminal ids are graceful no-ops.
        QVERIFY(!queue.cancel("never-existed"));
        QVERIFY(!queue.cancel("c-2"));
    }

    void cancelInFlightAbortsAndCleansPart() {
        FakeServer server;
        DownloadQueue queue(server.factory());
        QTemporaryDir dir;

        QVERIFY(queue.enqueue("live", "https://fake.cdn/live.mp3",
                              dir.filePath("live.mp3").toStdString()));

        std::vector<int> states;
        QObject::connect(&queue, &DownloadQueue::itemStateChanged,
                         [&](const QString&, int state, int) { states.push_back(state); });

        server.replies[0]->openStream("partial");
        QVERIFY(queue.cancel("live")); // triggers abort() -> finished()
        QTest::qWait(10);

        QVERIFY(!states.empty());
        QCOMPARE(states.back(), static_cast<int>(DownloadState::Cancelled));
        QVERIFY(!QFile::exists(dir.filePath("live.mp3.part"))); // cleaned up
        QVERIFY(queue.isEmpty());
    }

    void duplicateEnqueueRejected() {
        FakeServer server;
        DownloadQueue queue(server.factory());
        QTemporaryDir dir;

        QVERIFY(queue.enqueue("dup", "https://fake.cdn/dup.mp3",
                              dir.filePath("dup.mp3").toStdString()));
        QVERIFY(!queue.enqueue("dup", "https://fake.cdn/dup.mp3",
                               dir.filePath("dup.mp3").toStdString()));
        QCOMPARE(queue.activeCount(), 1);
        QCOMPARE(queue.queuedCount(), 0);
    }

    // ── write accounting ─────────────────────────────────────────────────────
    // The defect being pinned: `item.partFile.write(chunk)` discarded its return
    // value, so an ENOSPC mid-download renamed a truncated file into place and
    // reported Completed. Nothing downstream can tell a short MP3 from a real
    // one -- it opens, it plays, and it stops.

    void sinkWritesAreClassifiedExactly() {
        QCOMPARE(classifySinkWrite(20, 20), SinkWrite::Complete);
        QCOMPARE(classifySinkWrite(21, 20), SinkWrite::Complete); // never written short
        QCOMPARE(classifySinkWrite(0, 0), SinkWrite::Complete);
        QCOMPARE(classifySinkWrite(19, 20), SinkWrite::Short);
        QCOMPARE(classifySinkWrite(0, 20), SinkWrite::Short);
        QCOMPARE(classifySinkWrite(-1, 20), SinkWrite::Failed);
        QCOMPARE(classifySinkWrite(-1, 0), SinkWrite::Failed);
    }

    void selfInflictedFailureMessagesNameTheClipAndTheNumber() {
        QCOMPARE(byteCapReason("clip-42", 100, 101),
                 std::string("clip-42 exceeded the per-item byte cap of 100 bytes (101 received)"));
        QCOMPARE(shortWriteReason("clip-42", 50, 20, 0, "No space left on device"),
                 std::string("clip-42 could not be written: 0 of 20 bytes at offset 50 "
                             "(No space left on device)"));
    }

    void aShortWriteNeverBecomesACompletedFile() {
        FakeServer server;
        QTemporaryDir dir;
        QByteArray sink; // outlives the device the queue destroys
        // Injected opener: state and payload are what this mechanism can prove.
        // `.part` on disk is asserted by the tests that use the real opener.
        DownloadQueue queue(server.factory(),
                            [&sink](const std::string&) -> std::unique_ptr<QIODevice> {
                                return std::make_unique<BudgetedSink>(5, &sink);
                            });

        std::vector<int> states;
        QObject::connect(&queue, &DownloadQueue::itemStateChanged,
                         [&](const QString&, int state, int) { states.push_back(state); });

        const QString dest = dir.filePath("short.mp3");
        QVERIFY(queue.enqueue("short", "https://fake.cdn/short.mp3", dest.toStdString()));

        // 12 bytes in one chunk; the sink keeps 5 and returns 5.
        server.replies[0]->openStream(QByteArray("0123456789ab"), false, 200);
        QTest::qWait(10);

        QCOMPARE(sink, QByteArray("01234")); // the short write really happened
        QVERIFY(!QFile::exists(dest));       // ... and did NOT become a file
        QCOMPARE(queue.activeCount(), 0);
        QCOMPARE(queue.waitingCount(), 1); // retryable: requeued, not completed
        QCOMPARE(states.back(), static_cast<int>(DownloadState::Queued));
        QVERIFY(std::none_of(states.begin(), states.end(), [](const int s) {
            return s == static_cast<int>(DownloadState::Completed);
        }));

        QVERIFY(queue.cancel("short")); // do not sit out the 1 s ladder
        QTest::qWait(10);
        QCOMPARE(states.back(), static_cast<int>(DownloadState::Cancelled));
        QVERIFY(!QFile::exists(dest));
    }

    void aRefusedWriteExhaustsRetriesAndLeavesNothingBehind() {
        FakeServer server;
        QTemporaryDir dir;
        QByteArray sink;
        DownloadQueue queue(server.factory(),
                            [&sink](const std::string&) -> std::unique_ptr<QIODevice> {
                                return std::make_unique<BudgetedSink>(-1, &sink); // always -1
                            });

        int lastState = -1;
        QObject::connect(&queue, &DownloadQueue::itemStateChanged,
                         [&](const QString&, int state, int) { lastState = state; });

        const QString dest = dir.filePath("full.mp3");
        QVERIFY(queue.enqueue("full", "https://fake.cdn/full.mp3", dest.toStdString()));

        for (int i = 0; i < 3; ++i) {
            QTRY_COMPARE(static_cast<int>(server.replies.size()), i + 1);
            server.replies[i]->openStream("payload", false, 200);
            QTest::qWait(10);
        }
        QTest::qWait(50);

        QCOMPARE(lastState, static_cast<int>(DownloadState::FailedRetryable));
        QCOMPARE(static_cast<int>(server.replies.size()), 3);
        QCOMPARE(sink, QByteArray()); // a refused write lands nothing at all
        QVERIFY(!QFile::exists(dest));
        QVERIFY(queue.isEmpty());
    }

    // ── byte cap ─────────────────────────────────────────────────────────────

    void theByteCapBoundaryIsExact() {
        constexpr qint64 kCap = 100;

        { // exactly at the cap: legal, completes
            FakeServer server;
            DownloadQueue queue(server.factory());
            queue.setMaxBytesPerItem(kCap);
            QTemporaryDir dir;

            std::vector<int> states;
            QObject::connect(&queue, &DownloadQueue::itemStateChanged,
                             [&](const QString&, int state, int) { states.push_back(state); });

            const QString dest = dir.filePath("atcap.mp3");
            QVERIFY(queue.enqueue("atcap", "https://fake.cdn/at.mp3", dest.toStdString()));
            QCOMPARE(queue.maxBytesPerItem(), kCap);

            const QByteArray body(kCap, 'x');
            QCOMPARE(static_cast<qint64>(body.size()), kCap);
            server.replies[0]->succeed(body);
            QTest::qWait(10);

            QCOMPARE(states.back(), static_cast<int>(DownloadState::Completed));
            QCOMPARE(readAll(dest), body);
            QVERIFY(!QFile::exists(dir.filePath("atcap.mp3.part")));
        }

        { // one byte over: permanent, named, no file
            FakeServer server;
            DownloadQueue queue(server.factory());
            queue.setMaxBytesPerItem(kCap);
            QTemporaryDir dir;

            std::vector<int> states;
            QObject::connect(&queue, &DownloadQueue::itemStateChanged,
                             [&](const QString&, int state, int) { states.push_back(state); });

            const QString dest = dir.filePath("over.mp3");
            QVERIFY(queue.enqueue("over", "https://fake.cdn/over.mp3", dest.toStdString()));

            const QByteArray body(kCap + 1, 'x');
            QCOMPARE(static_cast<qint64>(body.size()), kCap + 1);
            server.replies[0]->openStream(body, false, 200);
            QTest::qWait(10);

            QCOMPARE(states.back(), static_cast<int>(DownloadState::FailedPermanent));
            QCOMPARE(static_cast<int>(server.replies.size()), 1); // never retried
            QVERIFY(!QFile::exists(dest));
            QVERIFY(!QFile::exists(dir.filePath("over.mp3.part")));
            QVERIFY(queue.isEmpty());
        }
    }

    void aZeroCapDisablesTheCeiling() {
        FakeServer server;
        DownloadQueue queue(server.factory());
        queue.setMaxBytesPerItem(0);
        QTemporaryDir dir;

        QCOMPARE(queue.maxBytesPerItem(), qint64{0});
        const QString dest = dir.filePath("nocap.mp3");
        QVERIFY(queue.enqueue("nocap", "https://fake.cdn/nocap.mp3", dest.toStdString()));
        const QByteArray body(4096, 'y');
        server.replies[0]->succeed(body);
        QTest::qWait(10);
        QCOMPARE(readAll(dest), body);
    }

    // ── transfer timeout ─────────────────────────────────────────────────────

    void everyRequestCarriesTheStallDeadline() {
        FakeServer server;
        DownloadQueue queue(server.factory());
        queue.setMaxConcurrent(2);
        QTemporaryDir dir;

        QCOMPARE(DownloadQueue::kDefaultTransferTimeoutMs, 30'000);
        QCOMPARE(queue.transferTimeoutMs(), 30'000);

        QVERIFY(queue.enqueue("t1", "https://fake.cdn/t1", dir.filePath("t1").toStdString()));
        QVERIFY(queue.enqueue("t2", "https://fake.cdn/t2", dir.filePath("t2").toStdString()));

        // The assertion that stops the attribute being silently dropped later.
        QCOMPARE(server.requests[0].transferTimeout(),
                 static_cast<int>(DownloadQueue::kDefaultTransferTimeoutMs));
        QCOMPARE(server.requests[1].transferTimeout(),
                 static_cast<int>(DownloadQueue::kDefaultTransferTimeoutMs));
        QCOMPARE(server.requests[0].transferTimeout(), 30000);

        // A retry carries the configured value too, not just the first attempt.
        queue.setTransferTimeoutMs(90'000);
        QCOMPARE(queue.transferTimeoutMs(), 90'000);
        server.replies[0]->fail(QNetworkReply::ConnectionRefusedError);
        QTRY_COMPARE(static_cast<int>(server.requests.size()), 3);
        QCOMPARE(server.requests[2].transferTimeout(), 90'000);

        // Zero disables it, which is Qt's own "no timer" contract.
        queue.setTransferTimeoutMs(-1);
        QCOMPARE(queue.transferTimeoutMs(), 0);
    }

    // ── Retry-After ──────────────────────────────────────────────────────────

    void retryAfterIsReadInBothWireForms() {
        const std::int64_t base = 784111777; // Sun, 06 Nov 1994 08:49:37 GMT

        // delta-seconds
        QCOMPARE(parseRetryAfter(QByteArray("120"), base), std::optional<std::int64_t>{120});
        QCOMPARE(parseRetryAfter(QByteArray("  120  "), base), std::optional<std::int64_t>{120});
        QCOMPARE(parseRetryAfter(QByteArray("0"), base), std::optional<std::int64_t>{0});
        QCOMPARE(parseRetryAfter(QByteArray("600"), base), std::optional<std::int64_t>{600});

        // HTTP-date, measured against the same instant: 0 means "now".
        QCOMPARE(parseRetryAfter(QByteArray("Sun, 06 Nov 1994 08:49:37 GMT"), base),
                 std::optional<std::int64_t>{0});
        QCOMPARE(parseRetryAfter(QByteArray("Sun, 06 Nov 1994 08:50:37 GMT"), base),
                 std::optional<std::int64_t>{60});
        QCOMPARE(parseRetryAfter(QByteArray("Sun, 06 Nov 1994 07:49:37 GMT"), base),
                 std::optional<std::int64_t>{-3600});
        // "+0100" at 09:49:37 is the same instant; "-0100" at 08:49:37 is one hour later.
        QCOMPARE(parseRetryAfter(QByteArray("Sun, 06 Nov 1994 09:49:37 +0100"), base),
                 std::optional<std::int64_t>{0});
        QCOMPARE(parseRetryAfter(QByteArray("Sun, 06 Nov 1994 08:49:37 -0100"), base),
                 std::optional<std::int64_t>{3600});
        QCOMPARE(parseRetryAfter(QByteArray("Sun, 06 Nov 1994 08:49:37 UTC"), base),
                 std::optional<std::int64_t>{0});
        QCOMPARE(parseRetryAfter(QByteArray("Sun, 06 Nov 1994 08:49:37 gmt"), base),
                 std::optional<std::int64_t>{0});
        QCOMPARE(parseRetryAfter(QByteArray("Mon, 06 Nov 1994 08:49:37 GMT"), base),
                 std::optional<std::int64_t>{0}); // weekday is redundant; must be ignored
        QCOMPARE(parseRetryAfter(QByteArray("Sun, 6 Nov 1994 08:49:37 GMT"), base),
                 std::optional<std::int64_t>{0}); // lenient on width

        // Absolute ground truth: an all-numeric-offset date, no local-time input.
        QCOMPARE(parseRetryAfter(QByteArray("Thu, 01 Jan 2026 00:00:00 GMT"), 0),
                 std::optional<std::int64_t>{1767225600});

        // Malformed -> no hint at all, so the ladder wins. Never a guess.
        QVERIFY(!parseRetryAfter(QByteArray(), base).has_value());
        QVERIFY(!parseRetryAfter(QByteArray("   "), base).has_value());
        QVERIFY(!parseRetryAfter(QByteArray("soon"), base).has_value());
        QVERIFY(!parseRetryAfter(QByteArray("5s"), base).has_value()); // trailing garbage
        QVERIFY(!parseRetryAfter(QByteArray("-5"), base).has_value()); // delta-seconds is 1*DIGIT
        QVERIFY(!parseRetryAfter(QByteArray("99999999999999999999999"), base).has_value());
        QVERIFY(!parseRetryAfter(QByteArray("Sun 06 Nov 1994 08:49:37 GMT"), base).has_value());
        QVERIFY(!parseRetryAfter(QByteArray("Sun, 06 Nov 1994 08:49:37 PST"), base).has_value());
        QVERIFY(!parseRetryAfter(QByteArray("Sun, 40 Nov 1994 08:49:37 GMT"), base).has_value());
        QVERIFY(!parseRetryAfter(QByteArray("Sun, 06 Foo 1994 08:49:37 GMT"), base).has_value());
        QVERIFY(!parseRetryAfter(QByteArray("Sun, 06 Nov 1994 99:49:37 GMT"), base).has_value());
        QVERIFY(!parseRetryAfter(QByteArray("Sun, 06 Nov 1994 08:49 GMT"), base).has_value());
    }

    void retryAfterIsClampedRatherThanTrusted() {
        std::mt19937 rng{1234};
        const auto inRange = [](const std::int64_t got, const std::int64_t lo,
                                const std::int64_t hi) { return got >= lo && got <= hi; };

        // No hint: the ladder, symmetric +/-20%.
        for (int i = 0; i < 200; ++i) {
            const std::int64_t got = retryDelayMs(1, std::nullopt, rng);
            QVERIFY2(inRange(got, 4000 - 800, 4000 + 800), qPrintable(QString::number(got)));
        }
        // A hint is honoured, with positive-only jitter.
        for (int i = 0; i < 200; ++i) {
            const std::int64_t got = retryDelayMs(0, 5, rng);
            QVERIFY2(inRange(got, 5000, 6000), qPrintable(QString::number(got)));
        }
        // Floor: 0 and a stale date both become 1 s, never 0 and never negative.
        QVERIFY(inRange(retryDelayMs(0, 0, rng), 1000, 1200));
        QVERIFY(inRange(retryDelayMs(0, -3600, rng), 1000, 1200));
        // Ceiling: "Retry-After: 3600" must not hold a slot for an hour.
        QVERIFY(inRange(retryDelayMs(0, 3600, rng), 60000, 72000));
        QCOMPARE(kMaxRetryAfterMs, std::int64_t{60'000});
    }

    void anAbsurdRetryAfterWaitsButTheLadderDoesNotTakeOver() {
        FakeServer server;
        DownloadQueue queue(server.factory());
        QTemporaryDir dir;

        QVERIFY(queue.enqueue("ra", "https://fake.cdn/ra.mp3",
                              dir.filePath("ra.mp3").toStdString()));
        // 600 s asked for. It must be honoured *as a clamp* -- 60 s, not 1 s.
        server.replies[0]->failWithRetryAfter(QNetworkReply::UnknownServerError, 503, "600");
        QTest::qWait(1500);

        QCOMPARE(static_cast<int>(server.requests.size()), 1); // ladder did not fire
        QCOMPARE(queue.waitingCount(), 1);                     // but the hint is in effect
        QVERIFY(queue.cancel("ra"));
    }

    void aMalformedRetryAfterFallsBackToTheLadder() {
        FakeServer server;
        DownloadQueue queue(server.factory());
        QTemporaryDir dir;

        QVERIFY(queue.enqueue("bad-ra", "https://fake.cdn/bad.mp3",
                              dir.filePath("bad.mp3").toStdString()));
        server.replies[0]->failWithRetryAfter(QNetworkReply::UnknownServerError, 503, "soon");
        // The ladder's first rung is 1 s +/- 20%, so this must arrive well inside
        // a second -- and must not crash, hang, or wait forever.
        QTRY_COMPARE_WITH_TIMEOUT(static_cast<int>(server.requests.size()), 2, 3000);
        QCOMPARE(queue.activeCount(), 1);
        server.replies[1]->succeed("recovered");
        QTest::qWait(10);
        QCOMPARE(readAll(dir.filePath("bad.mp3")), QByteArray("recovered"));
    }

    // ── .part claiming ───────────────────────────────────────────────────────
    // The old opener was WriteOnly|Truncate, which follows a symlink and races a
    // second writer. Measured on this machine against Qt 6.11.1:
    //   WriteOnly|NewOnly on an existing regular file -> fails
    //   WriteOnly|Truncate  on an existing regular file -> succeeds, empties it
    //   WriteOnly|NewOnly on a symlink                  -> fails
    //   WriteOnly|Truncate  on a symlink               -> succeeds and CLOBBERS the
    //                                                       symlink's target
    // so the tests below discriminate against the old code rather than merely
    // restating the new outcome.

    void aStalePartFromACrashedRunIsReclaimed() {
        FakeServer server;
        DownloadQueue queue(server.factory());
        QTemporaryDir dir;

        const QString dest = dir.filePath("stale.mp3");
        const QString part = dir.filePath("stale.mp3.part");
        QCOMPARE(writeFile(part, QByteArray("half a download")), qint64{15});

        QVERIFY(queue.enqueue("stale", "https://fake.cdn/stale.mp3", dest.toStdString()));
        const QByteArray body = "the whole thing";
        server.replies[0]->succeed(body);
        QTest::qWait(10);

        // Policy: an unlocked survivor is a crashed run, and a crashed run leaves
        // an unusable prefix (range resume is deliberately disabled, so there is
        // nothing to resume from). It is replaced, not appended to and not
        // refused -- refusing would wedge the item behind a file whose name is an
        // implementation detail.
        QCOMPARE(readAll(dest), body);
        QVERIFY(!QFile::exists(part));
    }

    void aPartHeldByALiveWriterIsACollisionNotAReclaim() {
        FakeServer serverA;
        FakeServer serverB;
        DownloadQueue first(serverA.factory());
        DownloadQueue second(serverB.factory());
        QTemporaryDir dir;

        const QString dest = dir.filePath("shared.mp3");
        const QString part = dir.filePath("shared.mp3.part");

        std::vector<int> secondStates;
        QObject::connect(&second, &DownloadQueue::itemStateChanged,
                         [&](const QString&, int state, int) { secondStates.push_back(state); });

        QVERIFY(first.enqueue("first", "https://fake.cdn/first.mp3", dest.toStdString()));
        QVERIFY(QFile::exists(part)); // first attempt is live and holds the lock

        QVERIFY(second.enqueue("second", "https://fake.cdn/second.mp3", dest.toStdString()));
        QTest::qWait(10);

        // flock() conflicts between two descriptors for one file inside a single
        // process (BSD documents it), so this also covers two queues in one app.
        QCOMPARE(secondStates.back(), static_cast<int>(DownloadState::FailedPermanent));
        QVERIFY(second.isEmpty());
        QVERIFY(QFile::exists(part));          // the live writer's scratch file survives
        QCOMPARE(readAll(part), QByteArray()); // untouched: nothing was written to it

        // And the live writer still completes normally.
        serverA.replies[0]->succeed("first body");
        QTest::qWait(10);
        QCOMPARE(readAll(dest), QByteArray("first body"));
    }

    void aSymlinkAtThePartPathIsUnlinkedAndNeverFollowed() {
        FakeServer server;
        DownloadQueue queue(server.factory());
        QTemporaryDir dir;

        const QString dest = dir.filePath("sym.mp3");
        const QString part = dir.filePath("sym.mp3.part");
        const QString victim = dir.filePath("victim.bin");
        QCOMPARE(writeFile(victim, QByteArray("SENTINEL")), qint64{8});
        QVERIFY(makeSymlink(victim, part));
        QCOMPARE(fs::symlink_status(part.toStdString()).type(), fs::file_type::symlink);

        QVERIFY(queue.enqueue("sym", "https://fake.cdn/sym.mp3", dest.toStdString()));
        const QByteArray body = "downloaded properly";
        server.replies[0]->succeed(body);
        QTest::qWait(10);

        // The link is removed, not followed: the victim keeps its bytes and the
        // destination holds the download. WriteOnly|Truncate would have written
        // the payload into victim.bin and left dest as a dangling link.
        QCOMPARE(readAll(victim), QByteArray("SENTINEL"));
        QCOMPARE(readAll(dest), body);
        QVERIFY(fs::symlink_status(part.toStdString()).type() == fs::file_type::not_found);
    }

    void theScratchFileIsRemovedOnEveryNonSuccessPath() {
        QTemporaryDir dir;
        // Every block names its own scratch path; the suffix is the queue's, so
        // each one is dest + kPartSuffix and nothing else.
        const auto partOf = [&dir](const char* name) {
            return dir.filePath(QString::fromLatin1(name) + DownloadQueue::kPartSuffix);
        };

        { // retryable failure: unlinked before the retry is scheduled
            FakeServer server;
            DownloadQueue queue(server.factory());
            const QString part = partOf("f.mp3");
            QVERIFY(queue.enqueue("f", "https://fake.cdn/f.mp3",
                                  dir.filePath("f.mp3").toStdString()));
            QVERIFY(QFile::exists(part));
            server.replies[0]->fail(QNetworkReply::TimeoutError);
            QTest::qWait(10);
            QVERIFY(!QFile::exists(part));
            QCOMPARE(queue.waitingCount(), 1);
            QVERIFY(queue.cancel("f"));
            QTest::qWait(10);
            QVERIFY(!QFile::exists(part));
        }

        { // permanent failure
            FakeServer server;
            DownloadQueue queue(server.factory());
            const QString part = partOf("p.mp3");
            QVERIFY(queue.enqueue("p", "https://fake.cdn/p.mp3",
                                  dir.filePath("p.mp3").toStdString()));
            QVERIFY(QFile::exists(part));
            server.replies[0]->fail(QNetworkReply::ContentNotFoundError, 404);
            QTest::qWait(10);
            QVERIFY(!QFile::exists(part));
            QVERIFY(!QFile::exists(dir.filePath("p.mp3")));
        }

        { // cancel while queued for retry
            FakeServer server;
            DownloadQueue queue(server.factory());
            const QString part = partOf("cw.mp3");
            QVERIFY(queue.enqueue("cw", "https://fake.cdn/cw.mp3",
                                  dir.filePath("cw.mp3").toStdString()));
            server.replies[0]->fail(QNetworkReply::TimeoutError);
            QTest::qWait(10);
            QVERIFY(queue.cancel("cw"));
            QTest::qWait(10);
            QVERIFY(!QFile::exists(part));
            QVERIFY(queue.isEmpty());
        }

        { // queue destroyed while an attempt is live
            FakeServer server;
            auto queue = std::make_unique<DownloadQueue>(server.factory());
            const QString part = partOf("d.mp3");
            QVERIFY(queue->enqueue("d", "https://fake.cdn/d.mp3",
                                   dir.filePath("d.mp3").toStdString()));
            QVERIFY(QFile::exists(part));
            queue.reset();
            QVERIFY(!QFile::exists(part));
            QVERIFY(!QFile::exists(dir.filePath("d.mp3")));
        }
    }

    void aDanglingSymlinkAtThePartPathIsNotMistakenForACrashedRun() {
        FakeServer server;
        DownloadQueue queue(server.factory());
        QTemporaryDir dir;

        const QString part = dir.filePath("dangling.mp3.part");
        QVERIFY(makeSymlink(dir.filePath("does-not-exist"), part));
        // symlink_status does not follow links, so a dangling link reports
        // symlink rather than not_found -- which is exactly what lets the queue
        // tell "path is absent" apart from "path is hostile".
        QCOMPARE(fs::symlink_status(part.toStdString()).type(), fs::file_type::symlink);

        QVERIFY(queue.enqueue("dang", "https://fake.cdn/dang.mp3",
                              dir.filePath("dang.mp3").toStdString()));
        server.replies[0]->succeed("ok");
        QTest::qWait(10);
        QCOMPARE(readAll(dir.filePath("dang.mp3")), QByteArray("ok"));
        QVERIFY(!QFile::exists(part));
    }
};

#include "test_DownloadQueue.moc"

int runTestDownloadQueue(int argc, char** argv) {
    TestDownloadQueue t;
    return QTest::qExec(&t, argc, argv);
}
