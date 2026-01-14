#include <QtTest>
#include "core/Logger.hpp"

class TestLogger : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        // Ensure clean state
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
