#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>
#include <utility>

#include "audio/AudioEngine.hpp"
#include "core/Config.hpp"
#include "lyrics/LyricsData.hpp"
#include "lyrics/LyricsSync.hpp"
#include "qml_bridge/LyricsBridge.hpp"
#include "suno/ClipParser.hpp"
#include "suno/SunoDatabase.hpp"
#include "suno/SunoDownloader.hpp"
#include "suno/SunoLyrics.hpp"

using namespace vc;
using namespace vc::suno;

namespace {

SunoClip makeClip()
{
    SunoClip clip;
    clip.id = "clip-1";
    clip.title = "Captured Song";
    clip.display_name = "Captured Artist";
    clip.metadata.prompt = "style prompt words";
    clip.metadata.lyrics = "[Verse]\nhello world";
    return clip;
}

QJsonObject capturedClipObject()
{
    return QJsonDocument::fromJson(QByteArray(R"({
        "id": "clip-1",
        "title": "Captured Song",
        "display_name": "Captured Artist",
        "metadata": {
            "prompt": "style prompt words",
            "lyrics": "[Verse]\nhello world"
        }
    })"))
        .object();
}

LyricsData makeLyrics()
{
    LyricsData data;
    data.source = "suno";
    data.title = "Test";
    data.artist = "Artist";
    data.isSynced = true;

    LyricsLine first;
    first.text = "hello";
    first.startTime = 0.0f;
    first.endTime = 1.0f;
    first.isSynced = true;
    first.words.push_back({"hello", 0.0f, 0.5f, 1.0f});
    first.words.push_back({"world", 0.5f, 1.0f, 1.0f});

    LyricsLine second;
    second.text = "again";
    second.startTime = 1.0f;
    second.endTime = 2.0f;
    second.isSynced = true;
    second.words.push_back({"again", 1.0f, 2.0f, 1.0f});

    data.lines.push_back(first);
    data.lines.push_back(second);
    return data;
}

class DownloadPathGuard
{
public:
    explicit DownloadPathGuard(fs::path path)
        : path_(std::move(path))
    {
    }

    ~DownloadPathGuard()
    {
        CONFIG.suno().downloadPath = path_;
    }

private:
    fs::path path_;
};

}

