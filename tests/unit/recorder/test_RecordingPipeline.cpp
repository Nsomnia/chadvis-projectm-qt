#include <QtTest>
#include <QTemporaryDir>

#include "audio/AudioQueue.hpp"
#include "core/Config.hpp"
#include "recorder/EncoderSettings.hpp"
#include "recorder/FFmpegUtils.hpp"
#include "recorder/FrameGrabber.hpp"
#include "recorder/VideoRecorderCore.hpp"

#include <array>
#include <fstream>
#include <type_traits>
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

// A minimal but genuine 8 kHz mono 8-bit PCM WAV, so the reader tests open a
// real demuxer rather than a hand-poked context. 4 KiB of payload keeps the
// format probe and find_stream_info both confident.
bool writeTestWav(const fs::path& path) {
    const std::vector<u8> data(4096, 0x40);

    const auto put32 = [](std::ostream& os, u32 value) {
        const char bytes[4] = {static_cast<char>(value & 0xFFu),
                               static_cast<char>((value >> 8) & 0xFFu),
                               static_cast<char>((value >> 16) & 0xFFu),
                               static_cast<char>((value >> 24) & 0xFFu)};
        os.write(bytes, 4);
    };
    const auto put16 = [](std::ostream& os, u16 value) {
        const char bytes[2] = {static_cast<char>(value & 0xFFu),
                               static_cast<char>((value >> 8) & 0xFFu)};
        os.write(bytes, 2);
    };

    std::ofstream out(path, std::ios::binary);
    if (!out) return false;

    out.write("RIFF", 4);
    put32(out, 36u + static_cast<u32>(data.size()));
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    put32(out, 16u);            // PCM fmt chunk size
    put16(out, 1u);             // format = PCM
    put16(out, 1u);             // channels
    put32(out, 8000u);          // sample rate
    put32(out, 8000u);          // byte rate = rate * channels * bits / 8
    put16(out, 1u);             // block align
    put16(out, 8u);             // bits per sample
    out.write("data", 4);
    put32(out, static_cast<u32>(data.size()));
    out.write(reinterpret_cast<const char*>(data.data()),
              static_cast<std::streamsize>(data.size()));
    return out.good();
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

    // ── AVFormatContext ownership ───────────────────────────────────────────
    // A demuxer context has oformat == nullptr. The single shared deleter read
    // c->oformat->flags to decide whether to close pb, so every read path
    // segfaulted at 0x2c the moment the unique_ptr released its context. The
    // fix is in the types, so the guard that matters is a compile-time one: a
    // reader and a writer must not be interchangeable in either direction.

    void formatContextReaderAndWriterAreDistinctTypes() {
        static_assert(!std::is_same_v<AVFormatContextInPtr, AVFormatContextOutPtr>);
        static_assert(!std::is_same_v<AVFormatContextInDeleter,
                                      AVFormatContextOutDeleter>);
        // is_convertible is the check that actually rejects the mistake. Two
        // unique_ptrs over the same pointer are different types either way;
        // what must fail to compile is `out = in` and `in = out`.
        static_assert(!std::is_convertible_v<AVFormatContextInPtr,
                                             AVFormatContextOutPtr>);
        static_assert(!std::is_convertible_v<AVFormatContextOutPtr,
                                             AVFormatContextInPtr>);
        // Both still own the same C type, so a .get() is usable either way.
        static_assert(std::is_same_v<AVFormatContextInPtr::element_type,
                                     AVFormatContext>);
        static_assert(std::is_same_v<AVFormatContextOutPtr::element_type,
                                     AVFormatContext>);

        // Both aliases are usable as owning pointers right now. Constructing
        // each from its own allocation is the runtime half of the same claim.
        AVFormatContextInPtr in(avformat_alloc_context());
        AVFormatContextOutPtr out(avformat_alloc_context());
        QVERIFY(in != nullptr);
        QVERIFY(out != nullptr);
        QVERIFY(in->pb == nullptr);
        QVERIFY(out->pb == nullptr);
        in.reset();
        out.reset();
    }

    void inputAliasClosesADemuxerContext() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const auto path = fs::path(dir.path().toStdString()) / "demux-probe.wav";
        QVERIFY(writeTestWav(path));
        const auto pathStr = path.string();

        AVFormatContext* raw = nullptr;
        QCOMPARE(avformat_open_input(&raw, pathStr.c_str(), nullptr, nullptr), 0);
        QVERIFY(raw != nullptr);
        // The exact state that killed the old deleter: a demuxer has an
        // iformat and no oformat.
        QVERIFY(raw->iformat != nullptr);
        QVERIFY(raw->oformat == nullptr);

        i32 packetsRead = 0;
        {
            AVFormatContextInPtr format(raw);
            QVERIFY(format);
            // lavf opened this and owns it, so the close has to release it.
            QVERIFY(format->pb != nullptr);
            QVERIFY(avformat_find_stream_info(format.get(), nullptr) >= 0);
            QCOMPARE(av_find_best_stream(format.get(), AVMEDIA_TYPE_AUDIO, -1, -1,
                                         nullptr, 0),
                     0);

            AVPacketPtr packet(av_packet_alloc());
            QVERIFY(packet);
            QCOMPARE(av_read_frame(format.get(), packet.get()), 0);
            av_packet_unref(packet.get());
            packetsRead = 1;
        } // releases here; the old deleter dereferenced oformat and crashed

        QCOMPARE(packetsRead, 1);
    }

    void inputAliasReleasesASelfAllocatedContext() {
        // A context with no iformat and no pb is still a context we own.
        // avformat_close_input skips the read_close hook and calls
        // avio_close(nullptr), so this is a clean avformat_free_context --
        // the input alias must not require a demuxer to be safe.
        {
            AVFormatContextInPtr context(avformat_alloc_context());
            QVERIFY(context);
            QVERIFY(context->iformat == nullptr);
            QVERIFY(context->pb == nullptr);
        }
        // Default construction and a reset with nothing to do are no-ops, which
        // is what every optional-path early return in the recorder relies on.
        AVFormatContextInPtr empty;
        empty.reset();
        QVERIFY(empty == nullptr);
    }

    void outputAliasClosesTheIOContextItOwns() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const auto path = fs::path(dir.path().toStdString()) / "mux-close.wav";
        const auto pathStr = path.string();

        AVFormatContext* raw = nullptr;
        QCOMPARE(avformat_alloc_output_context2(&raw, nullptr, nullptr,
                                                pathStr.c_str()),
                 0);
        QVERIFY(raw != nullptr);
        QVERIFY(raw->oformat != nullptr);
        // Not an AVFMT_NOFILE muxer, so the caller owns pb -- exactly the
        // production shape from VideoRecorderFFmpeg::init.
        QVERIFY((raw->oformat->flags & AVFMT_NOFILE) == 0);
        QCOMPARE(avio_open(&raw->pb, pathStr.c_str(), AVIO_FLAG_WRITE), 0);
        QVERIFY(raw->pb != nullptr);

        {
            AVFormatContextOutPtr format(raw);
            QVERIFY(format);
            const u8 payload[512] = {};
            avio_write(format->pb, payload, static_cast<int>(sizeof(payload)));
        } // releases here: close pb, then free the context

        // 512 bytes stay inside the 32 KiB AVIOContext buffer until it is
        // closed, so a non-empty file is the observable proof that the deleter
        // closed pb instead of leaking it. FFmpeg has no public allocation
        // counter, so this is the release assertion that actually observes
        // something.
        QVERIFY(fs::file_size(path) > 0);
    }

    void outputAliasLeavesACustomIOContextToItsOwner() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const auto path = fs::path(dir.path().toStdString()) / "custom-io.wav";
        const auto pathStr = path.string();

        AVFormatContext* raw = nullptr;
        QCOMPARE(avformat_alloc_output_context2(&raw, nullptr, nullptr,
                                                pathStr.c_str()),
                 0);
        QVERIFY(raw != nullptr);
        // Held separately because after the context dies we still have to
        // close this ourselves -- that is the whole point of the flag.
        AVIOContext* owned = nullptr;
        QCOMPARE(avio_open(&owned, pathStr.c_str(), AVIO_FLAG_WRITE), 0);
        QVERIFY(owned != nullptr);
        raw->pb = owned;
        // "The caller has supplied a custom AVIOContext, don't avio_close() it."
        // The old check tested oformat->flags instead, which describes the
        // muxer and cannot see this, so it closed a pb it did not own.
        raw->flags |= AVFMT_FLAG_CUSTOM_IO;

        {
            AVFormatContextOutPtr format(raw);
            const u8 payload[512] = {};
            avio_write(format->pb, payload, static_cast<int>(sizeof(payload)));
        }

        // Still zero bytes: the deleter honoured the flag and left the buffer
        // unflushed, which is only possible if the AVIOContext survived.
        QVERIFY(fs::file_size(path) == 0);

        // And the survivor is a usable, live AVIOContext -- the caller closes
        // it and the bytes land, closing the loop with no leak.
        avio_closep(&owned);
        QVERIFY(owned == nullptr);
        QVERIFY(fs::file_size(path) > 0);
    }
};

int runTestRecorder(int argc, char** argv) {
    TestRecorder test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_RecordingPipeline.moc"
