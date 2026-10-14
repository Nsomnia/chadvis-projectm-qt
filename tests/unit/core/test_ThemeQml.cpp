/**
 * @file test_ThemeQml.cpp
 * @brief Covers the pure colour maths in the QML `Theme` singleton
 *
 * T0186. T0047 shipped a fix for "the theme does not persist" and shipped no
 * test with it. The C++ half was covered -- `vc::parseHexColor` /
 * `vc::toHexString` are exercised in test_PathSafety.cpp -- but the QML half,
 * which is where the actual defect was, had zero coverage: nothing in the tree
 * referenced parseRgbaHex, toRgbaHex, or applyAccent. So the failure mode was
 * not "a test went red" but "nobody could tell the fix was needed twice".
 *
 * The four things that went wrong, and what pins each one:
 *
 *  1. Byte order. `#000000FF` is opaque black to vc::Color and *fully
 *     transparent* to Qt, which reads an 8-digit hex as `#AARRGGBB`. That
 *     turned the whole window transparent and -- against a blue desktop
 *     wallpaper -- made the app read as a flat mid-blue sheet. Every assertion
 *     below compares ALPHA as well as RGB, because comparing only RGB is what
 *     let the original through: `#000000FF` and `#00000000` have identical RGB.
 *
 *  2. The write-back direction. Qt's own `color.toString()` emits `#AARRGGBB`,
 *     so persisting a colour through it moves alpha into red. That corruption
 *     survives a save and reload rather than being a transient display
 *     artefact, which is why `toRgbaHex` exists at all.
 *
 *  3. Six digits mean opaque. A 6-digit value is opaque *by definition*;
 *     treating the absent alpha as zero is how a colour silently vanishes.
 *
 *  4. Derivation. T0185 added `ramp()`/`luma()` and the glass/border/text
 *     tokens that follow `accent` and `background`. The guard is
 *     `derivedTokensFollowTheAccent`: set the accent to magenta and assert the
 *     glass border stops being cyan. Before T0185 that assertion fails, and it
 *     should -- that is the point of it.
 *
 * Deliberately NOT tested here: applyAccent/applyBackground, because they write
 * through `SettingsBridge` to the user's real config.toml. These are pure
 * function tests and must stay side-effect free.
 */

#include <QGuiApplication>
#include <QtTest>

#include <QColor>
#include <QQmlEngine>
#include <QVariant>

#include <memory>

namespace {

/// Channel comparison with an explicit tolerance, because the QML side does
/// arithmetic in floating point and then rounds back through a hex byte.
constexpr int kChannelTolerance = 2;

void compareColor(const QColor& actual, const QColor& expected,
                  const char* what, const char* file, int line)
{
    const auto check = [&](const char* channel, int got, int want) {
        QVERIFY2(std::abs(got - want) <= kChannelTolerance,
                 qPrintable(QStringLiteral("%1.%2: got %3, want %4 (+/- %5)")
                                .arg(QString::fromUtf8(what), QString::fromUtf8(channel))
                                .arg(got).arg(want).arg(kChannelTolerance)));
    };
    check("r", actual.red(), expected.red());
    check("g", actual.green(), expected.green());
    check("b", actual.blue(), expected.blue());
    check("a", actual.alpha(), expected.alpha());
}

#define COMPARE_COLOR(actual, expected) \
    compareColor((actual), (expected), #actual, __FILE__, __LINE__)

} // namespace

class test_ThemeQml : public QObject {
    Q_OBJECT

    std::unique_ptr<QQmlEngine> engine_;
    QObject* theme_ = nullptr;

    /// Call a function on the Theme singleton. QML `function foo(x)` declarations
    /// land in the metaobject, so invokeMethod reaches them; the return value of
    /// `Qt.rgba(...)` arrives as a QColor in a QVariant.
    QVariant call(const char* fn, const QVariantList& args)
    {
        QVariant out;
        const bool ok = QMetaObject::invokeMethod(
            theme_, fn, Qt::DirectConnection, Q_RETURN_ARG(QVariant, out),
            QGenericArgument("QVariantList", args));
        QVERIFY2(ok, qPrintable(QStringLiteral("Theme.%1() is not invokable").arg(QLatin1String(fn))));
        return out;
    }

    QColor callColor(const char* fn, const QVariantList& args)
    {
        return call(fn, args).value<QColor>();
    }

    QColor prop(const char* name) const
    {
        const QVariant v = theme_->property(name);
        QVERIFY2(v.isValid(), qPrintable(QStringLiteral("Theme has no property '%1'").arg(QLatin1String(name))));
        return v.value<QColor>();
    }

    QString propString(const char* name) const
    {
        return theme_->property(name).toString();
    }

private slots:

