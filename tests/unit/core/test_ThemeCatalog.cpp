/**
 * @file test_ThemeCatalog.cpp
 * @brief Discovery and validation for <configDir>/themes/*.json
 *
 * Fail-closed on bad input, never on bad neighbours: one rejected file
 * must not abort discovery (THEME-ENGINE-PLAN.md §3.9, §11). Each case
 * below isolates one rejection reason while a valid theme proves the
 * others still load after it.
 */

#include <QtTest>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "core/ThemeCatalog.hpp"
#include "util/FileUtils.hpp"

using namespace vc;

namespace {

QByteArray toJson(const QJsonObject& object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QJsonObject validTheme(const QString& id)
{
    QJsonObject colors;
    colors.insert(QStringLiteral("accent"), QStringLiteral("#BB86FCFF"));
    colors.insert(QStringLiteral("background"), QStringLiteral("#121212FF"));
    colors.insert(QStringLiteral("textPrimary"), QStringLiteral("#E1E1E5FF"));
    colors.insert(QStringLiteral("textOnAccent"), QStringLiteral("#1A1A1AFF"));
    QJsonObject theme;
    theme.insert(QStringLiteral("schemaVersion"), 1);
    theme.insert(QStringLiteral("id"), id);
    theme.insert(QStringLiteral("displayName"), QStringLiteral("Valid"));
    theme.insert(QStringLiteral("description"), QStringLiteral("A valid theme."));
    theme.insert(QStringLiteral("isDark"), true);
    theme.insert(QStringLiteral("author"), QStringLiteral("Test"));
    theme.insert(QStringLiteral("colors"), colors);
    return theme;
}

bool writeFile(const fs::path& path, const QByteArray& bytes)
{
    QFile file(QString::fromStdString(path.string()));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    return file.write(bytes) == bytes.size();
}

} // namespace

class TestThemeCatalog : public QObject {
    Q_OBJECT

private slots:
    void missingDirectoryIsEmptyCatalog()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const fs::path absent = fs::path(temp.path().toStdString()) / "themes";
        ThemeCatalog catalog(absent);
        QVERIFY(catalog.themes().isEmpty());
        QVERIFY(catalog.errors().isEmpty());
        QVERIFY(!catalog.find(QStringLiteral("anything")).has_value());
    }

    void validThemeIsDiscovered()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const fs::path themesDir = fs::path(temp.path().toStdString()) / "themes";
        std::error_code ec;
        fs::create_directories(themesDir, ec);
        QVERIFY(!ec);
        QVERIFY(writeFile(themesDir / "valid.json", toJson(validTheme(QStringLiteral("valid-theme")))));

