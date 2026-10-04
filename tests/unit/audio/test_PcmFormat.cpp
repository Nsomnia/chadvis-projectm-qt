#include <QtTest>

#include "audio/PcmFormat.hpp"

#include <bit>
#include <cmath>
#include <limits>
#include <span>
#include <string>
#include <vector>

using vc::f32;
using vc::i16;
using vc::i32;
using vc::u32;
using vc::u8;
using vc::usize;
using vc::pcm::ConversionError;

namespace {

/// A byte view over any trivially copyable sample vector. The production caller
/// (AudioEngine::processAudioBuffer) reaches its data the same way, through
/// QAudioBuffer::constData(), so the tests exercise the byte-oriented contract
/// rather than a friendlier one only they have.
template <typename T>
std::span<const std::byte> asBytes(const std::vector<T>& samples) {
    return {reinterpret_cast<const std::byte*>(samples.data()), samples.size() * sizeof(T)};
}

/// Destination pre-filled with a recognisable wrong value. A conversion that
/// wrote fewer samples than it claimed, or fell through without writing at all,
/// would leave this behind -- which is precisely the class of defect the old
/// pass-through of scratchBuffer_ had.
std::vector<f32> poison(usize count) { return std::vector<f32>(count, -12345.0f); }

QString formatName(QAudioFormat::SampleFormat sampleFormat) {
    const std::string_view name = vc::pcm::sampleFormatName(sampleFormat);
    return QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size()));
}

} // namespace

class TestPcmFormat : public QObject {
    Q_OBJECT

private slots:
    void floatSamplesAreCopiedBitForBit() {
        // Exact bit patterns, not "close enough": a float pass-through that
        // widened, narrowed or clamped would satisfy any tolerance-based
        // assertion while changing what the visualizer is fed.
        const std::vector<f32> source{
                1.0f,
                -1.0f,
                0.0f,
                -0.0f,
                0.5f,
                std::numeric_limits<f32>::infinity(),
                -std::numeric_limits<f32>::infinity(),
                std::numeric_limits<f32>::denorm_min(),
                std::numeric_limits<f32>::max(),
        };
        auto destination = poison(source.size());

        auto converted =
                vc::pcm::convertInterleaved(asBytes(source), destination, QAudioFormat::Float);
        QVERIFY(converted.has_value());
        QCOMPARE(destination.size(), source.size());
        QCOMPARE(destination[0], 1.0f);
        QCOMPARE(destination[1], -1.0f);
        QCOMPARE(destination[2], 0.0f);
        // -0.0f == 0.0f compares equal, so the sign bit is checked directly.
        QVERIFY(std::signbit(destination[3]));
        QCOMPARE(destination[4], 0.5f);
        QVERIFY(std::isinf(destination[5]));
        QVERIFY(destination[5] > 0.0f);
        QVERIFY(std::isinf(destination[6]));
        QVERIFY(destination[6] < 0.0f);
        QCOMPARE(destination[7], std::numeric_limits<f32>::denorm_min());
        QCOMPARE(destination[8], std::numeric_limits<f32>::max());
        for (usize i = 0; i < source.size(); ++i) {
            QCOMPARE(std::bit_cast<u32>(destination[i]), std::bit_cast<u32>(source[i]));
        }
    }

    void floatNanSurvivesTheCopy() {
        const f32 nan = std::numeric_limits<f32>::quiet_NaN();
        const std::vector<f32> source{nan, -nan, 1.0f};
        auto destination = poison(source.size());

        QVERIFY(vc::pcm::convertInterleaved(asBytes(source), destination, QAudioFormat::Float)
                        .has_value());
        QVERIFY(std::isnan(destination[0]));
        QVERIFY(std::isnan(destination[1]));
        QCOMPARE(destination[2], 1.0f);
    }

    void int16SamplesConvertToTheExactFullScale() {
        // Every value below is a dyadic rational, so the only correct answer is
        // bit-exact. A divisor of 32767, a 0.5% "safety" factor, or a
        // centisecond-style 10-vs-divide slip all produce nonzero output here and
        // would pass a "nonzero" or "roughly 1.0" assertion.
        struct Case {
            i16 input;
            f32 expected;
        };
        const std::vector<Case> cases{
                {0, 0.0f},         {1, 1.0f / 32768.0f},        {-1, -1.0f / 32768.0f},
                {256, 0.0078125f}, {-256, -0.0078125f},         {16384, 0.5f},
                {-16384, -0.5f},   {32767, 0.999969482421875f}, {-32768, -1.0f},
        };

        std::vector<i16> source;
        std::vector<f32> expected;
        for (const auto& testCase : cases) {
            source.push_back(testCase.input);
            expected.push_back(testCase.expected);
        }
        auto destination = poison(source.size());

        QVERIFY(vc::pcm::convertInterleaved(asBytes(source), destination, QAudioFormat::Int16)
                        .has_value());
        QCOMPARE(destination.size(), expected.size());
        for (usize i = 0; i < expected.size(); ++i) {
            QCOMPARE(destination[i], expected[i]);
        }
    }

