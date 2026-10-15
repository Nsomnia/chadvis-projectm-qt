#include <QGuiApplication>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QTimer>
#include <QUrl>
#include <QtTest>

#include "audio/AudioEngine.hpp"
#include "core/Config.hpp"
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

    /// T0047's real regression guard, and the one the item never had.
    ///
    /// The theme used to show the QML default cyan `#00bcd4` while
    /// config.toml said `#00FF88` green, because the seeding that was supposed
    /// to apply the persisted colours ran in a scope that never executes (a
    /// singleton's own `Component.onCompleted`, ordered against a bridge that
    /// may not exist yet). Nothing caught it: every other test in this file
    /// only checks that a window exists and navigates.
    ///
    /// This asserts the property the user actually sees -- `Theme.accent` after
    /// main.qml has loaded -- against the value vc::UIConfig carries. It runs
    /// headless, needs no GL, and fails loudly if the seeding regresses.
    void themeAccentComesFromThePersistedConfig() {
        vc::AudioEngine audio;
        QQmlApplicationEngine engine;

        auto* window = loadShell(audio, engine);
        QVERIFY(window);

        // The QML Theme singleton. Looked up by URI+name rather than through
        // rootObjects because main.qml's root is the ApplicationWindow, not the
        // singleton, and a singleton is not a root object.
        auto* theme = engine.singletonInstance<QObject*>("ChadVis", "Theme");
        QVERIFY2(theme, "the ChadVis Theme singleton is not registered");

        // Read the accent the way QML holds it, and compare CHANNEL values, not
        // hex text. A string comparison has to pick a byte order, and the two
        // sides disagree by design: vc::Color writes #RRGGBBAA and Qt's
        // color.toString() writes #AARRGGBB. Comparing the channels is the only
        // comparison that cannot be right for the wrong reason.
        const QVariant accentVariant = theme->property("accent");
        QVERIFY2(accentVariant.isValid(), "Theme.accent is not a readable property");
        const QColor accent = accentVariant.value<QColor>();
        const vc::Color expectedAccent = vc::Config::instance().ui().accentColor;
        QCOMPARE(accent.red(), expectedAccent.r);
        QCOMPARE(accent.green(), expectedAccent.g);
        QCOMPARE(accent.blue(), expectedAccent.b);
        // Opaque. This is the assertion a colour-only comparison cannot make:
        // the T0047 regression produced a colour with the right RED, GREEN and
        // BLUE and an alpha of 0, so it passed every channel check and rendered
        // a fully transparent window that showed the desktop wallpaper through.
        QCOMPARE(accent.alpha(), 255);

        // Same channel comparison for the background, for the same reason. The
        // background is the one that broke: config's #000000FF is opaque black,
        // and Qt reading it as #AARRGGBB made it transparent -- a window you can
        // see the desktop wallpaper through, which on a blue-toned wallpaper is
        // indistinguishable from a blue theme.
        const QVariant backgroundVariant = theme->property("background");
        QVERIFY2(backgroundVariant.isValid(), "Theme.background is not a readable property");
        const QColor background = backgroundVariant.value<QColor>();
        const vc::Color expectedBackground = vc::Config::instance().ui().backgroundColor;
        QCOMPARE(background.red(), expectedBackground.r);
        QCOMPARE(background.green(), expectedBackground.g);
        QCOMPARE(background.blue(), expectedBackground.b);
        QCOMPARE(background.alpha(), 255);
    }

    /// Pixel-level proof of what the user actually sees, for T0047.
    ///
    /// The property assertions above prove the *value* is right; this proves the
    /// *paint* is right. A theme can hold the correct colour and still render
    /// blue if something downstream recomputes it, which is exactly the
    /// symptom being chased: the app looked mid-blue while the config said
    /// green. Sampling the composited frame removes every layer in between.
    ///
    /// Two samples, deliberately: one inside the nav rail over the accent, one
    /// on the main content surface. If either is blue-ish the UI is wrong even
    /// though the properties read correctly.
    void renderedFrameIsNotBlueWhenConfigSaysGreen() {
        vc::AudioEngine audio;
        QQmlApplicationEngine engine;

        auto* window = loadShell(audio, engine);
        QVERIFY(window);
        QVERIFY(window->isVisible());
        QTRY_VERIFY_WITH_TIMEOUT(window->isExposed(), 3000);
        QTest::qWait(150);

        const QImage frame = window->grabWindow();
        QVERIFY(!frame.isNull());

        // The mean of every pixel in the top rail band and the content band.
        // A mean is robust to a single accent element and would still be moved
        // by a background or surface that is the wrong hue.
        auto meanOf = [&frame](int y0, int y1) {
            quint64 r = 0, g = 0, b = 0, n = 0;
            for (int y = y0; y < y1 && y < frame.height(); ++y) {
                for (int x = 0; x < frame.width(); ++x) {
                    const QRgb p = frame.pixel(x, y);
                    r += qRed(p); g += qGreen(p); b += qBlue(p); ++n;
                }
            }
            if (n == 0) return QColor();
            return QColor(int(r / n), int(g / n), int(b / n));
        };

        const QColor rail = meanOf(0, 90);
        const QColor content = meanOf(140, frame.height() - 40);

        // "Blue-dominant" is the condition worth failing on: blue above both
        // reds and greens by a margin. Green-accented UI cannot satisfy it.
        const auto blueDominant = [](const QColor& c) {
            return c.blue() > c.red() + 40 && c.blue() > c.green() + 40;
        };
        QVERIFY2(!blueDominant(rail),
                 qPrintable(QStringLiteral("the nav rail renders blue-dominant "
                                           "(R%1 G%2 B%3); config accent is #%4")
                                .arg(rail.red()).arg(rail.green()).arg(rail.blue())
                                .arg(QString::fromStdString(
                                    vc::Config::instance().ui().accentColor.toHex()))));
        QVERIFY2(!blueDominant(content),
                 qPrintable(QStringLiteral("the content surface renders blue-dominant "
                                           "(R%1 G%2 B%3); config background is #%4")
                                .arg(content.red()).arg(content.green()).arg(content.blue())
                                .arg(QString::fromStdString(
                                    vc::Config::instance().ui().backgroundColor.toHex()))));

        // And the frame is not uniformly transparent. This is the assertion
        // that catches the actual T0047 regression, and it has to be a mean
        // rather than a single pixel: sampling one centre pixel reports a hole
        // between controls as if the whole window were transparent, which is a
        // false failure. A window whose background lost its alpha is
        // transparent EVERYWHERE, so the mean is 0 only in the real case.
        int transparent = 0;
        int sampled = 0;
        for (int y = 0; y < frame.height(); y += 8) {
            for (int x = 0; x < frame.width(); x += 8) {
                ++sampled;
                if (qAlpha(frame.pixel(x, y)) == 0) ++transparent;
            }
        }
        const double opaqueFraction = sampled > 0
            ? double(sampled - transparent) / double(sampled) : 0.0;
        QVERIFY2(opaqueFraction > 0.95,
                 qPrintable(QStringLiteral("only %1% of sampled pixels are opaque; the "
                                           "background colour lost its alpha, so the window "
                                           "is see-through and shows the desktop wallpaper")
                                .arg(opaqueFraction * 100.0, 0, 'f', 1)));
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

    /// T0074. A QML Shortcut is processed before a Dialog's own Esc handling,
    /// so the window-scope Esc used to close the whole Settings window while the
    /// modal "Reset all settings?" confirmation was open — a user backing out of
    /// a destructive confirmation lost every unsaved edit instead.
    ///
    /// The test asserts the SHORTCUT's enabled state rather than sending key
    /// events, because synthetic keys do not reach QML shortcuts under the
    /// offscreen platform plugin: the first version of this test sent Esc and
    /// passed against the very build that had the bug, which is the vacuous
    /// green the red-check exists to catch. The contract being pinned is ours —
    /// "the Esc shortcut is disabled while a modal dialog is up" — and that is
    /// a property, observable without focus plumbing.
    void escapeRespectsTheResetDialog()
    {
        vc::AudioEngine audio;
        QQmlApplicationEngine engine;
        auto* window = loadShell(audio, engine);
        QVERIFY(window);
        auto* settings = findSettingsWindow(window);
        QVERIFY(settings);

        settings->show();
        QTRY_VERIFY(settings->isVisible());

        auto* shortcut =
            settings->findChild<QObject*>(QStringLiteral("settingsEscapeShortcut"));
        QVERIFY2(shortcut, "SettingsWindow declares no Shortcut named "
                           "\"settingsEscapeShortcut\" — the Esc path changed "
                           "and this test has nothing to pin");
        auto* dialog = settings->findChild<QObject*>(QStringLiteral("resetDialog"));
        QVERIFY(dialog);

        // Window up, nothing modal: Esc is armed.
        QTRY_COMPARE(shortcut->property("enabled").toBool(), true);

        // Open the reset confirmation the way the footer does. The shortcut
        // must disarm — this is the whole of T0074.
        QVERIFY(QMetaObject::invokeMethod(settings, "requestReset"));
        QTRY_VERIFY(dialog->property("opened").toBool());
        QTRY_COMPARE_WITH_TIMEOUT(
            shortcut->property("enabled").toBool(), false, 1000);

        // Close the confirmation; the shortcut must re-arm, because Esc with
        // nothing modal is the ordinary dismiss and must keep working.
        QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
        QTRY_VERIFY(!dialog->property("opened").toBool());
        QTRY_COMPARE(shortcut->property("enabled").toBool(), true);

        settings->hide();
        QVERIFY(!settings->isVisible());
    }

    /// T0075. "Clear All" used to call PlaylistBridge.clear() directly from two
    /// entry points, so one right-click destroyed the whole queue. Both now
    /// route through requestClearAll(), which opens a confirmation. This pins
    /// the ask: requesting the clear opens the dialog and clears nothing.
    ///
    /// Deliberately NOT accepted here. Accepting would clear a real queue —
    /// the test's AudioEngine is the user's config directory — and the defect
    /// being pinned is the missing confirmation, not Qt's Yes button. The
    /// un-accepted leg is what makes this test safe to run on a machine with
    /// a live playlist.
    void requestingClearAllOpensAConfirmation()
    {
        vc::AudioEngine audio;
        QQmlApplicationEngine engine;
        auto* window = loadShell(audio, engine);
        QVERIFY(window);

        // The panel is instantiated inside the shell; find it by the dialog it
        // now owns, which is also the thing being asserted.
        auto* dialog = window->findChild<QObject*>(QStringLiteral("clearConfirm"));
        QVERIFY2(dialog, "PlaylistPanel declares no Dialog named "
                         "\"clearConfirm\" — the Clear All path changed and "
                         "this test has nothing to pin");
        QVERIFY(!dialog->property("opened").toBool());

        // The panel's single routed entry point. Red against the old code:
        // requestClearAll() did not exist, and the entries cleared directly.
        auto* panel = dialog->parent();
        QVERIFY(QMetaObject::invokeMethod(panel, "requestClearAll"));
        QTRY_VERIFY(dialog->property("opened").toBool());

        // Close without accepting: the queue must be untouched. Nothing is
        // asserted about PlaylistBridge's count — the singleton in this process
        // is not wired to the engine, and asserting on it would be vacuous.
        QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
        QTRY_VERIFY(!dialog->property("opened").toBool());
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