    void initTestCase()
    {
        engine_ = std::make_unique<QQmlEngine>();

        // Importing the module registers `Theme` (declared
        // QT_QML_SINGLETON_TYPE in cmake/TargetSetup.cmake) and `SettingsBridge`
        // (QML_ELEMENT/QML_SINGLETON) from the same generated registration code.
        // Theme.qml's `import ChadVis` resolves SettingsBridge that way; nothing
        // has to construct it for the pure-function tests below.
        QQmlComponent probe(engine_.get());
        probe.setData("import ChadVis\nQtObject{}", QUrl(QStringLiteral("qrc:/theme_probe.qml")));
        QObject* probeObject = probe.create();
        QVERIFY2(probeObject, qPrintable(QStringLiteral("cannot import the ChadVis module: %1")
                                             .arg(probe.errorString().toUtf8().constData())));
        delete probeObject;

        theme_ = engine_->singletonInstance<QObject*>(
            QStringLiteral("ChadVis"), QStringLiteral("Theme"));
        QVERIFY2(theme_,
                 "ChadVis.Theme singleton did not instantiate -- is it still marked "
                 "QT_QML_SINGLETON_TYPE in cmake/TargetSetup.cmake?");
    }

    // ── 1. Byte order: the byte that went missing ────────────────────────────

    void parseReadsTheLastByteAsAlpha_data()
    {
        QTest::addColumn<QString>("hex");
        QTest::addColumn<QColor>("expected");

        // The two values actually observed in config.toml during T0047.
        QTest::newRow("config black")   << "#000000FF" << QColor(0, 0, 0, 255);
        QTest::newRow("config green")   << "#00FF88FF" << QColor(0, 255, 136, 255);
        // ...and what Qt's own parser made of them, which is the regression.
        QTest::newRow("not argb magenta") << "#FF00FFFF" << QColor(255, 0, 255, 255);
        QTest::newRow("all four channels") << "#11223344" << QColor(0x11, 0x22, 0x33, 0x44);
        QTest::newRow("fully transparent")  << "#00000000" << QColor(0, 0, 0, 0);
        QTest::newRow("opaque white")       << "#FFFFFFFF" << QColor(255, 255, 255, 255);
        QTest::newRow("lower case")         << "#abcdef01" << QColor(0xab, 0xcd, 0xef, 0x01);
    }

    void parseReadsTheLastByteAsAlpha()
    {
        QFETCH(QString, hex);
        QFETCH(QColor, expected);
        COMPARE_COLOR(callColor("parseRgbaHex", {hex}), expected);
    }

    /// The assertion that would have caught T0047 on the day. `#000000FF` read
    /// as `#AARRGGBB` is rgba(0, 0, 255, 0) -- transparent blue. Its RGB
    /// matches opaque black's, so a colour-only check passes and the window
    /// goes transparent. Alpha is the whole of the test.
    void opaqueBlackIsNotTransparentBlue()
    {
        const QColor parsed = callColor("parseRgbaHex", {"#000000FF"});
        QCOMPARE(parsed.alpha(), 255);
        QCOMPARE(parsed.blue(), 0);
        QCOMPARE(QColor(parsed).name(QColor::HexArgb).toUpper(), QStringLiteral("#FF000000"));
    }

    // ── 2. Write-back direction ──────────────────────────────────────────────

    void toRgbaHexPutsAlphaLast()
    {
        QCOMPARE(call("toRgbaHex", {QColor(0x11, 0x22, 0x33, 0x44)}).toString(),
                 QStringLiteral("#11223344"));
        // Qt's own toString() would emit "#44112233" here -- alpha into red.
        QCOMPARE(call("toRgbaHex", {QColor(255, 0, 0, 128)}).toString(),
                 QStringLiteral("#FF000080"));
    }

    void roundTripIsExact_data()
    {
        QTest::addColumn<QString>("hex");
        QTest::newRow("config black")  << "#000000FF";
        QTest::newRow("config green")  << "#00FF88FF";
        QTest::newRow("shipped accent") << "#00BCD4FF";
        QTest::newRow("mid alpha")     << "#11223344";
        QTest::newRow("six digit")     << "#1A1A1AFF";
    }

    void roundTripIsExact()
    {
        QFETCH(QString, hex);
        QCOMPARE(call("toRgbaHex", {callColor("parseRgbaHex", {hex})}).toString(),
                 hex.toUpper());
    }

    // ── 3. Six digits ────────────────────────────────────────────────────────

    void sixDigitInputIsOpaque()
    {
        const QColor parsed = callColor("parseRgbaHex", {"#1a1a1a"});
        QCOMPARE(parsed.alpha(), 255);
        COMPARE_COLOR(parsed, QColor(0x1a, 0x1a, 0x1a, 255));
    }

    // ── 4. Derivation (T0185) ────────────────────────────────────────────────

