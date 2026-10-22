#include <QTemporaryDir>
#include <QtTest>

#include "audio/AudioQueue.hpp"
#include "audio/VisualizerAudioTap.hpp"

#include <fstream>
#include <string>
#include <vector>

using namespace vc;

namespace {

/// A PCM WAV the decoder can read without a codec. The tap's job is accounting
/// and position, not file-format support -- AudioFileDecoder's own suite owns
/// that -- so a hand-written s16 WAV keeps this fixture exact and dependency-free.
bool writeConstantWav(const fs::path& path, const u32 rate, const u32 channels, const u64 frames,
                      const i16 value) {
    std::vector<u8> bytes;
    const auto put32 = [&bytes](u32 v) {
        for (int s = 0; s < 32; s += 8) bytes.push_back(static_cast<u8>((v >> s) & 0xFFu));
    };
    const auto put16 = [&bytes](u16 v) {
        bytes.push_back(static_cast<u8>(v & 0xFFu));
        bytes.push_back(static_cast<u8>((v >> 8) & 0xFFu));
    };
    const auto tag = [&bytes](const char* fourcc) {
        for (int i = 0; i < 4; ++i) bytes.push_back(static_cast<u8>(fourcc[i]));
    };

    const auto dataBytes = static_cast<u32>(frames * channels * 2);
    tag("RIFF");
    put32(36u + dataBytes);
    tag("WAVE");
    tag("fmt ");
    put32(16);
    put16(1);
    put16(static_cast<u16>(channels));
    put32(rate);
    put32(rate * channels * 2);
    put16(static_cast<u16>(channels * 2));
    put16(16);
    tag("data");
    put32(dataBytes);
    for (u64 frame = 0; frame < frames; ++frame) {
        for (u32 channel = 0; channel < channels; ++channel)
            put16(static_cast<u16>(value));
    }

    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    return out.good();
}

/// The queue counts entries of AUDIO_FRAME_SAMPLES (8) interleaved stereo frames,
/// not PCM frames. Named here so an assertion reads in the unit it is comparing.
constexpr u32 kFramesPerQueueEntry = 8;

} // namespace

class TestVisualizerAudioTap : public QObject {
    Q_OBJECT

private slots:
    /// The reason this class exists: a tick while playing must put PCM in the
    /// visualizer queue, and it must decode ahead of the player rather than
    /// dribble exactly nothing.
    void aTickFeedsTheQueueAheadOfThePlayer() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const fs::path file = fs::path(dir.path().toStdString()) / "tone.wav";
        QVERIFY(writeConstantWav(file, 48000, 2, 48000 * 3, 16384));

        AudioQueue queue;
        VisualizerAudioTap tap(queue);
        tap.setSource(file);

        tap.pump(0);
        QVERIFY2(tap.isActive(), "the tap gave up on a valid file");
        // Four 4096-frame chunks, capped per tick. The lead is 400 ms (19200
        // frames), which four chunks do not reach, so the cap is what decides.
        QCOMPARE(queue.vizDepth(), 4u * 4096u / kFramesPerQueueEntry);

        tap.pump(1000);
        QVERIFY2(queue.vizDepth() > 4u * 4096u / kFramesPerQueueEntry,
                 "the second tick added no audio, so the queue would starve in 100 ms");
        QVERIFY(tap.isActive());
    }

    /// A player position beyond the end must end the tap rather than seek past the
    /// file every tick. The engine then stops pumping; a later track resets it.
    void anEndedFileGoesDormant() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const fs::path file = fs::path(dir.path().toStdString()) / "short.wav";
        QVERIFY(writeConstantWav(file, 48000, 2, 48000, 16384));

        AudioQueue queue;
        VisualizerAudioTap tap(queue);
        tap.setSource(file);
        tap.pump(5000);
        QVERIFY2(!tap.isActive(), "the tap kept seeking past the end of a one-second file");
    }

    /// A file the decoder refuses is reported once and disables the tap; it must
    /// not retry 25 times a second for the life of the track.
    void anUndecodableFileIsDroppedNotRetried() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const fs::path file = fs::path(dir.path().toStdString()) / "not-audio.mp3";
        {
            std::ofstream out(file, std::ios::binary);
            QVERIFY(out.good());
            out << "not audio";
        }

        AudioQueue queue;
        VisualizerAudioTap tap(queue);
        tap.setSource(file);
        tap.pump(0);
        QVERIFY(!tap.isActive());
        QCOMPARE(queue.vizDepth(), 0u);

        tap.pump(100);
        QVERIFY(!tap.isActive());
    }

    /// No local file (a remote entry, or before a track is selected) must be a
    /// no-op rather than an open of an empty path.
    void noSourcePushesNothing() {
        AudioQueue queue;
        VisualizerAudioTap tap(queue);
        tap.pump(0);
        QCOMPARE(queue.vizDepth(), 0u);
        QVERIFY(!tap.isActive());
    }
};

#include "test_VisualizerAudioTap.moc"

int runTestVisualizerAudioTap(int argc, char** argv) {
    TestVisualizerAudioTap tc;
    return QTest::qExec(&tc, argc, argv);
}
