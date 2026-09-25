#include <QtTest>

#include "suno/SunoClient.hpp"

#include <QEventLoop>
#include <QObject>
#include <QSemaphore>
#include <QThread>
#include <QTimer>

#include <atomic>

using vc::suno::CredentialStoreWorker;

namespace {

void runEventLoopWithTimeout(QEventLoop& loop, int timeoutMs = 2000) {
    QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    loop.exec();
}

} // namespace

class TestCredentialRestoreWorker : public QObject {
    Q_OBJECT

private slots:
    void constructorThreadNeverRunsBlockingCredentialBackend() {
        QObject guiReceiver;
        QThread* constructingThread = QThread::currentThread();
        std::atomic_bool backendCalled{false};
        std::atomic_bool backendUsedConstructingThread{false};
        std::atomic_bool completionCalled{false};

        QCOMPARE(CredentialStoreWorker::completionConnectionType(), Qt::QueuedConnection);

        CredentialStoreWorker worker(
                &guiReceiver,
                [&](CredentialStoreWorker::Request) {
                    backendCalled.store(true);
                    backendUsedConstructingThread.store(
                            QThread::currentThread() == constructingThread);
                    return CredentialStoreWorker::Outcome{};
                });

        QEventLoop loop;
        QVERIFY(worker.requestRestore(
                CredentialStoreWorker::Request{},
                [&](CredentialStoreWorker::Outcome) {
                    completionCalled.store(true);
                    loop.quit();
                }));

        // requestRestore only posts to the owned worker and a queued GUI
        // completion; neither the backend nor completion can run inline.
        QVERIFY(!completionCalled.load());

        runEventLoopWithTimeout(loop);
        QVERIFY(backendCalled.load());
        QVERIFY(!backendUsedConstructingThread.load());
        QVERIFY(completionCalled.load());
        QCOMPARE(QThread::currentThread(), constructingThread);
        QVERIFY(!worker.isRestoreInFlight());
    }

    void concurrentRestoresCoalesceInsteadOfDoubleReading() {
        QObject guiReceiver;
        QSemaphore backendEntered(0);
        QSemaphore releaseBackend(0);
        std::atomic_int backendCalls{0};
        std::atomic_bool firstCompleted{false};
        std::atomic_bool duplicateCompleted{false};

        CredentialStoreWorker worker(
                &guiReceiver,
                [&](CredentialStoreWorker::Request) {
                    const int call = backendCalls.fetch_add(1) + 1;
                    if (call == 1) {
                        backendEntered.release();
                        releaseBackend.acquire();
                    }
                    return CredentialStoreWorker::Outcome{};
                });

        QVERIFY(worker.requestRestore(
                CredentialStoreWorker::Request{},
                [&](CredentialStoreWorker::Outcome) {
                    firstCompleted.store(true);
                }));
        QVERIFY(backendEntered.tryAcquire(1, 2000));

        QVERIFY(!worker.requestRestore(
                CredentialStoreWorker::Request{},
                [&](CredentialStoreWorker::Outcome) {
                    duplicateCompleted.store(true);
                }));
        QCOMPARE(backendCalls.load(), 1);
        QVERIFY(worker.isRestoreInFlight());

        releaseBackend.release();
        QTRY_VERIFY_WITH_TIMEOUT(firstCompleted.load(), 2000);
        QTRY_VERIFY_WITH_TIMEOUT(duplicateCompleted.load(), 2000);
        QVERIFY(!worker.isRestoreInFlight());
        QCOMPARE(backendCalls.load(), 1);
    }

    void emptyRestoreCompletesAndReopensGate() {
        QObject guiReceiver;
        std::atomic_int backendCalls{0};
        CredentialStoreWorker worker(
                &guiReceiver,
                [&](CredentialStoreWorker::Request) {
                    backendCalls.fetch_add(1);
                    return CredentialStoreWorker::Outcome{};
                });

        for (int expectedCall = 1; expectedCall <= 2; ++expectedCall) {
            QEventLoop loop;
            bool completed = false;
            QVERIFY(worker.requestRestore(
                    CredentialStoreWorker::Request{},
                    [&](CredentialStoreWorker::Outcome) {
                        completed = true;
                        loop.quit();
                    }));
            runEventLoopWithTimeout(loop);
            QVERIFY(completed);
            QVERIFY(!worker.isRestoreInFlight());
            QCOMPARE(backendCalls.load(), expectedCall);
        }
    }

    void discardedRestoreNotifiesEveryCompletion() {
        QObject guiReceiver;
        QSemaphore backendEntered(0);
        QSemaphore releaseBackend(0);
        std::atomic_int backendCalls{0};
        bool firstCompleted = false;
        bool duplicateCompleted = false;
        bool firstDiscarded = false;
        bool duplicateDiscarded = false;
        bool duplicateReused = false;

        CredentialStoreWorker worker(
                &guiReceiver,
                [&](CredentialStoreWorker::Request) {
                    backendCalls.fetch_add(1);
                    backendEntered.release();
                    releaseBackend.acquire();
                    return CredentialStoreWorker::Outcome{};
                });

        QVERIFY(worker.requestRestore(
                CredentialStoreWorker::Request{},
                [&](CredentialStoreWorker::Outcome result) {
                    firstCompleted = true;
                    firstDiscarded = result.restoreDiscarded;
                }));
        QVERIFY(backendEntered.tryAcquire(1, 2000));
        QVERIFY(!worker.requestRestore(
                CredentialStoreWorker::Request{},
                [&](CredentialStoreWorker::Outcome result) {
                    duplicateCompleted = true;
                    duplicateDiscarded = result.restoreDiscarded;
                    duplicateReused = result.reusedResult;
                }));

        worker.discardPendingRestore();
        releaseBackend.release();

        QTRY_VERIFY_WITH_TIMEOUT(firstCompleted, 2000);
        QTRY_VERIFY_WITH_TIMEOUT(duplicateCompleted, 2000);
        QVERIFY(firstDiscarded);
        QVERIFY(duplicateDiscarded);
        QVERIFY(duplicateReused);
        QCOMPARE(backendCalls.load(), 1);
    }
};

int runTestCredentialRestoreWorker(int argc, char** argv) {
    TestCredentialRestoreWorker test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_CredentialRestoreWorker.moc"
