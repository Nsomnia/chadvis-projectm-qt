#include <QtTest>
#include <QTemporaryDir>

#include "audio/AudioQueue.hpp"
#include "core/ConfigParsers.hpp"
#include "recorder/AudioFileDecoder.hpp"
#include "recorder/EncoderSettings.hpp"
#include "recorder/FFmpegUtils.hpp"
#include "recorder/ResamplerEngine.hpp"
#include "recorder/VideoRecorderFFmpeg.hpp"

extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/opt.h>
}

#include <array>
#include <bit>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using namespace vc;

namespace {

// ── Fixtures ─────────────────────────────────────────────────────────────────
//
// The input is synthesised at test time rather than checked in, and it is a WAV
// for a reason that is not laziness: **AAC cannot support an exact-value
// assertion**, and every claim this file makes about PCM values depends on being
// able to assert them exactly. A hand-written s16 WAV also lets the expected value
// be *derived* (s16 -> f32 is two power-of-two scalings) rather than transcribed,
// which is what the recorder's centisecond-to-millisecond bug argues for: a
// re-derived constant is what caught it, a copied one would not have.
//
// test_RecordingPipeline.cpp has an equivalent writer (writeTestWav) but it is in
// that file's anonymous namespace, so it cannot be shared without hoisting a
// test-media header this lane is not allowed to create. That hoist is the right
// follow-up; until then this is the second copy and says so.

/// What fills the WAV's payload.
enum class Source {
    /// Every sample the same value, so the decoded result is a constant whose
    /// exact bit pattern is known in advance. This is the strongest assertion
    /// available: a wrong-but-nonzero result -- the failure mode this repository
    /// has now been bitten by twice -- cannot pass it.
    Constant,
    /// A repeating 8-value pattern, different per channel. Catches channel
    /// ordering and interleaving errors a constant cannot, because every sample of
    /// a constant is interchangeable.
    Pattern,
};

/// Eight s16 values, each exactly representable in f32 after the 2^-15 scaling.
/// Includes both full-scale ends, zero, and values that are *not* powers of two
/// (32767, 24576), so a bug that happens to be right for 0.5 does not pass.
constexpr i16 kPattern[8] = {16384, -16384, 0, 8192, -24576, 32767, -32768, 4096};

i16 patternSample(u64 frame, u32 channel) {
    return kPattern[(frame + channel * 3) % 8];
}

/// The exact f32 a correct decoder must produce for an s16 sample.
///
/// Measured exact for every one of the 65536 inputs, not just the ones asserted:
/// the cast s16 -> f32 is exact (16 significand bits into f32's 24) and the scaling
/// is a power of two, so no rounding occurs at any magnitude. Named rather than
/// re-derived per assertion, for the same reason PcmFormat.hpp names
/// kInt16Scale.
constexpr f32 expectedFloat(i16 value) {
    return static_cast<f32>(value) * (1.0f / 32768.0f);
}

/// A complete, well-formed s16 WAV.
///
/// Written byte-by-byte rather than through a muxer for two reasons: the header
/// must be exact so the demuxer sees no probe ambiguity, and the payload must be
/// the literal s16 values the assertions compare against -- an encode/decode round
/// trip through libav would put an unknown quantisation between the fixture and
/// the assertion and destroy the whole point.
std::vector<u8> buildWav(u32 rate, u32 channels, u64 frames, Source source, i16 constant) {
    const auto dataBytes = static_cast<u64>(frames) * channels * 2u;
    std::vector<u8> bytes;
    bytes.reserve(44 + dataBytes);

    const auto tag = [&bytes](const char* fourcc) {
        for (int i = 0; i < 4; ++i) bytes.push_back(static_cast<u8>(fourcc[i]));
    };
    const auto put32 = [&bytes](u32 value) {
        for (int shift = 0; shift < 32; shift += 8)
            bytes.push_back(static_cast<u8>((value >> shift) & 0xFFu));
    };
    const auto put16 = [&bytes](u16 value) {
        bytes.push_back(static_cast<u8>(value & 0xFFu));
        bytes.push_back(static_cast<u8>((value >> 8) & 0xFFu));
    };
    const auto putSample = [&bytes](i16 value) {
        bytes.push_back(static_cast<u8>(static_cast<u16>(value) & 0xFFu));
        bytes.push_back(static_cast<u8>((static_cast<u16>(value) >> 8) & 0xFFu));
    };

    tag("RIFF");
    put32(static_cast<u32>(36 + dataBytes));
    tag("WAVE");
    tag("fmt ");
    put32(16);                    // PCM fmt chunk size
    put16(1);                     // WAVE_FORMAT_PCM
    put16(static_cast<u16>(channels));
    put32(rate);
    put32(rate * channels * 2);   // byte rate
    put16(static_cast<u16>(channels * 2)); // block align
    put16(16);                    // bits per sample
    tag("data");
    put32(static_cast<u32>(dataBytes));

    for (u64 frame = 0; frame < frames; ++frame) {
        for (u32 channel = 0; channel < channels; ++channel) {
            putSample(source == Source::Constant ? constant : patternSample(frame, channel));
        }
    }
    return bytes;
}

bool writeBytes(const fs::path& path, const std::vector<u8>& bytes) {
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    return out.good();
}

std::vector<u8> readBytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<u8>((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());
}

// ── Exactness helpers ────────────────────────────────────────────────────────

/// Bit-exact float comparison.
///
/// **QCOMPARE is not usable for this.** QTest's qCompare for floating point uses
/// qFuzzyCompare, which treats two values one ULP apart as equal -- which is
/// precisely the failure mode these tests exist to catch, so a 1-ULP error would
/// pass. Comparing the bit patterns removes the question entirely and gives a
/// failure message that shows the offending pattern.
inline u32 bitsOf(f32 value) {
    return std::bit_cast<u32>(value);
}

inline void compareExact(f32 actual, f32 expected, const char* what, int index) {
    // The diagnostic is emitted *before* the QCOMPARE, because QCOMPARE's failure
    // path is `maybeThrowOnFail(); return;` -- anything after it in this function is
    // unreachable exactly when it would have been useful.
    if (bitsOf(actual) != bitsOf(expected)) {
        qInfo("  %s at index %d: got %.9g (0x%08x), expected %.9g (0x%08x)", what, index,
              double(actual), bitsOf(actual), double(expected), bitsOf(expected));
    }
    QCOMPARE(bitsOf(actual), bitsOf(expected));
}

/// Exact vector equality with a located failure message. QCOMPARE on
/// std::vector<f32> is not usable here: QTest has no toString for it and would
/// print nothing, so a mismatch would be reported as a bare false.
void sameSamples(const std::vector<f32>& actual, const std::vector<f32>& expected,
                 const char* what) {
    QCOMPARE(actual.size(), expected.size());
    if (actual.size() != expected.size()) return;
    for (usize i = 0; i < actual.size(); ++i) {
        if (bitsOf(actual[i]) != bitsOf(expected[i])) {
            compareExact(actual[i], expected[i], what, int(i));
            return;
        }
    }
    QVERIFY2(true, qPrintable(QString::fromLatin1(what)));
}

QString text(std::string_view value) {
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

/// Decode a whole file, asserting nothing about it but that it produced audio.
///
/// Returns the concatenation of every chunk, so a caller can make its own claims
/// about the values and the frame count without re-implementing the loop. The
/// chunk sizes are returned too, because "what shape did the decoder hand out" is
/// itself a claim worth asserting.
struct DecodedAudio {
    std::vector<f32> samples;          ///< interleaved, stereo
    std::vector<u32> chunkFrames;      ///< frames per chunk, in order
    u64 totalFrames{0};
    DecodeOutcome outcome{DecodeOutcome::NotStarted};
    f64 positionSeconds{0.0};
    f64 declaredDurationSeconds{0.0};
    bool sawError{false};
    DecodeError error{};
};

DecodedAudio decodeWholeFile(const fs::path& path, const AudioFileDecoder::OutputSpec& spec) {
    DecodedAudio result;
    AudioFileDecoder decoder(spec);
    const auto opened = decoder.open(path);
    if (!opened) {
        result.sawError = true;
        result.error = opened.error();
        result.outcome = decoder.outcome();
        return result;
    }

    PcmChunk chunk;
    while (true) {
        const auto next = decoder.nextChunk(chunk);
        if (!next) {
            result.sawError = true;
            result.error = next.error();
            break;
        }
        if (*next == ChunkStatus::EndOfStream) break;
        // Recorded rather than QCOMPAREd: a QVERIFY in a non-void helper expands to
        // a bare `return`, which is a compile error here -- test_RecordingPipeline.cpp
        // says the same thing about QTRY_VERIFY. The assertions live in the tests.
        if (chunk.channels != spec.channels || chunk.sampleRate != spec.sampleRate) {
            result.sawError = true;
            result.error.kind = DecodeErrorKind::DecodeFailed;
            result.error.message = "a chunk was handed out in the wrong PCM shape";
            break;
        }
        result.chunkFrames.push_back(chunk.frameCount());
        result.totalFrames += chunk.frameCount();
        result.samples.insert(result.samples.end(), chunk.samples.begin(),
                              chunk.samples.end());
    }
    result.outcome = decoder.outcome();
    result.positionSeconds = decoder.positionSeconds();
    result.declaredDurationSeconds = decoder.declaredDurationSeconds();
    return result;
}

// ── A real compressed fixture, through the project's own encoder ─────────────
//
// Hand-written WAVs cover the exactness claims and the variable-frame-size loop
// (pcm_s16le reports AVCodecContext::frame_size == 0, measured), but they are not
// evidence that this handles a compressed track in a real container. This is that
// evidence, built the way test_RecordingPipeline.cpp builds its own media: through
// VideoRecorderFFmpeg, so no capture is checked in and no external tool is invoked.

bool encodeAacFixture(const fs::path& requested, fs::path& actualOut) {
    EncoderSettings settings;
    settings.outputPath = requested;
    settings.video.codec = VideoCodec::H264;
    settings.video.width = 32;
    settings.video.height = 32;
    settings.video.fps = 30;
    settings.video.crf = 0;
    settings.video.preset = EncoderPreset::Ultrafast;
    settings.audio.codec = AudioCodec::AAC;
    settings.audio.bitrate = 64;
    settings.audio.sampleRate = 48000;
    settings.audio.channels = 2;
    settings.container = Container::MP4;

    VideoRecorderFFmpeg encoder;
    if (auto started = encoder.init(settings); !started) return false;
    actualOut = fs::path(encoder.getOutputPath());

    u64 bytesWritten = 0;
    // 8192 frames is eight AAC frames at 1024 samples each, so the track carries
    // real content rather than a header. No video frames are submitted, which is
    // itself useful: the container's duration is then driven by the audio, and a
    // decoder that compared decoded audio against the *container* duration would
    // be relying on a coincidence rather than on a rule.
    std::vector<f32> audio(8192 * 2, 0.25f);
    encoder.encodeAudio(audio, 2, bytesWritten);
    encoder.flush(bytesWritten);
    encoder.cleanup();
    return true;
}

} // namespace

class TestAudioFileDecoder : public QObject {
    Q_OBJECT

private slots:
    // ── Exact PCM ───────────────────────────────────────────────────────────

