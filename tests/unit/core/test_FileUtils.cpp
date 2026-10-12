#include <QtTest>
#include "util/FileUtils.hpp"

#include <QFile>
#include <QTemporaryDir>

using namespace vc::file;

class TestFileUtils : public QObject {
    Q_OBJECT

private slots:
    void testBasicSeparators() {
        QVERIFY(sanitizeFilename("foo/bar\\baz.mp3") == "foo_bar_baz.mp3");
    }

    void testControlChars() {
        // Adjacent literals stop the C++ \x escape before the 'f' in "file".
        QVERIFY(sanitizeFilename(std::string{"test\x01\x02" "file"}) == "test__file");
        QVERIFY(sanitizeFilename(std::string{"test\x7F\x7F" "file"}) == "test__file");
    }

    void testReservedName() {
        QVERIFY(sanitizeFilename("CON") == "_CON");
        QVERIFY(sanitizeFilename("prn") == "_prn");
        QVERIFY(sanitizeFilename("aux") == "_aux");
        QVERIFY(sanitizeFilename("nul") == "_nul");
        QVERIFY(sanitizeFilename("com5") == "_com5");
        QVERIFY(sanitizeFilename("lpt9") == "_lpt9");
    }

    void testReservedWithExtension() {
        QVERIFY(sanitizeFilename("CON.txt") == "_CON.txt");
        QVERIFY(sanitizeFilename("LPT2.mp3") == "_LPT2.mp3");
        QVERIFY(sanitizeFilename("P.R.N") == "P.R.N"); // dot doesn't make reserved
    }

    void testEmptyReturnsUnderscore() {
        QVERIFY(sanitizeFilename("") == "_");
    }

    void testShellForbidden() {
        QVERIFY(sanitizeFilename("file<name") == "file_name");
        QVERIFY(sanitizeFilename("file>name") == "file_name");
        QVERIFY(sanitizeFilename("file:name") == "file_name");
        QVERIFY(sanitizeFilename("file\"name") == "file_name");
        QVERIFY(sanitizeFilename("file|name") == "file_name");
        QVERIFY(sanitizeFilename("file?name") == "file_name");
        QVERIFY(sanitizeFilename("file*name") == "file_name");
    }

    void testTrailingSpacesAndDots() {
        QVERIFY(sanitizeFilename("file..  ") == "file");
        QVERIFY(sanitizeFilename("..test..") == "..test");
    }

    void testMixedSpecialChars() {
        // The "/..../" component contains six unsafe characters: two
        // separators and four traversal dots, each mapped to one underscore.
        QVERIFY(sanitizeFilename("C:/Users/Test\\Documents/..../song*title?.mp3") == "C__Users_Test_Documents______song_title_.mp3");
    }

    void testTraversalSafety() {
        QVERIFY(sanitizeFilename("/\\*?") == "____");
        QVERIFY(sanitizeFilename("../../etc/passwd") == "______etc_passwd");
    }

    void testPreserveExtension() {
        QVERIFY(sanitizeFilename("my song.mp3") == "my song.mp3");
        QVERIFY(sanitizeFilename("my song (2).wav") == "my song (2).wav");
    }

    // ── T0034: the recursive walk is bounded ───────────────────────────────────
    //
    // The unbounded walk was not merely slow: on a symlink loop it never
    // returned, and this runs on the preset-refresh path, so the UI froze until
    // the process was killed.
    //
    // Note the extension set carries the leading dot (".m4a", matching
    // audioExtensions). Passing {"m4a"} matches nothing at all and every
    // assertion below fails with a count of 0 -- silently, since an empty
    // result is exactly what a broken scan looks like.

    void recursiveScanFindsFilesAtEveryDepth() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto root = vc::fs::path(dir.path().toStdString());
        // a/b/c/d -- four levels, all inside the default cap.
        QVERIFY(vc::fs::create_directories(root / "a" / "b" / "c" / "d"));
        for (const char* rel : {"top.m4a", "a/one.m4a", "a/b/two.m4a", "a/b/c/three.m4a",
                                 "a/b/c/d/four.m4a"}) {
            QFile f(QString::fromStdString((root / rel).string()));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("x");
        }
        const auto found = listFiles(root, {".m4a"}, true);
        QCOMPARE(found.size(), std::size_t(5));
    }

    void recursiveScanStopsAtTheDepthBound() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto root = vc::fs::path(dir.path().toStdString());
        QVERIFY(vc::fs::create_directories(root / "a" / "b" / "c" / "d" / "e" / "f"));
        for (const char* rel : {"a/b/c/inside.m4a", "a/b/c/d/e/f/outside.m4a"}) {
            QFile f(QString::fromStdString((root / rel).string()));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("x");
        }
        // maxDepth 3 keeps a/b/c and refuses a/b/c/d/e/f.
        const auto found = listFiles(root, {".m4a"}, true, 3);
        QCOMPARE(found.size(), std::size_t(1));
        QVERIFY(found.front().string().find("inside.m4a") != std::string::npos);
    }

    void recursiveScanStopsAtTheEntryBound() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto root = vc::fs::path(dir.path().toStdString());
        for (int i = 0; i < 10; ++i) {
            QFile f(QString::fromStdString(
                    (root / ("f" + std::to_string(i) + ".m4a")).string()));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("x");
        }
        // Breadth bound counts entries VISITED, not matches.
        const auto found = listFiles(root, {".m4a"}, true, 16, 4);
        QCOMPARE(found.size(), std::size_t(4));
    }

    void nonRecursiveScanIgnoresTheDepthBound() {
        // The cap must not change the meaning of recursive=false, which was
        // never the unbounded path and never needed one.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto root = vc::fs::path(dir.path().toStdString());
        QVERIFY(vc::fs::create_directories(root / "a"));
        QFile top(QString::fromStdString((root / "top.m4a").string()));
        QVERIFY(top.open(QIODevice::WriteOnly));
        top.write("x");
        QCOMPARE(listFiles(root, {".m4a"}, false, 1).size(), std::size_t(1));
    }

    void missingDirectoryReturnsEmpty() {
        const auto found = listFiles("/nonexistent-chadvis-preset-dir", {".m4a"}, true);
        QVERIFY(found.empty());
    }
};

int runTestFileUtils(int argc, char** argv) {
    Q_UNUSED(argc);
    Q_UNUSED(argv);
    TestFileUtils test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_FileUtils.moc"