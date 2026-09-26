#include <QtTest>
#include <QTemporaryDir>

#include "audio/AudioQueue.hpp"
#include "core/Config.hpp"
#include "recorder/EncoderSettings.hpp"
#include "recorder/FrameGrabber.hpp"
#include "recorder/VideoRecorderCore.hpp"

#include <array>
#include <utility>
#include <vector>

using namespace vc;

namespace {

EncoderSettings testSettings(const QString& outputPath,
                              VideoCodec videoCodec = VideoCodec::H264,
                              AudioCodec audioCodec = AudioCodec::AAC,
                              Container container = Container::MP4) {
    EncoderSettings settings;
    settings.outputPath = fs::path(outputPath.toStdString());
    settings.video.codec = videoCodec;
    settings.video.width = 32;
    settings.video.height = 32;
    settings.video.fps = 30;
    settings.video.crf = 0;
    settings.video.preset = EncoderPreset::Ultrafast;
    settings.audio.codec = audioCodec;
    settings.audio.bitrate = 64;
    settings.audio.sampleRate = 48000;
    settings.audio.channels = 2;
    settings.container = container;
    return settings;
}

bool pushAudio(AudioQueue& queue) {
    std::array<float, 4096 * 2> samples{};
    samples.fill(0.1f);
    return queue.pushAll(samples.data(), 4096, 2, 48000);
}

} // namespace

class TestRecorder : public QObject {
    Q_OBJECT

private slots:
    // glReadPixels hands rows over bottom-up and the encoder uploads them in
    // the order they arrive, so this row reversal is the whole reason an
    // encoded frame is not upside down. Pinned here because it is a one-line
    // routine that is trivially "optimized" into nothing.
    void flipImageReversesRows() {
        constexpr u32 width = 3;
        constexpr u32 height = 4;
        std::vector<u8> pixels(static_cast<usize>(width) * height * 4);
        for (u32 y = 0; y < height; ++y) {
            for (u32 x = 0; x < width; ++x) {
                pixels[(y * width + x) * 4] = static_cast<u8>(y + 1);
            }
        }

        FrameGrabber::flipImage(pixels, width, height);

        for (u32 y = 0; y < height; ++y) {
            for (u32 x = 0; x < width; ++x) {
                QCOMPARE(pixels[(y * width + x) * 4],
                         static_cast<u8>(height - y));
            }
        }
    }

    void flipImageLeavesUndersizedBuffersAlone() {
        // A short buffer would read past its end; the guard is what keeps a
        // malformed frame from turning into memory corruption.
        std::vector<u8> pixels(8, 7);
        FrameGrabber::flipImage(pixels, 64, 64);
        QCOMPARE(pixels.size(), static_cast<usize>(8));
        QCOMPARE(pixels.front(), static_cast<u8>(7));
        QCOMPARE(pixels.back(), static_cast<u8>(7));
    }

    void flipImageIsItsOwnInverse() {
        constexpr u32 width = 5;
        constexpr u32 height = 7;
        std::vector<u8> original(static_cast<usize>(width) * height * 4);
        for (usize i = 0; i < original.size(); ++i)
            original[i] = static_cast<u8>((i * 31) % 251);

        std::vector<u8> pixels = original;
        FrameGrabber::flipImage(pixels, width, height);
        QVERIFY(pixels != original);
        FrameGrabber::flipImage(pixels, width, height);
        QCOMPARE(pixels, original);
    }

    void frameSubmissionsReachEncoder() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const auto outputPath = QString::fromStdString(
            (fs::path(dir.path().toStdString()) / "frames.mp4").string());
        VideoRecorder recorder;
        auto started = recorder.start(testSettings(outputPath));
        QVERIFY(started);

        constexpr u32 frameCount = 8;
        for (u32 i = 0; i < frameCount; ++i) {
            std::vector<u8> frame(32 * 32 * 4, static_cast<u8>(i * 17));
            recorder.submitVideoFrame(std::move(frame), 32, 32,
                                      static_cast<i64>(i) * 1000);
        }

        QTRY_VERIFY_WITH_TIMEOUT(
            recorder.getCurrentStats().framesWritten >= frameCount, 5000);
        const auto progress = recorder.getCurrentStats();
        QVERIFY(progress.framesWritten >= frameCount);
        (void)recorder.stop();
    }

    void audioQueueBeforeWorkerIsConsumed() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const auto outputPath = QString::fromStdString(
            (fs::path(dir.path().toStdString()) / "audio-before.mkv").string());
        AudioQueue queue;
        VideoRecorder recorder;
        recorder.setAudioQueue(&queue); // attachment before worker creation
        auto started = recorder.start(testSettings(
            outputPath, VideoCodec::FFV1, AudioCodec::AAC, Container::MKV));
        QVERIFY(started);
        QVERIFY(pushAudio(queue));

        QTRY_VERIFY_WITH_TIMEOUT(queue.recDepth() == 0, 3000);
        (void)recorder.stop();
    }

    void audioQueueAfterWorkerIsConsumed() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const auto outputPath = QString::fromStdString(
            (fs::path(dir.path().toStdString()) / "audio-after.mkv").string());
        AudioQueue queue;
        VideoRecorder recorder;
        auto started = recorder.start(testSettings(
            outputPath, VideoCodec::FFV1, AudioCodec::AAC, Container::MKV));
        QVERIFY(started);
        recorder.setAudioQueue(&queue); // attachment after worker creation
        QVERIFY(pushAudio(queue));

        QTRY_VERIFY_WITH_TIMEOUT(queue.recDepth() == 0, 3000);
        (void)recorder.stop();
    }

    void configBuildsSanitizedContainerPath() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const auto original = Config::instance().recording();
        auto& recording = Config::instance().recording();
        recording.outputDirectory = fs::path(dir.path().toStdString());
        recording.defaultFilename = "capture.v2/{date}_{time}.mp4";
        recording.container = "mkv";
        recording.video.codec = "libx265";
        recording.video.crf = 17;

        const auto settings = EncoderSettings::fromConfig();
        const auto selectedPath = EncoderSettings::outputPathForContainer(
            "capture.mp4", Container::MKV);
        const auto filename = settings.outputPath.filename().string();
        const bool hasPathSeparator =
            filename.find('/') != std::string::npos ||
            filename.find('\\') != std::string::npos;
        const bool basenamePreserved =
            filename.find("capture.v2_") != std::string::npos;
        const bool extensionMatches =
            settings.outputPath.extension() == ".mkv";
        const bool selectedExtensionMatches =
            selectedPath.extension() == ".mkv";
        const bool codecWasApplied =
            settings.video.codec == VideoCodec::H265;
        const bool crfWasApplied = settings.video.crf == 17;

        Config::instance().recording() = original;
        QVERIFY(!settings.outputPath.empty());
        QVERIFY(!hasPathSeparator);
        QVERIFY(basenamePreserved);
        QVERIFY(extensionMatches);
        QVERIFY(selectedExtensionMatches);
        QVERIFY(codecWasApplied);
        QVERIFY(crfWasApplied);
    }
};

int runTestRecorder(int argc, char** argv) {
    TestRecorder test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_RecordingPipeline.moc"