    void int16ScaleIsExactForEveryPossibleInput() {
        // Exhaustive round trip over all 65536 inputs. out * 32768 must land back
        // on the original integer exactly, which holds only if the scale is a
        // power of two applied to an exactly-represented cast. Any other constant
        // -- 32767, 32767.5, 0.000030517578125 -- fails somewhere in this range
        // even though every spot check still looks right.
        //
        // Swept in four batches of kMaxInterleavedSamples, not in one 65536-sample
        // call, because convertInterleaved refuses anything above that ceiling with
        // TooManySamples. A single call is therefore not expressible, and raising
        // the ceiling to accommodate a test would trade a real guard -- the
        // conversion runs inside an audio callback and must not be handed a buffer
        // that would allocate -- for a convenience. The sibling
        // sampleCeilingBoundaryIsExact pins that ceiling at exactly 16384 and pins
        // limit+1 as refused, so the two assertions are complementary once the
        // sweep tiles the ceiling: this test now also describes what a *caller* has
        // to do with a buffer larger than the ceiling, which is the only way the
        // 65536-input domain is reachable at all.
        constexpr usize kTotal = 65536;
        constexpr usize kBatch = vc::pcm::kMaxInterleavedSamples;
        constexpr usize kBatches = kTotal / kBatch;
        static_assert(kTotal % kBatch == 0, "the sweep must tile the ceiling exactly");

        std::vector<i16> source(kTotal);
        for (usize i = 0; i < source.size(); ++i) {
            source[i] = static_cast<i16>(static_cast<i32>(i) - 32768);
        }
        // One poisoned sample past the end of every batch, so "this call wrote
        // exactly the samples it was given, and no more" is asserted rather than
        // assumed. A conversion that stopped one sample short, or one that ran into
        // the neighbouring batch's span, leaves a recognisable -12345.0f behind --
        // and the round trip below cannot see that on its own, because the poison
        // value is not a value the sweep generated.
        auto destination = poison(kTotal + kBatches);

        for (usize batch = 0; batch < kBatches; ++batch) {
            const usize offset = batch * kBatch;
            const std::span<const std::byte> sourceSpan{
                    reinterpret_cast<const std::byte*>(source.data() + offset),
                    kBatch * sizeof(i16)};
            // The count is derived from the byte width alone, independently of the
            // conversion, so a silently-truncating conversion cannot make the round
            // trip below agree with itself by shrinking its own count too.
            QCOMPARE(vc::pcm::interleavedSampleCount(sourceSpan.size(), QAudioFormat::Int16),
                     kBatch);
            const std::span<f32> destinationSpan(destination.data() + offset, kBatch + 1);
            QVERIFY2(vc::pcm::convertInterleaved(sourceSpan, destinationSpan, QAudioFormat::Int16)
                             .has_value(),
                     qPrintable(QString("batch %1 of %2 was refused").arg(batch).arg(kBatches)));
            QCOMPARE(destination[offset + kBatch], -12345.0f);

            for (usize i = 0; i < kBatch; ++i) {
                const f32 roundTripped = destination[offset + i] * 32768.0f;
                QVERIFY2(roundTripped == static_cast<f32>(source[offset + i]),
                         qPrintable(QString("batch %1 sample %2").arg(batch).arg(i)));
            }
        }
        QCOMPARE(vc::pcm::kInt16Scale, 1.0f / 32768.0f);
    }

    void interleavedStereoPairsStayInOrder() {
        // L R L R, because the queue indexes by frame and a swap here would be
        // an audible channel inversion rather than a wrong number.
        const std::vector<i16> source{16384, -16384, 0, 32767};
        auto destination = poison(source.size());

        QVERIFY(vc::pcm::convertInterleaved(asBytes(source), destination, QAudioFormat::Int16)
                        .has_value());
        QCOMPARE(destination[0], 0.5f);
        QCOMPARE(destination[1], -0.5f);
        QCOMPARE(destination[2], 0.0f);
        QCOMPARE(destination[3], 0.999969482421875f);
    }