    /// The multiplicative ramp this replaced: `Qt.lighter(black, 2)` is still
    /// black, so `applyBackground` at config's `#000000FF` used to derive five
    /// surfaces all equal to black. The relative ramp must produce distinct
    /// values from black and keep them ordered.
    void rampFromBlackIsDistinctAndOrdered()
    {
        const QColor base(0, 0, 0, 255);
        int previous = -1;
        for (double fraction : {0.02, 0.05, 0.085, 0.12, 0.139, 0.209}) {
            const QColor stepped = callColor("ramp", {base, fraction});
            QCOMPARE(stepped.alpha(), 255);
            const int grey = stepped.red();
            QVERIFY2(grey > previous,
                     qPrintable(QStringLiteral("ramp(black, %1) = %2, not brighter than %3")
                                    .arg(fraction).arg(grey).arg(previous)));
            previous = grey;
        }
        QCOMPARE(previous, callColor("ramp", {base, 0.209}).red());
    }

    /// The dark theme must look like the dark theme. These fractions were fitted
    /// against `#1a1a1a`, so this pins the hand-tuned values the app shipped
    /// with -- within the channel tolerance, which absorbs float rounding only.
    void rampReproducesTheDarkDefaults_data()
    {
        QTest::addColumn<double>("fraction");
        QTest::addColumn<QColor>("expected");

        QTest::newRow("surface")      << 0.05  << QColor(0x25, 0x25, 0x25);
        QTest::newRow("surfaceRaised") << 0.085 << QColor(0x2d, 0x2d, 0x2d);
        QTest::newRow("surfaceOverlay")<< 0.12  << QColor(0x35, 0x35, 0x35);
        QTest::newRow("border")        << 0.139 << QColor(0x3a, 0x3a, 0x3a);
        QTest::newRow("borderLight")   << 0.209 << QColor(0x4a, 0x4a, 0x4a);
        QTest::newRow("textPrimary")   << 0.864 << QColor(0xe0, 0xe0, 0xe0);
        // T0119 measured textPrimaryVariant at 6.53-9.26:1 and says leave it
        // alone; this is the assertion that keeps it that way.
        QTest::newRow("textVariant")   << 0.712 << QColor(0xbd, 0xbd, 0xbd);
    }

    void rampReproducesTheDarkDefaults()
    {
        QFETCH(double, fraction);
        QFETCH(QColor, expected);
        COMPARE_COLOR(callColor("ramp", {QColor(0x1a, 0x1a, 0x1a, 255), fraction}), expected);
    }

    /// The T0185 regression guard. Before that item, `glassBorder`,
    /// `glassHighlight` and `glowAccent` hardcoded `Qt.rgba(0, 0.733, 0.831, ..)`
    /// -- the shipped cyan -- so changing the accent left every glass border the
    /// old default. Setting the accent must move them.
    void derivedTokensFollowTheAccent()
    {
        const QString originalAccent = propString("accent");

        theme_->setProperty("accent", QColor(0xff, 0x00, 0xaa));

        const QColor accent(0xff, 0x00, 0xaa);
        // Glass picks up the accent's RGB and keeps its alpha, not its hue.
        const QColor glassBorder = prop("glassBorder");
        COMPARE_COLOR(glassBorder, QColor(accent.red(), accent.green(), accent.blue(), 77));
        const QColor glassHighlight = prop("glassHighlight");
        COMPARE_COLOR(glassHighlight, QColor(accent.red(), accent.green(), accent.blue(), 26));

        // ...and the surface family must NOT follow the accent. If it did, the
        // accent and the background would be the same control.
        COMPARE_COLOR(prop("background"), QColor(0x1a, 0x1a, 0x1a, 255));

        theme_->setProperty("accent", QColor(originalAccent));
    }

    void textOnAccentFollowsTheAccentLuminance()
    {
        const QString originalAccent = propString("accent");

        // The shipped #00bcd4 cyan is light, so text on it stays dark.
        theme_->setProperty("accent", QColor(0x00, 0xbc, 0xd4));
        QCOMPARE(prop("textOnAccent").lightnessF() < 0.5, true);

        theme_->setProperty("accent", QColor(0x00, 0x00, 0x00));
        QCOMPARE(prop("textOnAccent").lightnessF() > 0.5, true);

        theme_->setProperty("accent", QColor(originalAccent));
    }

    void backgroundIsLightInvertsAtTheRightPlace()
    {
        const QString original = propString("background");
        theme_->setProperty("background", QColor(0x1a, 0x1a, 0x1a));
        QCOMPARE(theme_->property("backgroundIsLight").toBool(), false);
        theme_->setProperty("background", QColor(0xfa, 0xfa, 0xfa));
        QCOMPARE(theme_->property("backgroundIsLight").toBool(), true);
        theme_->setProperty("background", QColor(original));
    }

    void lumaOrdersLightOverDark()
    {
        const double dark = call("luma", {QColor(0x00, 0x00, 0x00)}).toDouble();
        const double grey = call("luma", {QColor(0x80, 0x80, 0x80)}).toDouble();
        const double light = call("luma", {QColor(0xff, 0xff, 0xff)}).toDouble();
        QCOMPARE(dark, 0.0);
        QCOMPARE(light, 1.0);
        QVERIFY(grey > dark && grey < light);
    }
};

QTEST_MAIN(test_ThemeQml)

#include "test_ThemeQml.moc"