    void aConstantDcSignalComesBackBitExact() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const fs::path path = fs::path(dir.path().toStdString()) / "dc.wav";
        QVERIFY(writeBytes(path, buildWav(8000, 2, 4000, Source::Constant, 16384)));

        // 8 kHz in, 8 kHz out: no rate conversion and no channel rematrix, so
        // every sample must be the s16 value's exact f32 image.
        AudioFileDecoder decoder({.sampleRate = 8000, .channels = 2});
        const auto opened = decoder.open(path);
        QVERIFY2(opened, qPrintable(decoder.lastError().describe().c_str()));

        QCOMPARE(text(decoder.codecName()), QStringLiteral("pcm_s16le"));
        QCOMPARE(decoder.outcome(), DecodeOutcome::Streaming);
        QCOMPARE(decoder.framesDecoded(), u64{0});

        const DecodedAudio decoded = decodeWholeFile(
            path, AudioFileDecoder::OutputSpec{.sampleRate = 8000, .channels = 2});

        QVERIFY2(!decoded.sawError, qPrintable(decoded.error.describe().c_str()));
        QCOMPARE(decoded.totalFrames, u64{4000});
        QCOMPARE(decoded.samples.size(), usize{8000});

        // Every single sample, compared bit-for-bit. A tolerant comparison would
        // accept a -3 dB rematrix, a wrong channel order, or an off-by-one frame
        // offset; none of those can survive this.
        for (usize i = 0; i < decoded.samples.size(); ++i)
            compareExact(decoded.samples[i], expectedFloat(16384), "sample", int(i));

        QCOMPARE(decoded.outcome, DecodeOutcome::Complete);
        // 4000 frames at 8 kHz is exactly half a second, and exactly half a second
        // is what the WAV header declares.
        QCOMPARE(decoded.positionSeconds, 0.5);
        QCOMPARE(decoded.declaredDurationSeconds, 0.5);
    }

    void everyAssertedSignedValueSurvivesExactly() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        // -32768 is full negative scale, 32767 the largest positive s16 (whose
        // f32 image, 0.999969482421875, is NOT representable as a decimal literal
        // and has to come from the same scaling the decoder performs), and the
        // odd values guard against anything that happens to work for 0.5.
        const i16 values[] = {-32768, -24576, -16384, -1, 0, 1, 8192, 16384, 24576, 32767};