class TestLyricsPipeline : public QObject
{
    Q_OBJECT

private slots:
    void capturedFieldsAndMetadataLyrics()
    {
        const auto parsed = ClipParser::parseClip(capturedClipObject());
        QVERIFY(parsed.has_value());
        QCOMPARE(QString::fromStdString(parsed->metadata.lyrics),
                 QStringLiteral("[Verse]\nhello world"));
        QCOMPARE(QString::fromStdString(parsed->metadata.prompt),
                 QStringLiteral("style prompt words"));

        const auto clip = makeClip();
        const auto json = R"({
            "aligned_words": [
                {"word":"hello","start_s":0.0,"end_s":0.5,"p_align":0.93,"success":true},
                {"word":"world","start_s":0.5,"end_s":1.0,"p_align":0.81,"success":true}
            ]
        })";
        const auto lyrics = LyricsAligner::parseCapturedSunoLyrics(clip, json);
        QVERIFY(lyrics.has_value());
        QCOMPARE(lyrics->songId, std::string("clip-1"));
        QCOMPARE(lyrics->title, std::string("Captured Song"));
        QCOMPARE(lyrics->artist, std::string("Captured Artist"));
        QCOMPARE(lyrics->source, std::string("suno"));
        QVERIFY(lyrics->isSynced);
        QCOMPARE(lyrics->lines.size(), size_t{2});
        QCOMPARE(lyrics->lines[0].text, std::string("[Verse]"));
        QVERIFY(lyrics->lines[0].isInstrumental);
        QCOMPARE(lyrics->lines[1].text, std::string("hello world"));
        QCOMPARE(lyrics->lines[1].words.size(), size_t{2});
        QCOMPARE(lyrics->lines[1].words[0].confidence, 0.93f);
    }

    void invalidCapturedPayloadIsRejected()
    {
        const auto clip = makeClip();
        QVERIFY(!LyricsAligner::parseCapturedSunoLyrics(
            R"({"aligned_words":[{"word":"hello","start_s":0.0}]})", clip));
        QVERIFY(!LyricsAligner::parseCapturedSunoLyrics(
            R"({"aligned_words":[{"word":"hello","start_s":1.0,"end_s":0.5}]})", clip));
        QVERIFY(!LyricsAligner::parseCapturedSunoLyrics(
            R"({"aligned_words":[{"word":"hello","start_s":"bad","end_s":1.0}]})", clip));
    }

    void syncSelectsLineAndClampsProgress()
    {
        LyricsSync sync(nullptr);
        sync.loadLyrics(makeLyrics());
        QCOMPARE(sync.getState(), LyricsSyncState::Ready);

        sync.seek(0.75f);
        QCOMPARE(sync.getPosition().lineIndex, 0);
        QCOMPARE(sync.getPosition().wordIndex, 1);
        QVERIFY(std::abs(sync.getPosition().lineProgress - 0.75f) < 0.001f);

        sync.seek(3.0f);
        QCOMPARE(sync.getPosition().lineIndex, 1);
        QCOMPARE(sync.getPosition().lineProgress, 1.0f);
        QCOMPARE(sync.getPosition().wordProgress, 0.0f);
    }

    void emptyLoadIsTerminal()
    {
        LyricsSync sync(nullptr);
        sync.loadLyrics(LyricsData{});
        QCOMPARE(sync.getState(), LyricsSyncState::Error);
        sync.seek(1.0f);
        QCOMPARE(sync.getState(), LyricsSyncState::Error);
    }

    void downloaderSignalsAfterExistingFileJumps()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto filePath = directory.filePath(QStringLiteral("CapturedClip.mp3"));
        QFile file(filePath);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();

        DownloadPathGuard pathGuard(CONFIG.suno().downloadPath);
        CONFIG.suno().downloadPath = directory.path().toStdString();

        QTemporaryDir sessionDirectory;
        QVERIFY(sessionDirectory.isValid());
        AudioEngine audio(
            fs::path(sessionDirectory.path().toStdString()) / "last_session.m3u");
        QVERIFY(audio.init());
        SunoDatabase database;
        QVERIFY(database.init(":memory:"));
        auto* network = new QNetworkAccessManager;
        SunoDownloader downloader(nullptr, database, &audio, network);

        QSignalSpy ready(&downloader, &SunoDownloader::playbackReady);
        bool currentAtReady = false;
        QObject::connect(&downloader, &SunoDownloader::playbackReady,
                         &downloader, [&currentAtReady, &audio](const QString&) {
                             currentAtReady = audio.playlist().currentItem() != nullptr;
                         });

        SunoClip clip;
        clip.id = "clip-ready";
        clip.title = "CapturedClip";
        clip.status = "complete";
        clip.media_urls = {{"https://audiopipe.suno.ai/clip-ready.mp3", "mp3",
                             "streaming", ""}};
        downloader.downloadAndPlay(clip);

        QCOMPARE(ready.count(), 1);
        QVERIFY(currentAtReady);
        QCOMPARE(ready.takeFirst().at(0).toString(), QStringLiteral("clip-ready"));
    }

    void searchAndExportsOwnedLyrics()
    {
        LyricsSync sync(nullptr);
        qml_bridge::LyricsBridge::setLyricsSync(&sync);
        qml_bridge::LyricsBridge bridge;
        sync.loadLyrics(makeLyrics());
        sync.seek(0.5f);

        bridge.setSearchQuery(QStringLiteral("hello"));
        QCOMPARE(bridge.searchResults().size(), 1);
        QCOMPARE(bridge.getUpcomingLines(1).size(), 1);
        QCOMPARE(bridge.getContextLines(1, 1).size(), 2);
        QCOMPARE(bridge.getLine(0).value(QStringLiteral("text")).toString(),
                 QStringLiteral("hello"));

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString srtPath = directory.filePath(QStringLiteral("lyrics.srt"));
        const QString lrcPath = directory.filePath(QStringLiteral("lyrics.lrc"));
        QSignalSpy finished(&bridge, &qml_bridge::LyricsBridge::exportFinished);
        QSignalSpy failed(&bridge, &qml_bridge::LyricsBridge::exportFailed);

        bridge.exportToSrt(srtPath);
        bridge.exportToLrc(lrcPath);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 2, 2000);
        QCOMPARE(failed.count(), 0);

        QFile srt(srtPath);
        QVERIFY(srt.open(QIODevice::ReadOnly));
        const QByteArray srtContents = srt.readAll();
        QVERIFY(srtContents.contains("00:00:00,000 --> 00:00:01,000"));
        QVERIFY(srtContents.contains("hello"));

        QFile lrc(lrcPath);
        QVERIFY(lrc.open(QIODevice::ReadOnly));
        const QByteArray lrcContents = lrc.readAll();
        QVERIFY(lrcContents.contains("[ti:Test]"));
        QVERIFY(lrcContents.contains("[00:00.00]hello"));

        bridge.exportToSrt(QString());
        QCOMPARE(finished.count(), 2);
        QCOMPARE(failed.count(), 1);
    }

    void lazyBridgeReceivesUpdates()
    {
        LyricsSync sync(nullptr);
        qml_bridge::LyricsBridge::setLyricsSync(&sync);
        {
            qml_bridge::LyricsBridge bridge;
            QSignalSpy lyricsSpy(&bridge, &qml_bridge::LyricsBridge::lyricsChanged);
            QSignalSpy positionSpy(&bridge, &qml_bridge::LyricsBridge::positionChanged);

            sync.loadLyrics(makeLyrics());
            sync.seek(0.75f);

            QVERIFY(bridge.hasLyrics());
            QCOMPARE(bridge.currentLineIndex(), 0);
            QCOMPARE(bridge.currentWordIndex(), 1);
            QVERIFY(lyricsSpy.count() > 0);
            QVERIFY(positionSpy.count() > 0);
        }
        QVERIFY(!qml_bridge::LyricsBridge::instance());
    }
};

int runTestLyricsPipeline(int argc, char** argv)
{
    TestLyricsPipeline test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_LyricsPipeline.moc"
