#include <QGuiApplication>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QTimer>
#include <QUrl>
#include <QtTest>

#include "audio/AudioEngine.hpp"
#include "qml_bridge/BridgeRegistration.hpp"

int runTestProjectMFramebuffer(int argc, char** argv);

class TestQmlStartup : public QObject
{
    Q_OBJECT

private:
    // Loads main.qml with the bridges the production engine registers. Returns
    // the shell window, or nullptr; audio and engine must outlive the caller.
    static QQuickWindow* loadShell(vc::AudioEngine& audio, QQmlApplicationEngine& engine)
    {
        const auto audioResult = audio.init();
        if (!audioResult.isOk())
            return nullptr;

        qml_bridge::registerBridges(
            &engine, &audio, nullptr, nullptr, nullptr, nullptr, nullptr);

        engine.load(
            QUrl(QStringLiteral("qrc:/qt/qml/ChadVis/src/qml/main.qml")));
        const QList<QObject*> roots = engine.rootObjects();
        if (roots.isEmpty())
            return nullptr;

        return qobject_cast<QQuickWindow*>(roots.first());
    }

    // The Settings window is the one main.qml names. Found by name rather than
    // by position, because VideoView's native container is a QQuickWindow too
    // and "the second window in the tree" is not a fact that survives an edit.
    static QQuickWindow* findSettingsWindow(QQuickWindow* shell)
    {
        return shell->findChild<QQuickWindow*>(QStringLiteral("settingsWindow"));
    }

private slots:
    void mainQmlLoadsAndRenders()
    {
        vc::AudioEngine audio;
        QQmlApplicationEngine engine;

        auto* window = loadShell(audio, engine);
        QVERIFY(window);
        QVERIFY(window->isVisible());
        QTRY_VERIFY_WITH_TIMEOUT(window->isExposed(), 3000);
        QTest::qWait(100);

        const QImage frame = window->grabWindow();
        QVERIFY(!frame.isNull());
        QVERIFY(frame.width() > 0);
        QVERIFY(frame.height() > 0);
    }

    // Settings is the only route to sign-in (AccountPage), so the single call
    // the nav rail and the account chip both make has to put a window on
    // screen. It used to throw `TypeError: settingsWindowApi.open is not a
    // function` — a nonexistent QWindow API reached through a `var`-typed
    // property, so nothing but a launch could catch it.
    void settingsWindowOpensOnNavigate()
    {
        vc::AudioEngine audio;
        QQmlApplicationEngine engine;

        QStringList qmlErrors;
        connect(&engine, &QQmlEngine::warnings, this,
                [&qmlErrors](const QList<QQmlError>& warnings) {
                    for (const QQmlError& warning : warnings)
                        qmlErrors << warning.toString();
                });

        auto* window = loadShell(audio, engine);
        QVERIFY(window);

        auto* settings = findSettingsWindow(window);
        QVERIFY2(settings, "main.qml declares no QQuickWindow named "
                           "\"settingsWindow\"; the Settings surface is gone");
        QVERIFY(!settings->isVisible());

        // QWindow::show() on a Qt.ApplicationModal window may enter a nested
        // event loop and only return once the window hides, so a watchdog is
        // armed to close it: without one, a platform that blocks would turn
        // this into a hang rather than a failure. Where it does not block, the
        // watchdog never fires and the window is still up when navigate()
        // returns — which is what this build does.
        bool seenWhileUp = false;
        QTimer watchdog;
        watchdog.setSingleShot(true);
        connect(&watchdog, &QTimer::timeout, settings,
                [settings, &seenWhileUp] {
                    seenWhileUp = settings->isVisible();
                    settings->hide();
                });
        watchdog.start(250);

        const qsizetype errorsBefore = qmlErrors.size();
        const bool invoked = QMetaObject::invokeMethod(
            window, "navigate", Q_ARG(QVariant, QStringLiteral("settings")));
        QVERIFY(invoked);

        QVERIFY2(seenWhileUp || settings->isVisible(),
                 "navigate(\"settings\") left the Settings window hidden — the "
                 "call the nav rail and the account chip both make no longer "
                 "opens Settings");

        settings->hide();
        QVERIFY(!settings->isVisible());

        // The throwing form was `settingsWindowApi["open"]()`, reached through a
        // var-typed property: QWindow has no open(), so the TypeError aborted
        // navigate() before anything else ran.
        for (const QString& error : qmlErrors.mid(errorsBefore)) {
            QVERIFY2(false, qPrintable(
                QStringLiteral("navigate(\"settings\") reported: %1").arg(error)));
        }
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
