#include <QtTest>
#include <QTemporaryDir>

#include "audio/AudioQueue.hpp"
#include "core/Config.hpp"
#include "recorder/EncoderSettings.hpp"
#include "recorder/FFmpegUtils.hpp"
#include "recorder/FrameGrabber.hpp"
#include "recorder/VideoRecorderCore.hpp"
#include "recorder/VideoRecorderFFmpeg.hpp"

#include <algorithm>
#include <array>
#include <cmath>
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

// ── Presentation-timeline fixtures ──────────────────────────────────────────
//
// The timeline tests drive VideoRecorderFFmpeg directly instead of going
// through VideoRecorder's worker thread, for two load-bearing reasons. A worker
// pops one frame per loop iteration, so a *deliberately* dropped frame could
// not be positioned deterministically. And the drop being modelled is the one
// FrameGrabber performs before the encoder ever sees the frame -- which is
// reproduced exactly by simply not calling encodeVideo for it, with no other
// variable in play.

constexpr u32 kTimelineFps = 30;
constexpr f64 kTick = 1.0 / kTimelineFps;
constexpr i64 kIntervalUs = 1000000 / kTimelineFps; // 33333 us
// A steady_clock reading on a machine that has been up four hours. The point of
// a large origin is that an un-normalised PTS cannot be mistaken for a
// normalised one: this would become a start time of 14400 s.
constexpr i64 kOriginUs = 4LL * 60 * 60 * 1000000;

// Quarter-tick tolerance. The only rounding in play is the nearest-integer
// rescale of a capture time onto the frame grid, which is wrong by at most half
// a tick per frame; the 1e6/30 interval truncation adds 0.33 us per frame,
// about 1e-5 of a tick, and does not accumulate into anything observable here.
constexpr f64 kTickTolerance = 0.25 * kTick;

EncoderSettings timelineSettings(const QString& outputPath) {
    EncoderSettings settings = testSettings(outputPath);
    // B-frames off so the finished file carries no edit list and no
    // decode-order reshuffle to reason about. The code under test -- the PTS
    // assignment in encodeVideo -- takes the identical path either way; this
    // only makes the read-back observation unambiguous.
    settings.video.bFrames = 0;
    return settings;
}

// Encode one frame per entry in captureUs, in order. Returns the path actually
// written, which init may have renamed to dodge an existing file.
bool encodeAtCaptureTimes(const fs::path& requested,
                          const std::vector<i64>& captureUs,
                          fs::path& actualOut) {
    VideoRecorderFFmpeg encoder;
    const auto settings =
        timelineSettings(QString::fromStdString(requested.string()));
    if (auto started = encoder.init(settings); !started) return false;
    actualOut = fs::path(encoder.getOutputPath());

    u64 bytesWritten = 0;
    for (usize i = 0; i < captureUs.size(); ++i) {
        GrabbedFrame frame;
        frame.width = 32;
        frame.height = 32;
        frame.timestamp = captureUs[i];
        frame.data = std::vector<u8>(32 * 32 * 4, static_cast<u8>(i * 11));
        if (!encoder.encodeVideo(frame, bytesWritten)) return false;
    }
    encoder.flush(bytesWritten);
    encoder.cleanup();
    return true;
}

struct EncodedTimeline {
    // Presentation times in seconds, ascending. This is the observation the
    // whole change rests on: a seam would only prove the arithmetic, not the
    // file a player actually gets.
    std::vector<f64> presentationSeconds;
    // The same times as read, before sorting -- lets a test assert the muxer's
    // own decode ordering was monotonic too.
    std::vector<f64> readOrderSeconds;
    // Sum of the samples' own durations, i.e. the container's video duration.
    f64 durationSeconds{0.0};
    f64 timeBaseSeconds{0.0};
};