    void everyUnsupportedFormatIsAnErrorAndWritesNothing() {
        const std::vector<QAudioFormat::SampleFormat> unsupported{
                QAudioFormat::Unknown,
                QAudioFormat::UInt8,
                QAudioFormat::Int32,
                QAudioFormat::NSampleFormats,
                static_cast<QAudioFormat::SampleFormat>(QAudioFormat::NSampleFormats + 7),
        };

        for (const auto sampleFormat : unsupported) {
            QVERIFY(!vc::pcm::isSupported(sampleFormat));
            QCOMPARE(vc::pcm::bytesPerSample(sampleFormat), usize{0});

            // Four samples' worth of bytes, so a "format" check that only looked
            // at the byte count would think there was something to decode.
            const std::vector<u8> source(16, 0x7F);
            auto destination = poison(4);

            auto converted =
                    vc::pcm::convertInterleaved(asBytes(source), destination, sampleFormat);
            QVERIFY2(!converted.has_value(), qPrintable(formatName(sampleFormat)));
            QCOMPARE(converted.error().reason, ConversionError::UnsupportedSampleFormat);
            QCOMPARE(converted.error().sampleFormat, sampleFormat);
            QCOMPARE(converted.error().sourceBytes, usize{16});

            // The whole point: a refused conversion must not leave the caller
            // reading whatever was in the destination before.
            for (const f32 sample : destination) {
                QCOMPARE(sample, -12345.0f);
            }
            QVERIFY(!converted.error().describe().empty());
        }
    }

    void supportedFormatsAreReportedAsSupported() {
        QVERIFY(vc::pcm::isSupported(QAudioFormat::Int16));
        QVERIFY(vc::pcm::isSupported(QAudioFormat::Float));
        QCOMPARE(vc::pcm::bytesPerSample(QAudioFormat::Int16), usize{2});
        QCOMPARE(vc::pcm::bytesPerSample(QAudioFormat::Float), usize{4});
        QVERIFY(vc::pcm::sampleFormatName(QAudioFormat::Int32) == "Int32");
        QVERIFY(vc::pcm::sampleFormatName(QAudioFormat::Unknown) == "Unknown");
        // A format value this switch has never heard of still gets a name, so an
        // unsupported-format log line stays actionable.
        QVERIFY(vc::pcm::sampleFormatName(static_cast<QAudioFormat::SampleFormat>(99)) ==
                "UnrecognisedSampleFormat");
    }

    void sampleCeilingBoundaryIsExact() {
        // N-1 / N / N+1 around kMaxInterleavedSamples. The boundary is the point:
        // the old engine silently returned for anything above its scratch buffer,
        // so "just over" had to be a hard, reported failure rather than a pass.
        const usize limit = vc::pcm::kMaxInterleavedSamples;
        QCOMPARE(limit, usize{16384});

        {
            std::vector<f32> source(limit - 1, 0.25f);
            auto destination = poison(limit);
            QVERIFY(vc::pcm::convertInterleaved(asBytes(source), destination, QAudioFormat::Float)
                            .has_value());
            QCOMPARE(destination[limit - 2], 0.25f);
            // One past the converted count is still the poison value.
            QCOMPARE(destination[limit - 1], -12345.0f);
        }

        {
            std::vector<f32> source(limit, -0.5f);
            auto destination = poison(limit);
            QVERIFY(vc::pcm::convertInterleaved(asBytes(source), destination, QAudioFormat::Float)
                            .has_value());
            QCOMPARE(destination[limit - 1], -0.5f);
        }

        {
            std::vector<f32> source(limit + 1, 0.5f);
            auto destination = poison(limit + 1);
            auto converted =
                    vc::pcm::convertInterleaved(asBytes(source), destination, QAudioFormat::Float);
            QVERIFY(!converted.has_value());
            QCOMPARE(converted.error().reason, ConversionError::TooManySamples);
            QCOMPARE(converted.error().sampleCount, limit + 1);
            QCOMPARE(destination[0], -12345.0f);
            QCOMPARE(destination[limit], -12345.0f);
        }
    }

    void destinationTooSmallIsAnErrorNotATruncation() {
        const std::vector<i16> source{0, 16384, -16384, 32767};
        auto destination = poison(3);

        auto converted =
                vc::pcm::convertInterleaved(asBytes(source), destination, QAudioFormat::Int16);
        QVERIFY(!converted.has_value());
        QCOMPARE(converted.error().reason, ConversionError::DestinationTooSmall);
        QCOMPARE(converted.error().sampleCount, usize{4});
        QCOMPARE(converted.error().destinationSamples, usize{3});
        // Truncating to three samples would push a partial frame and silently
        // shift every following sample by one channel.
        for (const f32 sample : destination) {
            QCOMPARE(sample, -12345.0f);
        }
    }