        ThemeCatalog catalog(themesDir);
        QCOMPARE(catalog.themes().size(), 1);
        QVERIFY(catalog.errors().isEmpty());
        const auto found = catalog.find(QStringLiteral("valid-theme"));
        QVERIFY(found.has_value());
        QCOMPARE(found->displayName, QStringLiteral("Valid"));
        QCOMPARE(found->isDark, true);
        QCOMPARE(found->colors.value(QStringLiteral("accent")), QStringLiteral("#BB86FCFF"));
        QCOMPARE(found->colors.value(QStringLiteral("background")), QStringLiteral("#121212FF"));
    }

    void badFilesDoNotBlockTheValidOne()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const fs::path themesDir = fs::path(temp.path().toStdString()) / "themes";
        std::error_code ec;
        fs::create_directories(themesDir, ec);
        QVERIFY(!ec);

        QVERIFY(writeFile(themesDir / "valid.json", toJson(validTheme(QStringLiteral("valid-theme")))));

        // Bad id: uppercase + underscore violate ^[a-z0-9][a-z0-9-]*$.
        QJsonObject badId = validTheme(QStringLiteral("Bad_Id"));
        QVERIFY(writeFile(themesDir / "bad-id.json", toJson(badId)));

        // Missing key: displayName absent.
        QJsonObject missingKey = validTheme(QStringLiteral("missing-key"));
        missingKey.remove(QStringLiteral("displayName"));
        QVERIFY(writeFile(themesDir / "missing-key.json", toJson(missingKey)));

        // Oversized: valid JSON padded past 64 KiB.
        QByteArray padded = toJson(validTheme(QStringLiteral("oversized")));
        padded += QByteArray(padded.size() + 70000, ' ');
        QVERIFY(writeFile(themesDir / "oversized.json", padded));

        // Bad contrast: near-black text on a near-black background.
        QJsonObject lowContrastColors;
        lowContrastColors.insert(QStringLiteral("accent"), QStringLiteral("#BB86FCFF"));
        lowContrastColors.insert(QStringLiteral("background"), QStringLiteral("#121212FF"));
        lowContrastColors.insert(QStringLiteral("textPrimary"), QStringLiteral("#1E1E1EFF"));
        lowContrastColors.insert(QStringLiteral("textOnAccent"), QStringLiteral("#1A1A1AFF"));
        QJsonObject lowContrast = validTheme(QStringLiteral("low-contrast"));
        lowContrast.insert(QStringLiteral("colors"), lowContrastColors);
        QVERIFY(writeFile(themesDir / "low-contrast.json", toJson(lowContrast)));

        ThemeCatalog catalog(themesDir);
        QCOMPARE(catalog.themes().size(), 1);
        QVERIFY(catalog.find(QStringLiteral("valid-theme")).has_value());
        QVERIFY(!catalog.find(QStringLiteral("Bad_Id")).has_value());
        QVERIFY(!catalog.find(QStringLiteral("missing-key")).has_value());
        QVERIFY(!catalog.find(QStringLiteral("oversized")).has_value());
        QVERIFY(!catalog.find(QStringLiteral("low-contrast")).has_value());

        // Four rejections, each naming its file. Order is filesystem order;
        // assert membership, not position.
        QCOMPARE(catalog.errors().size(), 4);
        const auto hasErrorFor = [this](const QStringList& errors, const QString& fileName) {
            for (const QString& error : errors) {
                if (error.startsWith(fileName + QStringLiteral(":"))) {
                    return true;
                }
            }
            return false;
        };
        QVERIFY(hasErrorFor(catalog.errors(), QStringLiteral("bad-id.json")));
        QVERIFY(hasErrorFor(catalog.errors(), QStringLiteral("missing-key.json")));
        QVERIFY(hasErrorFor(catalog.errors(), QStringLiteral("oversized.json")));
        QVERIFY(hasErrorFor(catalog.errors(), QStringLiteral("low-contrast.json")));
    }

    void malformedJsonIsRejected()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const fs::path themesDir = fs::path(temp.path().toStdString()) / "themes";
        std::error_code ec;
        fs::create_directories(themesDir, ec);
        QVERIFY(!ec);
        QVERIFY(writeFile(themesDir / "valid.json", toJson(validTheme(QStringLiteral("valid-theme")))));
        QVERIFY(writeFile(themesDir / "broken.json", QByteArray(QStringLiteral("{ not json").toUtf8())));

        ThemeCatalog catalog(themesDir);
        QCOMPARE(catalog.themes().size(), 1);
        QCOMPARE(catalog.errors().size(), 1);
        QVERIFY(catalog.errors().first().startsWith(QStringLiteral("broken.json:")));
    }

    void duplicateIdKeepsTheFirstFile()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const fs::path themesDir = fs::path(temp.path().toStdString()) / "themes";
        std::error_code ec;
        fs::create_directories(themesDir, ec);
        QVERIFY(!ec);
        // listFiles sorts, so a.json loads first.
        QJsonObject first = validTheme(QStringLiteral("dup"));
        first.insert(QStringLiteral("displayName"), QStringLiteral("First"));
        QJsonObject second = validTheme(QStringLiteral("dup"));
        second.insert(QStringLiteral("displayName"), QStringLiteral("Second"));
        QVERIFY(writeFile(themesDir / "a.json", toJson(first)));
        QVERIFY(writeFile(themesDir / "b.json", toJson(second)));

        ThemeCatalog catalog(themesDir);
        QCOMPARE(catalog.themes().size(), 1);
        QCOMPARE(catalog.find(QStringLiteral("dup"))->displayName, QStringLiteral("First"));
        QCOMPARE(catalog.errors().size(), 1);
        QVERIFY(catalog.errors().first().startsWith(QStringLiteral("b.json:")));
    }

    void reloadPicksUpNewFiles()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const fs::path themesDir = fs::path(temp.path().toStdString()) / "themes";
        std::error_code ec;
        fs::create_directories(themesDir, ec);
        QVERIFY(!ec);
        QVERIFY(writeFile(themesDir / "first.json", toJson(validTheme(QStringLiteral("first-theme")))));

        ThemeCatalog catalog(themesDir);
        QCOMPARE(catalog.themes().size(), 1);

        QVERIFY(writeFile(themesDir / "second.json", toJson(validTheme(QStringLiteral("second-theme")))));
        catalog.reload();
        QCOMPARE(catalog.themes().size(), 2);
        QVERIFY(catalog.find(QStringLiteral("second-theme")).has_value());
        QVERIFY(catalog.errors().isEmpty());
    }

    void idPattern_data()
    {
        QTest::addColumn<QString>("id");
        QTest::addColumn<bool>("valid");
        QTest::newRow("simple") << QStringLiteral("material") << true;
        QTest::newRow("hyphens") << QStringLiteral("my-theme-1") << true;
        QTest::newRow("single char") << QStringLiteral("a") << true;
        QTest::newRow("uppercase") << QStringLiteral("Bad") << false;
        QTest::newRow("underscore") << QStringLiteral("bad_id") << false;
        QTest::newRow("dotdot") << QStringLiteral("../etc/passwd") << false;
        QTest::newRow("space") << QStringLiteral("bad id") << false;
        QTest::newRow("empty") << QStringLiteral("") << false;
        QTest::newRow("reserved custom") << QStringLiteral("custom") << false;
    }

    void idPattern()
    {
        QFETCH(QString, id);
        QFETCH(bool, valid);
        QCOMPARE(ThemeCatalog::isValidId(id), valid);
    }
};

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    TestThemeCatalog test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_ThemeCatalog.moc"
