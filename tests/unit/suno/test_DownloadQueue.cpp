#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "suno/DownloadQueue.hpp"

#include <QFile>
#include <QNetworkRequest>
#include <algorithm>
#include <random>

using namespace vc::suno;

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

    void fail(QNetworkReply::NetworkError err, int httpStatus = 0,
              bool acceptRanges = false, QByteArray body = {}) {
        setHeaders(httpStatus, acceptRanges);
        setError(err, QStringLiteral("scripted failure"));
        payload_ = std::move(body);
        offset_ = 0;
        if (!payload_.isEmpty()) emit readyRead();
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
        QCOMPARE(classifyFailure(NE::AuthenticationRequiredError, 0),
                 FailureKind::Permanent);
        QCOMPARE(classifyFailure(NE::ContentAccessDenied, 0), FailureKind::Permanent);
    }

    void backoffLadderAndJitter() {
        QCOMPARE(backoffBaseMs(0), qint64{1000});
        QCOMPARE(backoffBaseMs(1), qint64{4000});
        QCOMPARE(backoffBaseMs(2), qint64{16000});
        QCOMPARE(backoffBaseMs(9), qint64{16000});   // capped
        QCOMPARE(backoffBaseMs(-3), qint64{1000});   // clamped

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
        QTest::qWait(10);  // drain deleteLater

        QFile out(dir.filePath("song.mp3"));
        QVERIFY(out.open(QIODevice::ReadOnly));
        QCOMPARE(out.readAll(), body);
        QVERIFY(!QFile::exists(dir.filePath("song.mp3.part")));  // .part renamed away
        QCOMPARE(idle.count(), 1);
    }

    void retryableFailureRetriesWithBackoff() {
        FakeServer server;
        DownloadQueue queue(server.factory());
        QTemporaryDir dir;

        const QByteArray body = "second time is the charm";
        QVERIFY(queue.enqueue("clip-r", "https://fake.cdn/r.mp3",
                              dir.filePath("r.mp3").toStdString()));
        server.replies[0]->fail(QNetworkReply::TimeoutError);

        QTRY_COMPARE(static_cast<int>(server.requests.size()), 2);  // waits out ~1s backoff
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
        QTest::qWait(150);  // generous window: nothing may come back

        QCOMPARE(static_cast<int>(server.requests.size()), 1);  // single attempt only
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

    void resumeSendsRangeHeaderAndSplicesParts() {
        FakeServer server;
        DownloadQueue queue(server.factory());
        QTemporaryDir dir;

        const QByteArray first = "halfway";
        const QByteArray rest = "-and-done";
        QVERIFY(queue.enqueue("clip-resume", "https://fake.cdn/res.mp3",
                              dir.filePath("res.mp3").toStdString()));

        // Attempt 1: partial data lands in .part, then the stream dies.
        server.replies[0]->openStream(first, /*acceptRanges*/ true);
        server.replies[0]->fail(QNetworkReply::TimeoutError);
        QVERIFY(QFile::exists(dir.filePath("res.mp3.part")));

        // Attempt 2 (after ~1s backoff): must carry Range from the offset.
        QTRY_COMPARE(static_cast<int>(server.requests.size()), 2);
        const QByteArray range = server.requests[1].rawHeader("Range");
        QCOMPARE(range, QByteArray("bytes=" + QByteArray::number(
                                        static_cast<qint64>(first.size())) + '-'));
        server.replies[1]->succeed(rest, /*httpStatus*/ 206);
        QTest::qWait(10);

        QFile out(dir.filePath("res.mp3"));
        QVERIFY(out.open(QIODevice::ReadOnly));
        QCOMPARE(out.readAll(), first + rest);  // spliced seamlessly
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

        QVERIFY(queue.cancel("c-2"));  // still queued
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
        QVERIFY(queue.cancel("live"));  // triggers abort() -> finished()
        QTest::qWait(10);

        QVERIFY(!states.empty());
        QCOMPARE(states.back(), static_cast<int>(DownloadState::Cancelled));
        QVERIFY(!QFile::exists(dir.filePath("live.mp3.part")));  // cleaned up
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
};

#include "test_DownloadQueue.moc"

int runTestDownloadQueue(int argc, char** argv) {
    TestDownloadQueue t;
    return QTest::qExec(&t, argc, argv);
}