    void oversizedByOneSampleIsRefusedNotPartiallyWritten() {
        // The engine's own window can be smaller than the ceiling, which is how a
        // too-large sink callback reaches this path in production.
        const usize window = vc::pcm::kMinInterleavedSamples;
        std::vector<f32> source(window + 1, 0.75f);
        auto destination = poison(window);

        auto converted =
                vc::pcm::convertInterleaved(asBytes(source), destination, QAudioFormat::Float);
        QVERIFY(!converted.has_value());
        QCOMPARE(converted.error().reason, ConversionError::DestinationTooSmall);
        QCOMPARE(converted.error().sampleCount, window + 1);
        QCOMPARE(converted.error().destinationSamples, window);
        QCOMPARE(destination[window - 1], -12345.0f);
    }

    void truncatedSourceIsAnErrorRatherThanAPartialRead() {
        // Three bytes of Int16 is one and a half samples.
        std::vector<std::byte> source(3, std::byte{0x01});
        auto destination = poison(4);

        auto converted = vc::pcm::convertInterleaved(source, destination, QAudioFormat::Int16);
        QVERIFY(!converted.has_value());
        QCOMPARE(converted.error().reason, ConversionError::SourceNotSampleAligned);
        QCOMPARE(converted.error().sourceBytes, usize{3});
        QCOMPARE(destination[0], -12345.0f);
    }

    void emptyBufferIsASuccessThatWritesNothing() {
        auto destination = poison(4);
        QVERIFY(vc::pcm::convertInterleaved(std::span<const std::byte>{}, destination,
                                            QAudioFormat::Float)
                        .has_value());
        QVERIFY(vc::pcm::convertInterleaved(std::span<const std::byte>{}, destination,
                                            QAudioFormat::Int16)
                        .has_value());
        for (const f32 sample : destination) {
            QCOMPARE(sample, -12345.0f);
        }

        // An empty source into an empty destination is the other degenerate case.
        std::vector<f32> none;
        QVERIFY(vc::pcm::convertInterleaved(std::span<const std::byte>{}, none, QAudioFormat::Float)
                        .has_value());
    }

    void unsupportedFormatWinsOverAnEmptyBuffer() {
        // Order matters: an empty buffer must not be treated as "nothing to do,
        // therefore fine" for a format we cannot decode. Otherwise a sink that
        // reports Int32 and delivers no samples would look healthy.
        auto destination = poison(1);
        auto converted = vc::pcm::convertInterleaved(std::span<const std::byte>{}, destination,
                                                     QAudioFormat::Int32);
        QVERIFY(!converted.has_value());
        QCOMPARE(converted.error().reason, ConversionError::UnsupportedSampleFormat);
    }

    void interleavedSampleCountMatchesTheConversion() {
        QCOMPARE(vc::pcm::interleavedSampleCount(64, QAudioFormat::Float), usize{16});
        QCOMPARE(vc::pcm::interleavedSampleCount(64, QAudioFormat::Int16), usize{32});
        QCOMPARE(vc::pcm::interleavedSampleCount(65, QAudioFormat::Int16), usize{32});
        QCOMPARE(vc::pcm::interleavedSampleCount(64, QAudioFormat::Int32), usize{0});
        QCOMPARE(vc::pcm::interleavedSampleCount(0, QAudioFormat::Float), usize{0});
    }

    void conversionWindowClampsToTheEngineRange() {
        QCOMPARE(vc::pcm::conversionWindow(0), vc::pcm::kMinInterleavedSamples);
        QCOMPARE(vc::pcm::conversionWindow(256), vc::pcm::kMinInterleavedSamples);
        QCOMPARE(vc::pcm::conversionWindow(4095), vc::pcm::kMinInterleavedSamples);
        QCOMPARE(vc::pcm::conversionWindow(4096), usize{4096});
        QCOMPARE(vc::pcm::conversionWindow(8192), usize{8192});
        QCOMPARE(vc::pcm::conversionWindow(16384), usize{16384});
        QCOMPARE(vc::pcm::conversionWindow(16385), vc::pcm::kMaxInterleavedSamples);
        // The default AudioConfig::bufferSize of 2048 is below the floor, so the
        // engine's real window is not the configured number; the status line
        // reports the window for exactly that reason.
        QCOMPARE(vc::pcm::conversionWindow(2048), vc::pcm::kMinInterleavedSamples);
    }

    void errorTextNamesTheOffendingValues() {
        std::vector<f32> source(4, 0.0f);
        auto small = poison(2);
        const std::string text =
                vc::pcm::convertInterleaved(asBytes(source), small, QAudioFormat::Float)
                        .error()
                        .describe();
        QVERIFY(text.find("needs 4 samples") != std::string::npos);
        QVERIFY(text.find("holds 2") != std::string::npos);
    }
};

#include "test_PcmFormat.moc"

int runTestPcmFormat(int argc, char** argv) {
    TestPcmFormat t;
    return QTest::qExec(&t, argc, argv);
}
