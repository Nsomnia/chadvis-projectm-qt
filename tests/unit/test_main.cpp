/**
 * @file test_main.cpp
 * @brief Test suite entry point using Qt Test.
 */
#include <QCoreApplication>
#include <QtTest>

// Include test classes
#include "core/test_AudioAnalyzer.cpp"
#include "core/test_ConfigParsers.cpp"
#include "core/test_Logger.cpp"
#include "visualizer/test_PresetScanner.cpp"

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    int status = 0;

    {
        TestLogger tc;
        status |= QTest::qExec(&tc, argc, argv);
    }

    {
        TestAudioAnalyzer tc;
        status |= QTest::qExec(&tc, argc, argv);
    }

    {
        TestConfigParsers tc;
        status |= QTest::qExec(&tc, argc, argv);
    }

    {
        TestPresetScanner tc;
        status |= QTest::qExec(&tc, argc, argv);
    }

    return status;
}
