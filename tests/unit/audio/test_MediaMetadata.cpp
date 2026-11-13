#include <QtTest>

#include "audio/analysis/MediaMetadata.hpp"

#include <QString>

using namespace vc;

class TestMediaMetadata : public QObject {
    Q_OBJECT

private slots:
    void expandsEachPlaceholder() {
        MediaMetadata meta;
        meta.title = "Song";
        meta.artist = "Artist";
        meta.album = "Album";
        meta.genre = "Rock";
        meta.year = 2026;
        meta.trackNumber = 7;
        meta.duration = vc::Duration{65000};
        meta.bitrate = 320;

        QCOMPARE(QString::fromStdString(meta.formatLine("{title} - {artist} [{album}]")),
                 QStringLiteral("Song - Artist [Album]"));
        QCOMPARE(QString::fromStdString(meta.formatLine("{genre} {year} {track} {duration} {bitrate}")),
                 QStringLiteral("Rock 2026 7 01:05 320 kbps"));
    }

    void unknownPlaceholderStaysLiteral() {
        MediaMetadata meta;
        QCOMPARE(QString::fromStdString(meta.formatLine("{unknown} {title}")),
                 QStringLiteral("{unknown} Unknown Title"));
    }

    void repeatedPlaceholderExpandsEachOccurrence() {
        MediaMetadata meta;
        meta.title = "X";
        QCOMPARE(QString::fromStdString(meta.formatLine("{title}/{title}/{title}")),
                 QStringLiteral("X/X/X"));
    }

    void injectedValueIsNotReexpanded() {
        MediaMetadata meta;
        meta.title = "{artist}";
        meta.artist = "{title}";
        QCOMPARE(QString::fromStdString(meta.formatLine("{title}")),
                 QStringLiteral("{artist}"));
        QCOMPARE(QString::fromStdString(meta.formatLine("{artist}")),
                 QStringLiteral("{title}"));
    }

    void selfReferentialTokenTerminates() {
        // The old while(find) loop re-inserted the token on every replacement
        // and never returned for this input.
        MediaMetadata meta;
        meta.title = "{title}";
        QCOMPARE(QString::fromStdString(meta.formatLine("{title}")),
                 QStringLiteral("{title}"));
    }

    void emptyNumericValuesProduceEmptySlots() {
        MediaMetadata meta;
        QCOMPARE(QString::fromStdString(meta.formatLine("{year}|{track}")),
                 QStringLiteral("|"));
    }
};

int runTestMediaMetadata(int argc, char** argv) {
    Q_UNUSED(argc);
    Q_UNUSED(argv);
    TestMediaMetadata test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_MediaMetadata.moc"
