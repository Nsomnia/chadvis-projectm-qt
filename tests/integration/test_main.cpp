#include <QGuiApplication>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QUrl>
#include <QtTest>

#include "audio/AudioEngine.hpp"
#include "qml_bridge/BridgeRegistration.hpp"

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
    TestQmlStartup test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_main.moc"