bool readVideoTimeline(const fs::path& path, EncodedTimeline& out) {
    const std::string pathStr = path.string();
    AVFormatContext* raw = nullptr;
    if (avformat_open_input(&raw, pathStr.c_str(), nullptr, nullptr) < 0)
        return false;

    AVFormatContextInPtr format(raw);
    if (!format) return false;
    if (avformat_find_stream_info(format.get(), nullptr) < 0) return false;

    const int index = av_find_best_stream(format.get(), AVMEDIA_TYPE_VIDEO, -1,
                                          -1, nullptr, 0);
    if (index < 0) return false;
    AVStream* stream = format->streams[index];
    // Spelled out rather than av_q2d, which has moved headers between FFmpeg
    // releases; this is the same division.
    out.timeBaseSeconds = static_cast<f64>(stream->time_base.num) /
                          static_cast<f64>(stream->time_base.den);

    AVPacketPtr packet(av_packet_alloc());
    if (!packet) return false;

    while (av_read_frame(format.get(), packet.get()) >= 0) {
        if (packet->stream_index == index &&
            packet->pts != AV_NOPTS_VALUE) {
            const f64 seconds = packet->pts * out.timeBaseSeconds;
            out.presentationSeconds.push_back(seconds);
            out.readOrderSeconds.push_back(seconds);
            if (packet->duration > 0)
                out.durationSeconds += packet->duration * out.timeBaseSeconds;
        }
        av_packet_unref(packet.get());
    }

    std::sort(out.presentationSeconds.begin(), out.presentationSeconds.end());
    return !out.presentationSeconds.empty();
}

// The presentation time of each frame expressed in frame ticks, measured from
// the file's own first frame. Anchoring on front() rather than on absolute zero
// keeps the step pattern immune to any constant the container may apply to
// every timestamp; whether that constant is zero is a separate claim, asserted
// by uniformCaptureCadenceProducesOneTickStepsAndAZeroOrigin.
std::vector<i64> frameTickIndices(const EncodedTimeline& timeline) {
    const f64 origin = timeline.presentationSeconds.front();
    std::vector<i64> ticks;
    ticks.reserve(timeline.presentationSeconds.size());
    for (f64 seconds : timeline.presentationSeconds)
        ticks.push_back(static_cast<i64>(std::llround((seconds - origin) / kTick)));
    return ticks;
}