        for (const i16 value : values) {
            const fs::path path =
                fs::path(dir.path().toStdString()) / ("dc_" + std::to_string(value) + ".wav");
            QVERIFY(writeBytes(path, buildWav(8000, 2, 64, Source::Constant, value)));

            const DecodedAudio decoded =
                decodeWholeFile(path, AudioFileDecoder::OutputSpec{.sampleRate = 8000,
                                                                     .channels = 2});
            QVERIFY2(!decoded.sawError, qPrintable(decoded.error.describe().c_str()));
            QCOMPARE(decoded.totalFrames, u64{64});
            QCOMPARE(decoded.samples.size(), usize{128});
            for (usize i = 0; i < decoded.samples.size(); ++i) {
                compareExact(decoded.samples[i], expectedFloat(value), "dc sample", int(i));
            }
        }
    }

    // ── Channel conversion ───────────────────────────────────────────────────

    void aMonoSourceIsDuplicatedExactlyIntoBothChannels() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const fs::path path = fs::path(dir.path().toStdString()) / "mono.wav";
        QVERIFY(writeBytes(path, buildWav(8000, 1, 4000, Source::Pattern, 0)));

        // The pattern is what makes this test able to fail. With a constant, L and
        // R being swapped is invisible; with a channel-offset pattern every frame
        // is distinguishable, so a swap shows up as a value mismatch rather than as
        // a plausible-looking duplicate.
        const DecodedAudio decoded =
            decodeWholeFile(path, AudioFileDecoder::OutputSpec{.sampleRate = 8000,
                                                                 .channels = 2});
        QVERIFY2(!decoded.sawError, qPrintable(decoded.error.describe().c_str()));
        QCOMPARE(decoded.totalFrames, u64{4000});

        for (u64 frame = 0; frame < 4000; ++frame) {
            const f32 expected = expectedFloat(patternSample(frame, 0));
            compareExact(decoded.samples[frame * 2], expected, "left", int(frame));
            compareExact(decoded.samples[frame * 2 + 1], expected, "right", int(frame));
            // Explicit L == R as well, so the failure message names the property
            // that actually broke rather than whichever sample happened to differ.
            QCOMPARE(bitsOf(decoded.samples[frame * 2]), bitsOf(decoded.samples[frame * 2 + 1]));
        }
    }

    void aMonoSourceIsNotAttenuatedByTheDefaultRematrix() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const fs::path path = fs::path(dir.path().toStdString()) / "mono_dc.wav";
        QVERIFY(writeBytes(path, buildWav(8000, 1, 512, Source::Constant, 16384)));

        // This is the assertion that would fail if the resampler were left on
        // libswresample's default mono->stereo matrix. Measured: that matrix
        // multiplies by 1/sqrt(2), so a mono DC of 0.5f arrives as exactly
        // 0.353553385f -- bit-identical to 0.5f * 0.70710678118654752440f -- because
        // libswresample treats the source as a centre channel spreading to a pair.
        //
        // It would be a *subtle* 3 dB bug: the render would look and measure
        // correct, every level assertion would pass, and every mono source would be
        // quieter than the same file played through AudioQueue::pushAll, which
        // duplicates mono at unity (AudioQueue.hpp:215-218). So the expected value
        // here is the source's own amplitude, not a rescaled one.
        const DecodedAudio decoded =
            decodeWholeFile(path, AudioFileDecoder::OutputSpec{.sampleRate = 8000,
                                                                 .channels = 2});
        QVERIFY2(!decoded.sawError, qPrintable(decoded.error.describe().c_str()));
        QCOMPARE(decoded.totalFrames, u64{512});
        for (usize i = 0; i < decoded.samples.size(); ++i)
            compareExact(decoded.samples[i], 0.5f, "mono->stereo", int(i));
    }

    void aMonoSinkGetsNoRematrixAtAll() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const fs::path path = fs::path(dir.path().toStdString()) / "mono_dc2.wav";
        QVERIFY(writeBytes(path, buildWav(8000, 1, 512, Source::Constant, 16384)));

        // A mono sink is the case where a unity matrix would be *wrong*: a
        // two-element matrix applied to a one-channel output mixes the input into
        // itself twice. Asking for one channel and getting the source amplitude
        // back is what proves the matrix is applied only where it is meant to be.
        const DecodedAudio decoded =
            decodeWholeFile(path, AudioFileDecoder::OutputSpec{.sampleRate = 8000,
                                                                 .channels = 1});
        QVERIFY2(!decoded.sawError, qPrintable(decoded.error.describe().c_str()));
        QCOMPARE(decoded.totalFrames, u64{512});
        QCOMPARE(decoded.samples.size(), usize{512});
        for (usize i = 0; i < decoded.samples.size(); ++i)
            compareExact(decoded.samples[i], 0.5f, "mono->mono", int(i));
    }

    // ── Rate conversion ──────────────────────────────────────────────────────

    void aRateConversionProducesExactlyTheRescaledFrameCount() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        // 4000 frames is chosen to be a whole number of the demuxer's 512-sample
        // packets, so the input length is unambiguous and the expected output is
        // arithmetic rather than a transcribed constant.
        constexpr u64 kInputFrames = 4000;
        constexpr u32 kInputRate = 8000;
        const fs::path path = fs::path(dir.path().toStdString()) / "rate.wav";
        QVERIFY(writeBytes(path, buildWav(kInputRate, 2, kInputFrames, Source::Constant, 16384)));

        struct Case {
            u32 outputRate;
            u64 expectedFrames;
        };
        // av_rescale, spelled as the multiplication it is. Measured on this
        // project's FFmpeg: 8k->48k gives 24000, 8k->44.1k gives 22050, and a
        // *non-integral* ratio lands on the truncated value with nothing left over
        // once swr is drained. A "roughly the right length" assertion would pass on
        // a decoder that forgot to flush the resampler -- which loses the filter's
        // tail and clips the end of the song -- so the number has to be exact.
        const Case cases[] = {
            {48000, kInputFrames * 48000 / kInputRate},
            {44100, kInputFrames * 44100 / kInputRate},
            {16000, kInputFrames * 16000 / kInputRate},
        };

        for (const Case& one : cases) {
            const DecodedAudio decoded =
                decodeWholeFile(path, AudioFileDecoder::OutputSpec{.sampleRate = one.outputRate,
                                                                     .channels = 2});
            QVERIFY2(!decoded.sawError, qPrintable(decoded.error.describe().c_str()));
            QCOMPARE(decoded.totalFrames, one.expectedFrames);
            QCOMPARE(decoded.samples.size(), usize{one.expectedFrames * 2});
            // Same duration in, same duration out: the rescale is exactly length-
            // preserving in time, which is what makes the output usable for a render
            // that has to line up with a frame timeline.
            QCOMPARE(decoded.positionSeconds, 0.5);
        }
    }

    void aRateConversionTo48kFrom16kIsAlsoExact() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        constexpr u64 kInputFrames = 4000;
        const fs::path path = fs::path(dir.path().toStdString()) / "rate16.wav";
        QVERIFY(writeBytes(path, buildWav(16000, 2, kInputFrames, Source::Constant, 16384)));

        const DecodedAudio decoded =
            decodeWholeFile(path, AudioFileDecoder::OutputSpec{.sampleRate = 48000,
                                                                 .channels = 2});
        QVERIFY2(!decoded.sawError, qPrintable(decoded.error.describe().c_str()));
        QCOMPARE(decoded.totalFrames, kInputFrames * 3);
    }

    // ── Chunking and variable frame size ─────────────────────────────────────

    void chunksFollowTheDecodersOwnFrameSizesAndReassembleExactly() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const fs::path path = fs::path(dir.path().toStdString()) / "chunky.wav";
        QVERIFY(writeBytes(path, buildWav(8000, 2, 4000, Source::Pattern, 0)));

        // 512 is exactly one pcm_s16le packet. Chosen because it makes the chunk
        // sizes *observable*: pcm_s16le reports AVCodecContext::frame_size == 0
        // (measured), so the only thing that can size a chunk is the frame that
        // actually arrived -- 512 samples for each of the first seven packets and
        // 416 for the last. A decoder that sized its buffers from frame_size would
        // produce zeros, or refuse the file, and this assertion is where that shows.
        constexpr u32 kChunkFrames = 512;
        const DecodedAudio chunked =
            decodeWholeFile(path, AudioFileDecoder::OutputSpec{.sampleRate = 8000,
                                                                 .channels = 2,
                                                                 .chunkFrames = kChunkFrames});
        QVERIFY2(!chunked.sawError, qPrintable(chunked.error.describe().c_str()));

        QCOMPARE(chunked.chunkFrames.size(), usize{8});
        for (usize i = 0; i < 7; ++i)
            QCOMPARE(chunked.chunkFrames[i], u32{kChunkFrames});
        // The short tail is the assertion that matters: 4000 = 7 * 512 + 416.
        QCOMPARE(chunked.chunkFrames[7], u32{4000 - 7 * kChunkFrames});
        QCOMPARE(chunked.totalFrames, u64{4000});

        // The carry-over cursor must not drop or duplicate a sample. Compared
        // against a single-chunk read of the same file, sample for sample, so a
        // boundary bug that only appears when a chunk does not divide the stream is
        // caught rather than averaged away.
        const DecodedAudio whole = decodeWholeFile(
            path, AudioFileDecoder::OutputSpec{.sampleRate = 8000,
                                               .channels = 2,
                                               .chunkFrames = 100000});
        sameSamples(whole.samples, chunked.samples, "chunked reassembly");
        QCOMPARE(whole.totalFrames, chunked.totalFrames);
    }

    void aChunkSizeThatDividesNothingStillReassemblesExactly() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const fs::path path = fs::path(dir.path().toStdString()) / "chunky97.wav";
        QVERIFY(writeBytes(path, buildWav(8000, 2, 4000, Source::Pattern, 0)));

        // 97 is prime and divides none of the interesting lengths (4000, 512, 8).
        // The carry-over buffer is a cursor into a vector that a rate conversion
        // can leave partially consumed, so the interesting sizes are the ones that
        // force the cursor to advance by an odd amount repeatedly.
        const DecodedAudio odd = decodeWholeFile(
            path, AudioFileDecoder::OutputSpec{.sampleRate = 48000, .channels = 2,
                                               .chunkFrames = 97});
        QVERIFY2(!odd.sawError, qPrintable(odd.error.describe().c_str()));
        QCOMPARE(odd.totalFrames, u64{4000 * 6});

        const DecodedAudio reference = decodeWholeFile(
            path, AudioFileDecoder::OutputSpec{.sampleRate = 48000, .channels = 2,
                                               .chunkFrames = 1 << 20});
        sameSamples(reference.samples, odd.samples, "odd chunk reassembly");
    }

    // ── Named errors, never a throw and never a silent zero ──────────────────

    void anUnopenableFileIsRefusedWithANamedError() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const fs::path here = fs::path(dir.path().toStdString());

        // Four shapes that must all land on the same named failure rather than on a
        // zero-length success: absent, empty, not-media, and a directory. The last
        // one is the interesting case -- open(2) on a directory succeeds on Linux
        // and fails on macOS, so a decoder that reported the errno would differ per
        // platform. A named kind does not.
        const fs::path junk = here / "junk.bin";
        {
            std::ofstream out(junk, std::ios::binary);
            out << "this is not audio, it is a sentence in a file";
        }
        const fs::path empty = here / "empty.bin";
        QVERIFY(writeBytes(empty, {}));

        const fs::path cases[] = {here / "does-not-exist.wav", junk, empty, here};

        for (const fs::path& path : cases) {
            AudioFileDecoder decoder;
            const auto opened = decoder.open(path);
            QVERIFY2(!opened, qPrintable(path.string().c_str()));
            QCOMPARE(opened.error().kind, DecodeErrorKind::OpenFailed);
            QCOMPARE(text(decodeErrorKindName(opened.error().kind)),
                     QStringLiteral("OpenFailed"));
            // av_strerror's own words, so the message is a diagnosis and not a code.
            QVERIFY2(!opened.error().message.empty(),
                     qPrintable(path.string().c_str()));
            QVERIFY(!decoder.isOpen());
            QVERIFY(decoder.codecName().empty());
            QCOMPARE(decoder.outcome(), DecodeOutcome::Failed);
            QCOMPARE(decoder.framesDecoded(), u64{0});
        }
    }

    void anAudioStreamWithNoSamplesIsAnErrorNotASilentZero() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        // A valid header declaring a data chunk with no payload behind it. Measured:
        // the demuxer opens it, finds an audio stream, and decodes nothing --
        // AVStream::duration comes back as AV_NOPTS_VALUE, so the duration check
        // cannot catch it either. This is the shape that would otherwise render a
        // zero-length video and report it as a finished render, so it gets its own
        // error kind rather than an EndOfStream that reads like success.
        const fs::path path = fs::path(dir.path().toStdString()) / "header_only.wav";
        QVERIFY(writeBytes(path, buildWav(8000, 2, 0, Source::Constant, 16384)));

        AudioFileDecoder decoder({.sampleRate = 8000, .channels = 2});
        const auto opened = decoder.open(path);
        QVERIFY2(opened, qPrintable(decoder.lastError().describe().c_str()));

        PcmChunk chunk;
        const auto next = decoder.nextChunk(chunk);
        QVERIFY(!next.has_value());
        QCOMPARE(next.error().kind, DecodeErrorKind::EmptyAudioStream);
        QCOMPARE(text(decodeErrorKindName(next.error().kind)),
                 QStringLiteral("EmptyAudioStream"));
        // Cleared, never left holding a partial block.
        QCOMPARE(chunk.frameCount(), u32{0});
        QCOMPARE(decoder.framesDecoded(), u64{0});
        QCOMPARE(decoder.outcome(), DecodeOutcome::Failed);
        // And it stays refused: a second call must not quietly succeed on the
        // strength of the stream now being drained.
        QVERIFY(!decoder.nextChunk(chunk).has_value());
    }

    void anUnusableOutputSpecIsRefusedBeforeTheFileIsTouched() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const fs::path path = fs::path(dir.path().toStdString()) / "fine.wav";
        QVERIFY(writeBytes(path, buildWav(8000, 2, 64, Source::Constant, 16384)));

        struct Case {
            AudioFileDecoder::OutputSpec spec;
            const char* why;
        };
        const Case cases[] = {
            {{.sampleRate = 8000, .channels = 0}, "zero channels"},
            {{.sampleRate = 8000, .channels = 3}, "more channels than AudioFrame holds"},
            {{.sampleRate = 0, .channels = 2}, "zero rate"},
            {{.sampleRate = 48000, .channels = 2, .chunkFrames = 0}, "zero chunk"},
        };

        for (const Case& one : cases) {
            AudioFileDecoder decoder(one.spec);
            const auto opened = decoder.open(path);
            QVERIFY2(!opened, one.why);
            QCOMPARE(opened.error().kind, DecodeErrorKind::InvalidOutputSpec);
            // Checked first, so the failure cannot be blamed on the input: nothing
            // was opened, which is what proves it.
            QVERIFY(!decoder.isOpen());
            QCOMPARE(decoder.outcome(), DecodeOutcome::Failed);
        }
    }

    void readingBeforeOpeningIsNamedAndDoesNotFailTheStream() {
        AudioFileDecoder decoder;
        PcmChunk chunk;
        const auto next = decoder.nextChunk(chunk);
        QVERIFY(!next.has_value());
        QCOMPARE(next.error().kind, DecodeErrorKind::NotOpen);
        // NotOpen is a caller mistake, not a decode failure: it must not poison the
        // outcome, or a worker that probed before opening would report the job as
        // failed forever after.
        QCOMPARE(decoder.outcome(), DecodeOutcome::NotStarted);

        // And after a real open, the decoder is usable again -- open() resets.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const fs::path path = fs::path(dir.path().toStdString()) / "after.wav";
        QVERIFY(writeBytes(path, buildWav(8000, 2, 64, Source::Constant, 16384)));
        QVERIFY(decoder.open(path));
        QCOMPARE(decoder.outcome(), DecodeOutcome::Streaming);
    }

    void reopeningReplacesTheFileRatherThanMixingTwo() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const fs::path first = fs::path(dir.path().toStdString()) / "first.wav";
        const fs::path second = fs::path(dir.path().toStdString()) / "second.wav";
        QVERIFY(writeBytes(first, buildWav(8000, 2, 512, Source::Constant, 16384)));
        QVERIFY(writeBytes(second, buildWav(16000, 1, 256, Source::Constant, -16384)));

        AudioFileDecoder decoder({.sampleRate = 16000, .channels = 1});
        QVERIFY(decoder.open(first));
        PcmChunk chunk;
        QCOMPARE(decoder.nextChunk(chunk).value(), ChunkStatus::Ready);
        // The counters must be live before the reopen resets them, or the
        // assertions after it would pass on a decoder that never counted at all.
        QVERIFY(decoder.framesDecoded() > 0);
        QVERIFY(decoder.positionSeconds() > 0.0);

        QVERIFY(decoder.open(second));
        QCOMPARE(decoder.framesDecoded(), u64{0});
        QCOMPARE(decoder.positionSeconds(), 0.0);
        QCOMPARE(text(decoder.sourcePath()), QString::fromStdString(second.string()));

        const DecodedAudio decoded = decodeWholeFile(
            second, AudioFileDecoder::OutputSpec{.sampleRate = 16000, .channels = 1});
        QVERIFY(!decoded.sawError);
        QCOMPARE(decoded.totalFrames, u64{256});
        // -16384/32768 == -0.5 exactly, in the same file that previously carried
        // +0.5: proof the reopen did not leave the old stream's leftovers behind.
        for (usize i = 0; i < decoded.samples.size(); ++i)
            compareExact(decoded.samples[i], -0.5f, "second file", int(i));
    }

    // ── Truncation, and the honest answer about it ───────────────────────────

    void aTruncatedWavIsIndistinguishableFromAShortOneAndSaysSo() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const fs::path whole = fs::path(dir.path().toStdString()) / "whole.wav";
        QVERIFY(writeBytes(whole, buildWav(8000, 2, 4000, Source::Constant, 16384)));
        const std::vector<u8> complete = readBytes(whole);
        QVERIFY(complete.size() > 2000);

        // Cut inside the payload. The expected frame count is *derived*, not
        // transcribed: 2000 bytes total, 44 of them header, 4 bytes per stereo
        // frame. So the only frames available are (2000 - 44) / 4 == 489, and the
        // assertion cannot drift from the fixture the way a copied constant would.
        constexpr usize kCutBytes = 2000;
        constexpr usize kHeaderBytes = 44;
        constexpr usize kBytesPerFrame = 4;
        const u64 expectedFrames =
            static_cast<u64>((kCutBytes - kHeaderBytes) / kBytesPerFrame);

        const fs::path cut = fs::path(dir.path().toStdString()) / "cut.wav";
        QVERIFY(writeBytes(cut, std::vector<u8>(complete.begin(),
                                                complete.begin() + kCutBytes)));

        AudioFileDecoder decoder({.sampleRate = 8000, .channels = 2});
        QVERIFY2(decoder.open(cut), qPrintable(decoder.lastError().describe().c_str()));
        QCOMPARE(decoder.outcome(), DecodeOutcome::Streaming);

        PcmChunk chunk;
        u64 frames = 0;
        while (true) {
            const auto next = decoder.nextChunk(chunk);
            QVERIFY2(next.has_value(), qPrintable(decoder.lastError().describe().c_str()));
            if (*next == ChunkStatus::EndOfStream) break;
            frames += chunk.frameCount();
        }
        QCOMPARE(frames, expectedFrames);
        QCOMPARE(decoder.framesDecoded(), expectedFrames);

        // The measured answer, asserted rather than assumed, because it is the whole
        // point of this test:
        //
        //  * `av_read_frame` returned a CLEAN AVERROR_EOF. Every corruption tried
        //    produced the same -- cut mid-payload, cut into the header, a `data`
        //    chunk size of 0x7FFFFFFF, no payload at all.
        //  * libavformat REWROTE the duration to 61125 us == 489/8000, i.e. it
        //    agrees with what it decoded rather than with what the header declared
        //    (0.5 s).
        //
        // So there is no evidence of truncation and the decoder says Complete. A
        // decoder that guessed "truncated" here would be lying in the other
        // direction: a genuinely 61 ms file is indistinguishable from a truncated
        // 500 ms one, and reporting both as broken makes the verdict useless.
        QCOMPARE(decoder.demuxerStopCode(), AVERROR_EOF);
        QCOMPARE(decoder.outcome(), DecodeOutcome::Complete);
        // The two durations agreeing is *why* it is Complete, so assert that rather
        // than the verdict alone.
        QCOMPARE(decoder.declaredDurationSeconds(), decoder.positionSeconds());
        QCOMPARE(decoder.positionSeconds(), double(expectedFrames) / 8000.0);
        // The container's own figure is exposed separately for a render receipt.
        QCOMPARE(decoder.containerDurationSeconds(), decoder.positionSeconds());
    }

    // ── Backpressure: the exact full-queue boundary ──────────────────────────

    void aFullQueueDropsExactlyTheFramesItCannotHold() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const fs::path path = fs::path(dir.path().toStdString()) / "bp.wav";
        QVERIFY(writeBytes(path, buildWav(8000, 2, 4000, Source::Constant, 16384)));

        // Capacity 1. Measured on the vendored moodycamel: a one-slot queue accepts
        // exactly one 64-byte AudioFrame and refuses the second. A larger capacity
        // does NOT give a usable boundary -- 2, 3 and 8 all behaved identically
        // because the queue allocates in blocks and a capacity below the block size
        // rounds up (measured: capacity 2 accepted 24 frames = 3 AudioFrames), so
        // this is the only capacity whose boundary is a named constant.
        constexpr u32 kQueueCapacity = 1;
        AudioQueue queue(kQueueCapacity);

        AudioFileDecoder decoder(
            {.sampleRate = 8000, .channels = 2, .chunkFrames = 512});
        QVERIFY2(decoder.open(path), qPrintable(decoder.lastError().describe().c_str()));

        const auto first = decoder.pushChunkTo(queue);
        QVERIFY2(first.has_value(), qPrintable(decoder.lastError().describe().c_str()));
        const QueuePush push = first.value();

        // One full chunk in, one AudioFrame's worth accepted, the rest counted.
        // These three numbers are the boundary: exactly AUDIO_FRAME_SAMPLES frames
        // were taken, exactly the remainder were dropped, and the two sum to the
        // chunk. A decoder that ignored pushAll's verdict, or that reported a bool,
        // could not say any of that.
        QCOMPARE(push.frames, u32{512});
        QCOMPARE(push.framesAcceptedByRecorder(), AUDIO_FRAME_SAMPLES);
        QCOMPARE(push.framesDroppedByRecorder, u32{512} - AUDIO_FRAME_SAMPLES);
        QCOMPARE(push.framesAcceptedByRecorder() + push.framesDroppedByRecorder,
                 push.frames);
        QVERIFY(!push.lossless());
        QVERIFY(!push.acceptedByBothQueues);
        QCOMPARE(queue.recDepth(), kQueueCapacity);
        // The queue's own counters agree, which is what makes the numbers above
        // exact rather than inferred.
        QCOMPARE(queue.recDropCount(), u64{push.framesDroppedByRecorder});

        // Both queues got the same chunk, so both lost the same frames. A worker
        // that only drains viz needs to know its side is the one that stalled.
        QCOMPARE(push.framesDroppedByVisualizer, push.framesDroppedByRecorder);

        // A second push onto the still-full queue accepts **nothing** -- not even one
        // AudioFrame -- and reports all 512 offered frames as dropped. Measured, not
        // assumed: AudioQueue::pushInternal enqueues whole AudioFrames and returns
        // on the first try_enqueue failure (AudioQueue.hpp:221-234), so with no
        // slot free the very first 8-frame chunk is refused and `remaining` is
        // still the full 512. The first push above took exactly
        // AUDIO_FRAME_SAMPLES because one slot *was* free; the accepted count is
        // therefore the number of free slots, and zero free slots means zero
        // accepted. The old expectation of AUDIO_FRAME_SAMPLES described a queue
        // that had been drained -- which is what the next test does deliberately.
        const auto second = decoder.pushChunkTo(queue);
        QVERIFY2(second.has_value(), qPrintable(decoder.lastError().describe().c_str()));
        QCOMPARE(second.value().frames, u32{512});
        QCOMPARE(second.value().framesAcceptedByRecorder(), u32{0});
        QCOMPARE(second.value().framesDroppedByRecorder, u32{512});
        // Both queues were full from the first push, so both refused in full --
        // `pushAll` writes viz and rec independently, so a partial drop on one
        // side is possible in general and would be the case a bool could not show.
        QCOMPARE(second.value().framesDroppedByVisualizer, u32{512});
        QVERIFY(!second.value().lossless());
        QVERIFY(!second.value().acceptedByBothQueues);
        // The refusal is total: neither queue grew by a single entry.
        QCOMPARE(queue.recDepth(), kQueueCapacity);
        QCOMPARE(queue.vizDepth(), kQueueCapacity);
        // And the counters accumulate exactly: 504 from the first push, which had
        // one slot to fill, plus 512 for a queue with none. This is what makes
        // "dropped" comparable with "offered" at all, and it is the arithmetic
        // `pushChunkTo` reads its answer from.
        QCOMPARE(queue.recDropCount(), u64{(512u - AUDIO_FRAME_SAMPLES) + 512u});
        QCOMPARE(queue.vizDropCount(), u64{(512u - AUDIO_FRAME_SAMPLES) + 512u});
    }

    void aDrainedQueueAcceptsAgain() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const fs::path path = fs::path(dir.path().toStdString()) / "bp2.wav";
        QVERIFY(writeBytes(path, buildWav(8000, 2, 4000, Source::Constant, 16384)));

        AudioQueue queue(1);
        AudioFileDecoder decoder(
            {.sampleRate = 8000, .channels = 2, .chunkFrames = 512});
        QVERIFY2(decoder.open(path), qPrintable(decoder.lastError().describe().c_str()));

        const auto first = decoder.pushChunkTo(queue);
        QVERIFY(first.has_value());
        QVERIFY(!first.value().lossless());

        // Drain exactly one AudioFrame and exactly one slot is free again, so the
        // next push takes AUDIO_FRAME_SAMPLES frames -- one entry -- and drops the
        // remaining 504. The previous expectation of 2 * AUDIO_FRAME_SAMPLES was
        // self-refuting: it wanted two entries out of a queue whose own
        // `QCOMPARE(queue.recDepth(), u32{1})` below shows holds one. Capacity 1 is
        // exact on the vendored moodycamel (measured: requested 1 -> 1 entry,
        // requested 2 -> 3, requested 8 -> 15, requested 64 -> 127, because a block
        // is allocated with one spare slot and ceilToPow2 rounds up), so "accepts
        // again" means one entry. This is the loop a render worker runs, so its
        // arithmetic is pinned here rather than assumed.
        AudioFrame frame;
        QVERIFY(queue.popRec(frame));
        QCOMPARE(frame.sampleCount, AUDIO_FRAME_SAMPLES);
        QCOMPARE(frame.channels, u32{2});
        QCOMPARE(frame.sampleRate, u32{8000});
        QCOMPARE(queue.recDepth(), u32{0});

        const auto second = decoder.pushChunkTo(queue);
        QVERIFY2(second.has_value(), qPrintable(decoder.lastError().describe().c_str()));
        QCOMPARE(second.value().frames, u32{512});
        QCOMPARE(second.value().framesAcceptedByRecorder(), AUDIO_FRAME_SAMPLES);
        QCOMPARE(second.value().framesDroppedByRecorder, u32{512} - AUDIO_FRAME_SAMPLES);
        QCOMPARE(queue.recDepth(), u32{1});

        // "Accepts again" is only half the property, and the half the old test never
        // checked: taking that one entry must not leave room for another, or a
        // caller reading the verdict as "it all fit" would mis-plan. The queue is
        // full again, so the next push is refused whole -- the same total refusal
        // aFullQueueDropsExactlyTheFramesItCannotHold pins, asserted here so this
        // test covers the whole cycle on its own.
        //
        // Note the viz queue is still holding its very first entry (it was never
        // drained), which is why the visualizer side reports 512 dropped from the
        // second push onward. That is the queue's honest state, not the recorder's
        // arithmetic, so the recorder figures below stay the ones asserted.
        const auto third = decoder.pushChunkTo(queue);
        QVERIFY2(third.has_value(), qPrintable(decoder.lastError().describe().c_str()));
        QCOMPARE(third.value().frames, u32{512});
        QCOMPARE(third.value().framesAcceptedByRecorder(), u32{0});
        QCOMPARE(third.value().framesDroppedByRecorder, u32{512});
        QCOMPARE(queue.recDepth(), u32{1});
    }

    void aWorkerCanDrainEveryChunkWithoutLosingASample() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const fs::path path = fs::path(dir.path().toStdString()) / "loop.wav";
        QVERIFY(writeBytes(path, buildWav(8000, 2, 4000, Source::Pattern, 0)));

        // The shape the render worker needs: push, drain, repeat -- and end up
        // with every sample exactly once, in order. The backpressure loop is the
        // renderer's whole correctness story, and this is the only place it is
        // exercised end to end.
        //
        // BOTH queues are drained, and that is the point rather than an extra.
        // `pushAll` writes viz and rec independently and `lossless()` is true only
        // when *neither* dropped, so a worker that leaves one of the two queues it
        // fills undrained is not lossless and the assertion was right to fail. The
        // previous version drained viz only: the recorder queue took 64 entries
        // from the first push, 63 from the second and refused the 64th, dropping 8
        // frames. Those drops were caused by this test, not by the decoder.
        constexpr u32 kQueueCapacity = 64;
        AudioQueue queue(kQueueCapacity);
        AudioFileDecoder decoder({.sampleRate = 8000, .channels = 2, .chunkFrames = 512});
        QVERIFY2(decoder.open(path), qPrintable(decoder.lastError().describe().c_str()));

        // The two units this queue mixes, asserted once and in isolation so the
        // drain loops below cannot confuse them again: `vizDepth()` counts
        // AudioFrame *entries*, while a pop's `maxFrames` counts stereo *frames*
        // held inside those entries, and one entry holds AUDIO_FRAME_SAMPLES of
        // them. Measured on the vendored queue: 64 pushed frames is 8 entries, and
        // popVizBatch(..., AUDIO_FRAME_SAMPLES) returns AUDIO_FRAME_SAMPLES while
        // consuming exactly one entry. Reading the pop count as entries is how a
        // drain loop ends up believing a queue is empty when it is not.
        {
            AudioQueue unitProbe(kQueueCapacity);
            std::vector<f32> sixtyFour(64 * 2, 0.0f);
            QVERIFY(unitProbe.pushAll(sixtyFour.data(), 64, 2, 8000));
            QCOMPARE(unitProbe.vizDepth(), u32{64 / AUDIO_FRAME_SAMPLES});
            std::array<f32, AUDIO_FRAME_SAMPLES * 2> oneEntry{};
            QCOMPARE(unitProbe.popVizBatch(oneEntry.data(), AUDIO_FRAME_SAMPLES),
                     AUDIO_FRAME_SAMPLES);
            QCOMPARE(unitProbe.vizDepth(), u32{64 / AUDIO_FRAME_SAMPLES - 1});
        }

        std::vector<f32> drained;
        u64 produced = 0;
        u64 recorderFrames = 0;
        while (true) {
            // Drain to empty, not to a high-water mark. One 512-frame chunk is 64
            // entries and a requested capacity of 64 gives 127, so a residue as
            // small as 64 entries plus the next chunk would not fit -- which is
            // exactly what a `vizDepth() >= 32` high-water mark leaves behind.
            // AUDIO_FRAME_SAMPLES per call, because the pop's maxFrames is in
            // stereo frames; the unit assertion above is what makes that safe.
            while (queue.vizDepth() > 0) {
                std::array<f32, AUDIO_FRAME_SAMPLES * 2> scratch{};
                const u32 popped = queue.popVizBatch(scratch.data(), AUDIO_FRAME_SAMPLES);
                QVERIFY(popped > 0);
                drained.insert(drained.end(), scratch.begin(), scratch.begin() + popped * 2);
            }
            while (queue.recDepth() > 0) {
                std::array<f32, AUDIO_FRAME_SAMPLES * 2> scratch{};
                const u32 popped = queue.popRecBatch(scratch.data(), AUDIO_FRAME_SAMPLES);
                QVERIFY(popped > 0);
                recorderFrames += popped;
            }
            const auto push = decoder.pushChunkTo(queue);
            QVERIFY2(push.has_value(), qPrintable(decoder.lastError().describe().c_str()));
            if (push.value().frames == 0) break;
            QVERIFY2(push.value().lossless(),
                     "a worker that drains both of the queues it fills before every "
                     "push must not drop a frame");
            produced += push.value().frames;
        }
        while (queue.vizDepth() > 0) {
            std::array<f32, AUDIO_FRAME_SAMPLES * 2> scratch{};
            const u32 popped = queue.popVizBatch(scratch.data(), AUDIO_FRAME_SAMPLES);
            if (popped == 0) break;
            drained.insert(drained.end(), scratch.begin(), scratch.begin() + popped * 2);
        }
        while (queue.recDepth() > 0) {
            std::array<f32, AUDIO_FRAME_SAMPLES * 2> scratch{};
            const u32 popped = queue.popRecBatch(scratch.data(), AUDIO_FRAME_SAMPLES);
            if (popped == 0) break;
            recorderFrames += popped;
        }
        const u64 lost = produced * 2 - static_cast<u64>(drained.size());

        QCOMPARE(produced, u64{4000});
        QCOMPARE(decoder.framesDecoded(), produced);
        QCOMPARE(lost, u64{0});
        QCOMPARE(static_cast<u64>(drained.size()), u64{8000});
        // What `lossless()` claims, stated on its own: both queues were drained to
        // empty, both handshook every frame, and neither drop counter moved. The
        // viz-only version of this loop could assert none of that.
        QCOMPARE(queue.vizDepth(), u32{0});
        QCOMPARE(queue.recDepth(), u32{0});
        QCOMPARE(recorderFrames, produced);
        QCOMPARE(queue.vizDropCount(), u64{0});
        QCOMPARE(queue.recDropCount(), u64{0});
        // totalPushed_ counts *per queue*: pushInternal increments it once for viz
        // and once for rec, so a single pushAll of 512 frames advances it by 1024.
        // Measured, and pinned here because it is the reason "pushed minus dropped"
        // is not a meaningful subtraction -- the two counters are on different
        // scales (AudioQueue.hpp:227-231).
        QCOMPARE(queue.totalPushed(), 2 * produced);
        // And the values are the source's, in order -- a duplicated or skipped
        // frame would leave the right count and the wrong contents.
        for (u64 frame = 0; frame < 4000; ++frame) {
            compareExact(drained[frame * 2], expectedFloat(patternSample(frame, 0)), "drained L",
                         int(frame));
            compareExact(drained[frame * 2 + 1], expectedFloat(patternSample(frame, 1)),
                         "drained R", int(frame));
        }
    }

    // ── A real compressed track in a real container ──────────────────────────

    void aRealAacTrackInAnMp4DecodesToCompletion() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        fs::path actual;
        QVERIFY2(encodeAacFixture(fs::path(dir.path().toStdString()) / "aac.mp4", actual),
                 "the project's own encoder refused to write the AAC fixture");

        AudioFileDecoder decoder({.sampleRate = 48000, .channels = 2});
        const auto opened = decoder.open(actual);
        QVERIFY2(opened, qPrintable(decoder.lastError().describe().c_str()));
        QCOMPARE(text(decoder.codecName()), QStringLiteral("aac"));

        PcmChunk chunk;
        u64 frames = 0;
        u64 positives = 0;
        while (true) {
            const auto next = decoder.nextChunk(chunk);
            QVERIFY2(next.has_value(), qPrintable(decoder.lastError().describe().c_str()));
            if (*next == ChunkStatus::EndOfStream) break;
            frames += chunk.frameCount();
            for (const f32 sample : chunk.samples) {
                // AAC output is planar (AV_SAMPLE_FMT_FLTP), which is exactly why
                // the decode loop reads extended_data rather than data[0]: this is
                // the test that would break if it ever read only the first plane,
                // and the symptom would be a silent channel rather than an error.
                QVERIFY(std::isfinite(sample));
                if (sample > 0.0f) ++positives;
            }
        }

        QVERIFY2(frames > 0, "AAC produced no frames at all");
        QCOMPARE(decoder.framesDecoded(), frames);
        // Complete is not a vague "it finished": it is defined as the decoded
        // duration being within kDurationSlackSeconds of the audio stream's own
        // declared duration, which is the 50 ms that AAC's initial_padding (1024
        // samples, ~21 ms at 48 kHz) can legitimately cost. Asserting the verdict
        // rather than a tolerance here keeps the test exact.
        QCOMPARE(decoder.outcome(), DecodeOutcome::Complete);
        QVERIFY(qAbs(decoder.positionSeconds() - decoder.declaredDurationSeconds()) <=
                AudioFileDecoder::kDurationSlackSeconds);
        // Non-silence, without pretending to know what AAC did to it. A decoder
        // that returned zeros would satisfy every other assertion in this test.
        QVERIFY2(positives > 0, "every decoded sample was zero or negative");
        // And the duration it reports is the *audio* stream's, which for a file
        // whose picture outlasts its sound is the only figure a render should use.
        QVERIFY(decoder.containerDurationSeconds() > 0.0);
    }

    void everyErrorKindHasAName() {
        // A kind with no name would print as "Unknown" in a log line, which is the
        // difference between a diagnosis and a shrug.
        const DecodeErrorKind kinds[] = {
            DecodeErrorKind::NotOpen,        DecodeErrorKind::InvalidOutputSpec,
            DecodeErrorKind::OpenFailed,     DecodeErrorKind::StreamInfoFailed,
            DecodeErrorKind::NoAudioStream,  DecodeErrorKind::NoDecoder,
            DecodeErrorKind::UnusableStreamParameters,
            DecodeErrorKind::EmptyAudioStream,
            DecodeErrorKind::DecoderOpenFailed, DecodeErrorKind::ResamplerFailed,
            DecodeErrorKind::DecodeFailed,
        };
        // Distinctness, not just presence: two kinds sharing a name would make a
        // log line ambiguous, and a switch that grew a new enumerator without a
        // name would print "Unknown" -- both are silent failures of this file's
        // whole purpose.
        for (usize i = 0; i < std::size(kinds); ++i) {
            const std::string_view name = decodeErrorKindName(kinds[i]);
            QVERIFY(!name.empty());
            QVERIFY2(name != "Unknown",
                     qPrintable(QString::number(static_cast<int>(kinds[i]))));
            for (usize j = i + 1; j < std::size(kinds); ++j) {
                QVERIFY2(name != decodeErrorKindName(kinds[j]),
                         qPrintable(QString::fromUtf8(name.data(),
                                                     static_cast<qsizetype>(name.size()))));
            }
        }

        const DecodeOutcome outcomes[] = {DecodeOutcome::NotStarted, DecodeOutcome::Streaming,
                                          DecodeOutcome::Complete, DecodeOutcome::Truncated,
                                          DecodeOutcome::Failed};
        for (usize i = 0; i < std::size(outcomes); ++i) {
            const std::string_view name = decodeOutcomeName(outcomes[i]);
            QVERIFY(!name.empty());
            QVERIFY2(name != "Unknown",
                     qPrintable(QString::number(static_cast<int>(outcomes[i]))));
            for (usize j = i + 1; j < std::size(outcomes); ++j) {
                QVERIFY2(name != decodeOutcomeName(outcomes[j]),
                         qPrintable(QString::fromUtf8(name.data(),
                                                     static_cast<qsizetype>(name.size()))));
            }
        }
    }

    // ── Resampling engine selection ──────────────────────────────────────────
    //
    // The brief for this work said "call swr_set_engine". That function does not
    // exist in this project's libswresample: it is absent from swresample.h, and
    // `nm -gU libswresample.7.1.102.dylib` lists 20 swr_ symbols with no such
    // name among them. Engine selection goes through av_opt_set(ctx, "engine",
    // "soxr", 0) instead. These tests pin the *decision* -- which is pure, and so
    // assertable on a machine with no soxr at all -- plus one live probe against
    // the real libswresample, which is the only thing that can catch a wrong
    // option name.

    void soxrIsSelectedOnlyWhenTheLibraryActuallyAcceptedIt() {
        // The affirmative case, stated first: a genuine 0 is the ONLY thing that
        // selects soxr. Nothing else may, or a soxr-less build would silently
        // claim the better engine.
        const EngineChoice yes = chooseEngine(0);
        QCOMPARE(yes.engine, ResamplerEngine::Soxr);
        QVERIFY(yes.usingSoxr());
        QCOMPARE(yes.fallback, SoxrFallbackReason::None);
        // Nothing to report, so nothing is reported: a soxr success that still
        // carried a reason string would put a stale caveat in every log line.
        QVERIFY(yes.detail.empty());

        // The default is the engine libswresample already uses, so leaving it
        // alone changes no output -- which is what makes the fallback free.
        const EngineChoice no = chooseEngine(-1);
        QCOMPARE(no.engine, ResamplerEngine::Default);
        QVERIFY(!no.usingSoxr());
        QVERIFY(!no.describe().empty());
    }

    void aLibswresampleWithoutSoxrFallsBackAndSaysWhy() {
        // The measured result on this machine: pkg-config --exists soxr exits 1,
        // `ffmpeg -buildconf` has no --enable-libsoxr, and
        // av_opt_set(swr, "engine", "soxr", 0) returns AVERROR_OPTION_NOT_FOUND
        // because libswresample omits the option entirely rather than
        // registering it and refusing later. Feeding that exact value in asserts
        // the classification -- OptionMissing, NOT a generic refusal -- because
        // the two need different advice: only this one is fixed by rebuilding
        // FFmpeg, and a message that said "refused" would send someone to
        // reconfigure a build that was already correct.
        const EngineChoice missing = chooseEngine(AVERROR_OPTION_NOT_FOUND);
        QCOMPARE(missing.engine, ResamplerEngine::Default);
        QCOMPARE(missing.fallback, SoxrFallbackReason::OptionMissing);
        // The message names libswresample rather than saying "soxr unavailable",
        // because the actionable fact is which library lacks the option -- that
        // is what tells someone the fix is a different FFmpeg and not a
        // different build of this project.
        QVERIFY(soxrFallbackReasonName(SoxrFallbackReason::OptionMissing).find("libswresample") !=
                std::string_view::npos);
        // av_strerror's own words, so a log carries the number *and* the text.
        QCOMPARE(missing.detail, std::string("Option not found"));

        // A different negative means something else and must not be dressed up as
        // a missing option.
        QCOMPARE(chooseEngine(AVERROR(EINVAL)).fallback, SoxrFallbackReason::Refused);

        // Declining to attempt (CHADVIS_USE_SOXR_ENGINE=0, or a null context) is
        // its own reason: it says nothing about what the library would have said.
        const EngineChoice notTried = chooseEngine(std::nullopt);
        QCOMPARE(notTried.engine, ResamplerEngine::Default);
        QCOMPARE(notTried.fallback, SoxrFallbackReason::NotAttempted);
        QVERIFY(notTried.detail.empty());
    }

    void theLiveResamplerAgreesWithWhateverThisBuildCanDo() {
        // The only assertion here that can fail on a *correct* build is the one
        // that must not: it asserts the mechanism, not a particular engine. A
        // wrong option name would give AVERROR_OPTION_NOT_FOUND here too, and
        // that is indistinguishable from "this FFmpeg has no soxr" -- which is
        // exactly why the control below exists.
        //
        // Control first: av_opt_find does work on a SwrContext. Without it, a
        // null from the engine lookup would prove nothing, because a broken probe
        // looks exactly like an absent option.
        SwrContext* ctx = nullptr;
        AVChannelLayout stereo;
        av_channel_layout_default(&stereo, 2);
        QCOMPARE(swr_alloc_set_opts2(&ctx, &stereo, AV_SAMPLE_FMT_FLT, 48000, &stereo,
                                     AV_SAMPLE_FMT_FLT, 44100, 0, nullptr),
                 0);
        QVERIFY(ctx != nullptr);
        QVERIFY(av_opt_find(ctx, "filter_size", nullptr, 0, 0) != nullptr);

        const bool soxrInThisBuild = av_opt_find(ctx, "engine", nullptr, 0, 0) != nullptr;
        const EngineChoice applied = applyEngine(ctx, "test");
        swr_free(&ctx);

        if (soxrInThisBuild) {
            // The affirmative half of the brief: "if soxr is present, assert it is
            // actually selected." It is not merely asserted in a mock -- the real
            // library is asked and the real answer is checked.
            QVERIFY2(applied.usingSoxr(),
                     "this libswresample exposes an 'engine' option but soxr was "
                     "not selected");
        } else {
            QCOMPARE(applied.engine, ResamplerEngine::Default);
            QCOMPARE(applied.fallback, SoxrFallbackReason::OptionMissing);
            QCOMPARE(applied.detail, std::string("Option not found"));
        }

        // Either way the context is usable, which is the property that makes the
        // fallback free rather than a build-breaking dependency. Measured:
        // swr_init returns 0 with engine=soxr refused.
        QVERIFY(applyEngine(nullptr, "test").fallback == SoxrFallbackReason::NotAttempted);
    }

    // ── Encoder settings that could never survive a save ─────────────────────
    //
    // ConfigParsers::serialize rebuilds the whole TOML root from the structs, so
    // a struct member with no entry in a CHADVIS_*_FIELDS table is dropped on
    // every save -- silently, because the defaults were already the values anyone
    // would have configured. Four members shipped in that state for the life of
    // the project. This is the guard.
    //
    // The canonical home for a config round-trip is tests/unit/core/
    // test_ConfigLoader.cpp; it lives here as well so it is guarded today rather
    // than after a handoff, and because these four are the recorder's output spec
    // -- the AudioFileDecoder suite is where an encoder spec belongs.

    void everyEncoderFieldSurvivesASave() {
        RecordingConfig original;
        original.video.gopSize = 240;
        original.video.bFrames = 2;
        original.audio.sampleRate = 44100;
        original.audio.channels = 2;

        const toml::table out = ConfigParsers::serialize(
                AudioConfig{}, VisualizerConfig{}, original, UIConfig{}, KeyboardConfig{},
                SunoConfig{}, KaraokeConfig{}, OverlayConfig{}, false);

        // Read back through the real parser rather than inspecting the table, so
        // the test covers both halves of the round trip. A hand-edited
        // Configuration::get on the raw table would pass with the *write* broken.
        RecordingConfig reloaded;
        QVERIFY(out["recording"].is_table());
        ConfigParsers::parseRecording(out, reloaded);

        QCOMPARE(reloaded.video.gopSize, 240u);
        QCOMPARE(reloaded.video.bFrames, 2u);
        QCOMPARE(reloaded.audio.sampleRate, 44100u);
        QCOMPARE(reloaded.audio.channels, 2u);

        // And the keys are the documented spelling, asserted literally. The whole
        // failure was invisible precisely because nobody could name what had gone
        // missing; a test that only compared structs would not have caught a key
        // renamed on both sides at once.
        QCOMPARE(out["recording"]["video"]["gop_size"].value_or(0), 240);
        QCOMPARE(out["recording"]["video"]["b_frames"].value_or(0), 2);
        QCOMPARE(out["recording"]["audio"]["sample_rate"].value_or(0), 44100);
        QCOMPARE(out["recording"]["audio"]["channels"].value_or(0), 2);
    }

    void encoderOutputDefaultsAndValidationHold() {
        // The struct defaults are what a first run writes, so they are also what
        // an absent key falls back to. Pinning them makes a default change a
        // deliberate act.
        const AudioEncoderConfig def{};
        QCOMPARE(def.sampleRate, 48000u);
        QCOMPARE(def.channels, 2u);
        const VideoEncoderConfig vdef{};
        QCOMPARE(vdef.gopSize, 0u);
        QCOMPARE(vdef.bFrames, 0u);

        // Two bounds that are decisions rather than tastes, and both of which can
        // silently corrupt a render if dropped:
        //  - channels > 2 cannot reach a consumer, because AudioQueue::AudioFrame
        //    is stereo-only. Accepting 6 would open an encoder nothing can feed.
        //  - a b_frames value above 16 gains nothing and interacts badly with a
        //    short GOP.
        RecordingConfig cfg;
        cfg.audio.channels = 6;
        cfg.audio.sampleRate = 4000;
        cfg.video.gopSize = 100000;
        cfg.video.bFrames = 900;
        const toml::table tbl = ConfigParsers::serialize(AudioConfig{}, VisualizerConfig{}, cfg,
                                                         UIConfig{}, KeyboardConfig{}, SunoConfig{},
                                                         KaraokeConfig{}, OverlayConfig{}, false);
        RecordingConfig parsed;
        ConfigParsers::parseRecording(tbl, parsed);
        QCOMPARE(parsed.audio.channels, 2u);
        QCOMPARE(parsed.audio.sampleRate, 8000u);
        // gop_size 0 means "auto" (VideoRecorderFFmpeg substitutes fps * 2), so
        // an absurd value is pulled back to auto rather than clamped to something
        // arbitrary.
        QCOMPARE(parsed.video.gopSize, 0u);
        QCOMPARE(parsed.video.bFrames, 16u);
    }

    void theAudioEncoderSpecIsTheOutputsNotTheInputs() {
        // sampleRate/channels were the ambiguous half of this change: nothing in
        // ConfigData.hpp said whether they described the source file or the
        // encoded result, and a reader could reasonably assume the former. The
        // comment now claims they are the OUTPUT spec, so this pins that claim
        // against the code that consumes it -- a comment asserting an
        // unverified fact is worse than no comment.
        //
        // EncoderSettings::fromConfig() is the only hop from the config struct
        // into the encoder, and it is a static function reading the Config
        // singleton, so it cannot be driven from a fixture without global state.
        // The evidence is therefore the *other* consumer, which is explicit:
        // VideoRecorderFFmpeg::initAudioStream sets
        // `audioCodecCtx_->sample_rate = settings.audio.sampleRate` and passes
        // the same value to swr_alloc_set_opts2 as the resampler's OUT rate.
        // Both are reads of the encoder's output spec. If either were changed to
        // read the source, this test would need revisiting -- which is the point
        // of writing it.
        const EncoderSettings settings;
        QCOMPARE(settings.audio.sampleRate, 48000u);
        QCOMPARE(settings.audio.channels, 2u);

        // A different table, a different meaning: `[audio] sample_rate` belongs
        // to AudioConfig and describes the playback device, not this track. A
        // shared key name across the two tables is exactly how the original
        // ambiguity happened, so name them apart.
        const AudioConfig playback;
        QCOMPARE(playback.sampleRate, 44100u);
        QVERIFY(playback.sampleRate != AudioEncoderConfig{}.sampleRate);
    }
};

#include "test_AudioFileDecoder.moc"

int runTestAudioFileDecoder(int argc, char** argv) {
    TestAudioFileDecoder t;
    return QTest::qExec(&t, argc, argv);
}
