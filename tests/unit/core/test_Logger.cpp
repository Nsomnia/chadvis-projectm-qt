#include <QtTest>
#include <atomic>
#include <vector>
#include "core/Logger.hpp"
#include "util/JThread.hpp" // vc::JThread: portable jthread (AppleClang libc++ lacks std::jthread)

class TestLogger : public QObject {
    Q_OBJECT

private slots:
    // This suite is the first thing in unit_tests to touch the Logger. Do
    // NOT construct it here: the concurrency slot below must be the very
    // first Logger::get() in the process, from many threads at once (T0025).
    void initTestCase() {}

    void testGetIsSafeBeforeInitFromManyThreads() {
        // Before init(), every thread must receive a usable logger with no
        // race: the Meyers singleton serializes first construction and
        // get() never lazily runs init() (the old race was two threads both
        // seeing a null static and both running init()).
        std::atomic<int> nullLoggerCalls{0};
        std::vector<vc::JThread> threads;
        threads.reserve(8);
        for (int i = 0; i < 8; ++i) {
            threads.emplace_back([i, &nullLoggerCalls] {
                for (int j = 0; j < 64; ++j) {
                    if (!vc::Logger::get()) ++nullLoggerCalls;
                    LOG_INFO("pre-init message from worker {}", i);
                }
            });
        }
        for (auto& thread : threads) {
            thread.join();
        }
        QCOMPARE(nullLoggerCalls.load(), 0);
        QVERIFY(vc::Logger::get() != nullptr);
        vc::Logger::shutdown();
    }

    void testInitialization() {
        vc::Logger::init("test_app", true);
        QVERIFY(vc::Logger::get() != nullptr);

        // Should not crash
        LOG_INFO("Test info message");
        LOG_WARN("Test warn message");
        LOG_ERROR("Test error message");

        vc::Logger::shutdown();
    }

    void testDoubleInit() {
        vc::Logger::init("test_app", true);
        // Second init should be safe (idempotent or handled)
        vc::Logger::init("test_app", true);
        QVERIFY(vc::Logger::get() != nullptr);
        vc::Logger::shutdown();
    }
};

int runTestLogger(int argc, char** argv) {
    TestLogger tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "test_Logger.moc"
