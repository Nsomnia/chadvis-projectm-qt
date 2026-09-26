#include <QGuiApplication>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QUrl>
#include <QtTest>

#include "audio/AudioEngine.hpp"
#include "qml_bridge/BridgeRegistration.hpp"

int runTestProjectMFramebuffer(int argc, char** argv);

class TestQmlStartup : public QObject
{
    Q_OBJECT

private slots:
    void mainQmlLoadsAndRenders()
    {
        vc::AudioEngine audio;
        const auto audioResult = audio.init();
        QVERIFY(audioResult.isOk());

        QQmlApplicationEngine engine;
        qml_bridge::registerBridges(
            &engine, &audio, nullptr, nullptr, nullptr, nullptr, nullptr);

        engine.load(
            QUrl(QStringLiteral("qrc:/qt/qml/ChadVis/src/qml/main.qml")));
        const QList<QObject*> roots = engine.rootObjects();
        QVERIFY(!roots.isEmpty());

        auto* window = qobject_cast<QQuickWindow*>(roots.first());
        QVERIFY(window);
        QVERIFY(window->isVisible());
        QTRY_VERIFY_WITH_TIMEOUT(window->isExposed(), 3000);
        QTest::qWait(100);

        const QImage frame = window->grabWindow();
        QVERIFY(!frame.isNull());
        QVERIFY(frame.width() > 0);
        QVERIFY(frame.height() > 0);
    }
};

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);

    int status = 0;
    // Selecting test functions on the command line (as the GL-only ctest entry
    // does) must not be replayed against a suite that does not have them, or
    // QTest fails the run with "Function not found" in the other class.
    if (argc > 1) {
        status |= runTestProjectMFramebuffer(argc, argv);
        return status;
    }

    status |= runTestProjectMFramebuffer(argc, argv);

    TestQmlStartup test;
    status |= QTest::qExec(&test, argc, argv);
    return status;
}

#include "test_main.moc"
