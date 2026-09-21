#include <QtTest>
#include "util/FileUtils.hpp"

using namespace vc::file;

class TestFileUtils : public QObject {
    Q_OBJECT

private slots:
    void testBasicSeparators() {
        QVERIFY(sanitizeFilename("foo/bar\\baz.mp3") == "foo_bar_baz.mp3");
    }

    void testControlChars() {
        QVERIFY(sanitizeFilename(std::string{"test\x01\x02file"}) == "test__file");
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
        QVERIFY(sanitizeFilename("P.R.N") == "P_R_N"); // dot doesn't make reserved
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
        QVERIFY(sanitizeFilename("..test..") == "test");
    }

    void testMixedSpecialChars() {
        QVERIFY(sanitizeFilename("C:/Users/Test\\Documents/..../song*title?.mp3") == "C__Users_Test_Documents____song_title_.mp3");
    }

    void testPreserveExtension() {
        QVERIFY(sanitizeFilename("my song.mp3") == "my song.mp3");
        QVERIFY(sanitizeFilename("my song (2).wav") == "my song (2).wav");
    }
};

int runTestFileUtils(int argc, char** argv) {
    Q_UNUSED(argc);
    Q_UNUSED(argv);
    TestFileUtils test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_FileUtils.moc"