std::vector<i64> captureTimesEveryTick(i64 count, i64 skipIndex = -1) {
    std::vector<i64> times;
    for (i64 i = 0; i < count; ++i) {
        if (i == skipIndex) continue; // the frame that was dropped
        times.push_back(kOriginUs + i * kIntervalUs);
    }
    return times;
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

    // ── Presentation timeline ───────────────────────────────────────────────
    // The encoder used to assign pts = videoFrameCount_++, so every frame it
    // received got the next tick regardless of when that frame was actually
    // captured. A frame dropped by FrameGrabber therefore cost the file one
    // frame interval of real time silently: the surviving frames were
    // renumbered contiguously and the segment played faster than the song.
    // These tests read the finished file back and assert on the presentation
    // times that came out of it.

    void uniformCaptureCadenceProducesOneTickStepsAndAZeroOrigin() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        constexpr i64 frameCount = 12;
        const fs::path outDir(dir.path().toStdString());
        fs::path actual;
        QVERIFY(encodeAtCaptureTimes(outDir / "uniform.mp4",
                                     captureTimesEveryTick(frameCount), actual));

        EncodedTimeline timeline;
        QVERIFY(readVideoTimeline(actual, timeline));
        QCOMPARE(timeline.presentationSeconds.size(),
                 static_cast<usize>(frameCount));

        // A constant capture interval gives a constant PTS step.
        for (usize i = 1; i < timeline.presentationSeconds.size(); ++i) {
            const f64 step = timeline.presentationSeconds[i] -
                             timeline.presentationSeconds[i - 1];
            QVERIFY2(std::abs(step - kTick) < kTickTolerance,
                     qPrintable(QStringLiteral("step %1 s at frame %2")
                                    .arg(step)
                                    .arg(i)));
        }

        // The origin is normalised. The capture clock said four hours uptime;
        // a PTS derived from it without an origin would put the first frame
        // there, and the file's duration and seek index with it.
        QVERIFY2(std::abs(timeline.presentationSeconds.front()) <
                     kTickTolerance,
                 qPrintable(QStringLiteral("first presentation time is %1 s")
                                .arg(timeline.presentationSeconds.front())));

        // And the wall time the file spans is the wall time that was captured:
        // eleven intervals across for twelve frames.
        const f64 span = timeline.presentationSeconds.back() -
                         timeline.presentationSeconds.front();
        QVERIFY2(std::abs(span - (frameCount - 1) * kTick) < 0.5 * kTick,
                 qPrintable(QStringLiteral("span is %1 s").arg(span)));
    }

    void aDroppedCaptureFrameLeavesAGapInTheEncodedTimeline() {
        // The regression guard for this change. Frame 4 of 12 never reaches
        // encodeVideo -- exactly what FrameGrabber does when the queue is full
        // -- so the file must contain the other eleven frames spread over
        // *twelve* frame intervals, with the missing tick at index 4.
        //
        // Under the old counter this file contained eleven frames on ticks
        // 0..10: no gap, eleven intervals of span, and the segment played
        // faster than the audio it was recorded against.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        constexpr i64 frameCount = 12;
        constexpr i64 droppedIndex = 4;
        const fs::path outDir(dir.path().toStdString());
        fs::path actual;
        QVERIFY(encodeAtCaptureTimes(outDir / "gap.mp4",
                                     captureTimesEveryTick(frameCount, droppedIndex),
                                     actual));

        EncodedTimeline timeline;
        QVERIFY(readVideoTimeline(actual, timeline));
        QCOMPARE(timeline.presentationSeconds.size(),
                 static_cast<usize>(frameCount - 1));

        // Exact tick sequence: {0..11} minus the dropped frame's tick. This is
        // the whole claim -- the gap is in the right place, not just present.
        const auto ticks = frameTickIndices(timeline);
        QCOMPARE(ticks.size(), static_cast<usize>(frameCount - 1));
        i64 expected = 0;
        for (i64 i = 0; i < frameCount; ++i) {
            if (i == droppedIndex) continue;
            QVERIFY2(ticks[static_cast<usize>(expected)] == i,
                     qPrintable(QStringLiteral("frame index %1 landed on tick %2")
                                    .arg(i)
                                    .arg(ticks[static_cast<usize>(expected)])));
            ++expected;
        }

        // The step across the gap is two ticks; every other step is one. So
        // the file is one frame interval longer than its frame count, which is
        // the dropped frame's time accounted for rather than compressed away.
        const auto gapIndex = static_cast<usize>(droppedIndex);
        const f64 gapStep =
            timeline.presentationSeconds[gapIndex] -
            timeline.presentationSeconds[gapIndex - 1];
        QVERIFY2(std::abs(gapStep - 2 * kTick) < kTickTolerance,
                 qPrintable(QStringLiteral("step across the gap is %1 s")
                                .arg(gapStep)));

        const f64 span = timeline.presentationSeconds.back() -
                         timeline.presentationSeconds.front();
        QVERIFY2(std::abs(span - (frameCount - 1) * kTick) < 0.5 * kTick,
                 qPrintable(QStringLiteral("gap file spans %1 s").arg(span)));
    }

    void uniformCadenceLeavesTheFileDurationUnchanged() {
        // Deliberately a non-discrimination test. A counter and a capture-time
        // PTS agree exactly on a uniform input, so this cannot tell them apart
        // -- it exists to pin that the new arithmetic does not stretch or
        // compress an ordinary recording, which is the risk the gap test alone
        // would not catch.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        constexpr i64 frameCount = 20;
        const fs::path outDir(dir.path().toStdString());
        fs::path actual;
        QVERIFY(encodeAtCaptureTimes(outDir / "duration.mp4",
                                     captureTimesEveryTick(frameCount), actual));

        EncodedTimeline timeline;
        QVERIFY(readVideoTimeline(actual, timeline));
        QCOMPARE(timeline.presentationSeconds.size(),
                 static_cast<usize>(frameCount));

        // The container's own video duration, summed from the samples' stored
        // durations rather than from a packet count.
        QVERIFY2(std::abs(timeline.durationSeconds - frameCount * kTick) <
                     kTick,
                 qPrintable(QStringLiteral("duration is %1 s, expected %2 s")
                                .arg(timeline.durationSeconds)
                                .arg(frameCount * kTick)));
    }

    void nonIncreasingCaptureTimestampsStillGiveAMonotonicTimeline() {
        // A clock that repeats a tick, two instants inside the same frame
        // interval, and one that steps backwards. Every one of these rescales
        // to a tick at or before its predecessor, which a naive
        // "elapsed / interval" assignment would hand straight to libavcodec.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const std::vector<i64> capture = {
            kOriginUs,             // tick 0
            kOriginUs + 1000,      // 1 ms: rounds to tick 0 as well
            kOriginUs + 1000,      // identical reading
            kOriginUs,             // backwards to the origin
            kOriginUs + 2 * kIntervalUs, // tick 2, already behind the floor
            kOriginUs + 3 * kIntervalUs, // tick 3
        };

        fs::path actual;
        const fs::path outDir(dir.path().toStdString());
        QVERIFY(encodeAtCaptureTimes(outDir / "nonmonotonic.mp4", capture,
                                     actual));

        EncodedTimeline timeline;
        QVERIFY(readVideoTimeline(actual, timeline));

        // Every submitted frame survived, so nothing was dropped or refused on
        // the way through the encoder and the muxer.
        QCOMPARE(timeline.presentationSeconds.size(), capture.size());

        // Strictly increasing in presentation order...
        for (usize i = 1; i < timeline.presentationSeconds.size(); ++i) {
            QVERIFY2(timeline.presentationSeconds[i] >
                         timeline.presentationSeconds[i - 1],
                     qPrintable(QStringLiteral("pts %1 at %2 s is not above %3 s")
                                    .arg(i)
                                    .arg(timeline.presentationSeconds[i])
                                    .arg(timeline.presentationSeconds[i - 1])));
        }
        // ...and in the order the muxer wrote them, so the stream is not
        // merely sorted after the fact. bFrames is 0 for these fixtures, so
        // decode order and presentation order coincide.
        QVERIFY(std::is_sorted(timeline.readOrderSeconds.begin(),
                               timeline.readOrderSeconds.end()));
    }

    void frameGrabberDropsTheOldestFrameAndKeepsRealCaptureTimes() {
        // The precondition the gap test assumes: the drop discards a frame but
        // never rewrites the survivors' capture times, so the frame after a
        // drop still knows how much real time has passed. Without this the gap
        // test would be testing a fiction.
        FrameGrabber grabber;
        grabber.start();

        constexpr i64 submitted = 64;
        for (i64 i = 0; i < submitted; ++i) {
            GrabbedFrame frame;
            frame.width = 1;
            frame.height = 1;
            frame.timestamp = kOriginUs + i * kIntervalUs;
            grabber.pushFrame(std::move(frame));
        }

        const u32 dropped = grabber.droppedFrames();
        QVERIFY2(dropped > 0, "queue never overflowed, so nothing was dropped");
        QVERIFY(dropped < static_cast<u32>(submitted));

        GrabbedFrame frame;
        for (u32 i = 0; i < submitted - dropped; ++i) {
            QVERIFY(grabber.getNextFrame(frame, 100));
            const i64 expected = kOriginUs +
                                 (static_cast<i64>(dropped) + i) * kIntervalUs;
            QCOMPARE(frame.timestamp, expected);
        }
        GrabbedFrame drained;
        QVERIFY(!grabber.getNextFrame(drained, 10));
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
