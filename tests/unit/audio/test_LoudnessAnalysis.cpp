#include <QtTest>

#include "audio/LoudnessAnalysis.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <numbers>
#include <span>
#include <vector>

using vc::f32;
using vc::f64;
using vc::u32;
using vc::u64;
using vc::usize;
using vc::loudness::BiquadCoefficients;
using vc::loudness::GainPlan;
using vc::loudness::LoudnessAnalyser;
using vc::loudness::LoudnessAnalysis;
using vc::loudness::LoudnessError;
using vc::loudness::LoudnessTarget;

namespace {

constexpr f64 kRate = 48000.0;

/// A 1 kHz sine, generated in f64 and *stored* as f32, which is what every decoder
/// hands over. The reference figures these tests assert were produced with exactly
/// this construction, and narrowing is where the whole 8.2e-6 LU of measurement
/// noise comes from: each f32 sample carries up to 0.5 ULP of the sine, and 19200 of
/// them are summed per block.
///
/// The phase is always 0. A tone whose crest sits between samples would be a more
/// adversarial signal, but for *loudness* it is a worse test -- it changes which
/// sample lands in which block. The crest alignment matters for true peak, which is
/// where `samplePeakDbfs < truePeakDbfs` is asserted instead.
std::vector<f32> sineFrames(f64 amplitude, f64 seconds, f64 hertz = 1000.0) {
    const auto count = static_cast<usize>(std::llround(seconds * kRate));
    std::vector<f32> samples(count);
    for (usize index = 0; index < count; ++index) {
        const f64 phase =
                2.0 * std::numbers::pi_v<f64> * hertz * static_cast<f64>(index) / kRate;
        samples[index] = static_cast<f32>(amplitude * std::sin(phase));
    }
    return samples;
}

/// Interleaves a per-channel frame list into the f32 layout the analyser consumes.
///
/// A stereo duplication at **unity**, which is what `AudioFileDecoder` does for a mono
/// source. Deliberately not at -3 dB: see the header's note on the swresample rematrix
/// trap resurfacing as a loudness number.
std::vector<f32> interleave(const std::vector<std::vector<f32>>& channels) {
    const usize channelCount = channels.size();
    std::vector<f32> interleaved(channels.front().size() * channelCount);
    for (usize channel = 0; channel < channelCount; ++channel) {
        for (usize frame = 0; frame < channels[channel].size(); ++frame) {
            interleaved[frame * channelCount + channel] = channels[channel][frame];
        }
    }
    return interleaved;
}

/// Runs `samples` through a fresh analyser at `channelCount`.
///
/// Returned rather than asserted on so each test can state what it expects. An
/// unexpected error surfaces as a default-constructed result whose `integratedLufs`
/// is 0.0, which no expectation below can match.
LoudnessAnalysis measure(const std::vector<f32>& samples, u32 channelCount) {
    LoudnessAnalyser analyser;
    analyser.configure(kRate, channelCount);
    if (!analyser.push(std::span<const f32>(samples), samples.size() / channelCount)) {
        return {};
    }
    const auto result = analyser.result();
    return result ? *result : LoudnessAnalysis{};
}

/// 1 kHz at -20 dBFS **peak** (amplitude 0.1), stereo. The brief's "a -20 dBFS sine
/// measures about -20 LUFS", and it is nearly exact: BS.1770's -0.691 offset was
/// chosen to cancel the K-weighting's +0.6977 dB at 1 kHz, leaving 0.0067 LU of
/// residual. Reading it as mono instead gives -23.0036 LUFS, which is the R128
/// -23 LUFS reference level -- the same signal, one convention apart.
constexpr f64 kMinus20DbfsPeakAmplitude = 0.1;
constexpr f64 kExpectedMinus20Lufs = -19.993303687;

/// Tolerance on every LUFS figure below, and where it comes from.
///
///  * Measured agreement between this algorithm's biquad recursion and a closed-form
///    evaluation of |B(e^jw)/A(e^jw)| using the *published* Table 1 / Table 2
///    coefficients: **8.2e-6 LU**. That is the entire arithmetic noise floor, and it
///    is dominated by f32 sample quantisation rather than by f64 anything.
///  * The 400 ms block that contains the K-filter's start-up transient sits 0.0003 LU
///    below steady state. Spread across a 37-block mean that is 1e-5 LU.
///  * So 0.002 LU leaves two orders of magnitude of headroom over the noise, which is
///    what makes it safe against a different libm, an FMA contraction, or SIMD in the
///    sine generator changing the last bits.
///
/// And what it buys: perturbing a single K-weighting coefficient moves the result by
/// 48 LU (pre-filter b2) to 510 LU (RLB a2) per unit of coefficient error, so 0.002 LU
/// catches any single-coefficient error above 4e-5. Below that it does not -- which is
/// why `fullScaleOneKilohertzSineMeasuresTheKWeightedFigureNotZero` *also* asserts the
/// derived coefficients against the published literals to 1e-12. The two assertions
/// have disjoint jobs: this one pins the algorithm, that one pins the numbers.
constexpr f64 kLufsTolerance = 0.002;

} // namespace

class TestLoudnessAnalysis : public QObject {
    Q_OBJECT

private slots:
    // The test that catches a wrong coefficient. It pins the coefficient table
    // itself, then the four K-weighted figures that table produces.
    void fullScaleOneKilohertzSineMeasuresTheKWeightedFigureNotZero() {
        // 1. The derived coefficients must BE BS.1770-4 Tables 1 and 2 at 48 kHz.
        //
        // These literals are transcribed from the recommendation, not from any
        // implementation. `preFilterCoefficients()` derives its values from the
        // normative analogue parameters instead, so this assertion is genuinely
        // independent: it is what caught a 5.5e-7 transcription error in a2 during
        // development, where the derived and remembered values disagreed in the
        // seventh digit and the published table sided with the derivation.
        //
        // Tolerance 1e-12 against a measured agreement of 8.88e-16 -- four orders of
        // margin for a libm whose tan/pow differ in the last bit, and still four
        // orders tighter than any plausible typo.
        const auto pre = vc::loudness::preFilterCoefficients(kRate);
        const auto rlb = vc::loudness::rlbHighPassCoefficients(kRate);
        QVERIFY(pre.has_value());
        QVERIFY(rlb.has_value());

        constexpr f64 kCoefficientTolerance = 1e-12;
        // BS.1770-4 Table 1, "stage 1 of the pre-filter to model a spherical head".
        QVERIFY2(std::abs(pre->b0 - 1.53512485958697) < kCoefficientTolerance,
                 "Table 1 b0");
        QVERIFY2(std::abs(pre->b1 - -2.69169618940638) < kCoefficientTolerance,
                 "Table 1 b1");
        QVERIFY2(std::abs(pre->b2 - 1.19839281085285) < kCoefficientTolerance,
                 "Table 1 b2");
        QVERIFY2(std::abs(pre->a1 - -1.69065929318241) < kCoefficientTolerance,
                 "Table 1 a1");
        QVERIFY2(std::abs(pre->a2 - 0.73248077421585) < kCoefficientTolerance,
                 "Table 1 a2 -- 0.73248077421585, not 0.73248022221538");
        // BS.1770-4 Table 2, the RLB weighting curve.
        QCOMPARE(rlb->b0, 1.0);
        QCOMPARE(rlb->b1, -2.0);
        QCOMPARE(rlb->b2, 1.0);
        QVERIFY2(std::abs(rlb->a1 - -1.99004745483398) < kCoefficientTolerance,
                 "Table 2 a1");
        QVERIFY2(std::abs(rlb->a2 - 0.99007225036621) < kCoefficientTolerance,
                 "Table 2 a2");

        // 2. What that table does to a full-scale 1 kHz sine.
        //
        // The brief's premise here was that K-weighting *attenuates* 1 kHz. It does
        // not: measured |H_K(1 kHz)|^2 = 1.1742767, i.e. **+0.6977 dB**. The reason a
        // full-scale 1 kHz sine does not read 0 LUFS is different, and is three
        // separate factors that happen to be individually large:
        //
        //   -0.691   BS.1770 eq. (2)'s offset
        //   -3.0103  a sine of amplitude 1 has RMS 1/sqrt(2), and z is a mean *square*
        //   +3.0103  summing two channels of weight 1.0 against one (Table 3)
        //
        // Mono therefore reads -3.0036 and stereo +0.0067 -- the latter *above* digital
        // full scale, which is the honest answer for a full-scale signal in two
        // channels and the reason LUFS is not bounded by 0.
        const std::vector<f32> mono = sineFrames(1.0, 4.0);
        const std::vector<f32> stereo = interleave({mono, mono});

        const LoudnessAnalysis monoResult = measure(mono, 1);
        const LoudnessAnalysis stereoResult = measure(stereo, 2);

        QVERIFY2(std::abs(monoResult.integratedLufs - (-3.003603743)) < kLufsTolerance,
                 qPrintable(QString("mono full scale 1 kHz read %1 LUFS, expected -3.003604")
                                    .arg(monoResult.integratedLufs, 0, 'f', 6)));
        QVERIFY2(std::abs(stereoResult.integratedLufs - 0.006696213) < kLufsTolerance,
                 qPrintable(QString("stereo full scale 1 kHz read %1 LUFS, expected +0.006696")
                                    .arg(stereoResult.integratedLufs, 0, 'f', 6)));

        // 3. The channel-weighting convention, as its own assertion.
        //
        // BS.1770 eq. (1) divides by N -- the block length of ONE channel -- and then
        // *sums* the weighted per-channel energies, so a stereo pair reads
        // 10*log10(2) louder than the identical signal read as mono. An implementation
        // that averaged over channels instead would land 3.0103 LU low here, which is
        // a plausible-looking wrong number rather than an obvious failure.
        //
        // Tolerance 1e-9 on a difference of two ~3-magnitude numbers; the two
        // accumulations are not bit-identical because the stereo quarter sum adds each
        // value twice in a different order, which is worth ~1e-13.
        const f64 channelDelta = stereoResult.integratedLufs - monoResult.integratedLufs;
        QVERIFY2(std::abs(channelDelta - 10.0 * std::log10(2.0)) < 1e-9,
                 qPrintable(QString("channel delta was %1 LU, expected %2")
                                    .arg(channelDelta, 0, 'f', 9)
                                    .arg(10.0 * std::log10(2.0), 0, 'f', 9)));

        // 4. The two peak fields are genuinely different measurements, not one number
        //    twice. A full-scale 1 kHz sine starting at phase 0 puts its crest exactly
        //    on sample 12, so there is no inter-sample peak to find and both figures
        //    must read 0.
        QVERIFY(stereoResult.samplePeakDbfs <= stereoResult.truePeakDbfs + 1e-9);
        QVERIFY2(std::abs(stereoResult.samplePeakDbfs) < kLufsTolerance,
                 qPrintable(QString("sample peak %1 dBFS, expected 0")
                                    .arg(stereoResult.samplePeakDbfs, 0, 'f', 6)));
        QVERIFY2(std::abs(stereoResult.truePeakDbfs) < kLufsTolerance,
                 qPrintable(QString("true peak %1 dBTP, expected 0")
                                    .arg(stereoResult.truePeakDbfs, 0, 'f', 6)));

        // ...and a 12 kHz tone whose crest falls midway between samples proves the
        // oversampler is doing something. At f = fs/4 the samples can only take the
        // values +/-cos(pi/4), so the sample peak is pinned *exactly* at -3.010300 dBFS
        // by arithmetic, while the reconstructed waveform genuinely reaches 0 dBTP.
        //
        // That gap is the entire reason Annex 2 exists: 3 dB of clipping that a
        // sample-peak meter reports as "3 dB of headroom". An implementation that
        // returned the sample peak for both fields would fail the delta below, and one
        // whose oversampler over-attenuated would fail it in the other direction.
        //
        // phi = -pi/4 puts the crest midway. The true peak is asserted as a bound
        // rather than an exact value because the implementation is edge-inclusive and
        // the zero-padded ramp rings on this signal's boundary step; measured, that
        // lifts it to +0.778098 dBTP where the interior-only figure is +0.000024. The
        // delta is therefore 3.788 edge-inclusive and 3.010 interior, and `> 3.0`
        // holds for both while still failing an identity implementation by 3 dB.
        constexpr f64 kQuarterRate = 12000.0;
        constexpr f64 kHalfSamplePhase = -std::numbers::pi_v<f64> / 4.0;
        const auto count = static_cast<usize>(std::llround(4.0 * kRate));
        std::vector<f32> quarterRate(count);
        for (usize index = 0; index < count; ++index) {
            const f64 phase = 2.0 * std::numbers::pi_v<f64> * kQuarterRate *
                                      static_cast<f64>(index) / kRate +
                              kHalfSamplePhase;
            quarterRate[index] = static_cast<f32>(std::sin(phase));
        }
        const LoudnessAnalysis interSample = measure(interleave({quarterRate, quarterRate}), 2);

        QVERIFY2(std::abs(interSample.samplePeakDbfs - (-3.010300)) < 0.01,
                 qPrintable(QString("sample peak %1 dBFS, expected -3.010300 = cos(pi/4)")
                                    .arg(interSample.samplePeakDbfs, 0, 'f', 6)));
        const f64 recovered = interSample.truePeakDbfs - interSample.samplePeakDbfs;
        QVERIFY2(recovered > 3.0,
                 qPrintable(QString("oversampling recovered only %1 dB of inter-sample peak")
                                    .arg(recovered, 0, 'f', 6)));
        QVERIFY2(interSample.truePeakDbfs > -0.5,
                 qPrintable(QString("true peak %1 dBTP; the reconstruction should reach ~0")
                                    .arg(interSample.truePeakDbfs, 0, 'f', 6)));

        // 5. The filter's own passband, checked without synthesising 96 kHz.
        QVERIFY2(vc::loudness::truePeakPassbandWorstDeviationDb() < 0.002,
                 qPrintable(QString("true-peak passband deviation %1 dB, measured 0.00088")
                                    .arg(vc::loudness::truePeakPassbandWorstDeviationDb(),
                                         0, 'f', 6)));
    }

    // The brief's "a -20 dBFS sine must measure about -20 LUFS", which is true to
    // 0.0067 LU -- and the reason is worth stating because it is not a coincidence.
    void minusTwentyDecibelSineMeasuresMinusTwentyLufs() {
        const std::vector<f32> mono = sineFrames(kMinus20DbfsPeakAmplitude, 4.0);
        const std::vector<f32> stereo = interleave({mono, mono});

        const LoudnessAnalysis stereoResult = measure(stereo, 2);
        const LoudnessAnalysis monoResult = measure(mono, 1);

        // -19.993304 rather than -20.000000, and the 0.0067 LU is BS.1770's own
        // arithmetic: the -0.691 offset in eq. (2) does not quite cancel the
        // K-weighting's +0.6977 dB at 1 kHz. Tolerance 0.002 LU, justified above --
        // and note it is *tighter than the 0.0067 LU residual*, so this is not a loose
        // "about -20" check wearing a tolerance as an excuse.
        QVERIFY2(std::abs(stereoResult.integratedLufs - kExpectedMinus20Lufs) < kLufsTolerance,
                 qPrintable(QString("stereo -20 dBFS-peak 1 kHz read %1 LUFS, expected %2")
                                    .arg(stereoResult.integratedLufs, 0, 'f', 9)
                                    .arg(kExpectedMinus20Lufs, 0, 'f', 9)));

        // The same signal read as mono is the R128 reference level: -23.003604 LUFS,
        // which is the -23 LUFS of EBU Tech 3342 to within 0.004 LU. That is the one
        // figure in this whole file anchored to something outside itself -- every
        // commercial loudness meter is calibrated to it -- which is why it is worth
        // asserting separately from the stereo reading rather than folding the two
        // into one tolerance.
        QVERIFY2(std::abs(monoResult.integratedLufs - (-23.003603643)) < kLufsTolerance,
                 qPrintable(QString("mono -20 dBFS-peak 1 kHz read %1 LUFS, expected -23.003604")
                                    .arg(monoResult.integratedLufs, 0, 'f', 9)));

        // A steady tone has no loudness range. Not a formality: LRA is percentiles
        // over the gated block distribution, and a percentile implementation that
        // collapsed to the mean would also report 0 for this. The dynamic case is what
        // makes LRA mean anything, and it is asserted in the gating test below rather
        // than here, to stay inside the four-function budget.
        QVERIFY2(std::abs(monoResult.loudnessRangeLu) < 1e-3,
                 qPrintable(QString("a steady tone reported LRA %1 LU")
                                    .arg(monoResult.loudnessRangeLu, 0, 'f', 6)));

        // Block accounting for a 4 s signal at 48 kHz with 400 ms blocks on a 100 ms
        // stride: floor((192000 - 19200) / 4800) + 1 = 37. Pinned because the stride is
        // this implementation's choice, not the recommendation's, so a future edit that
        // changed it would otherwise change every figure in this file silently.
        QCOMPARE(stereoResult.blocksConsidered, u64{37});
        QCOMPARE(stereoResult.blocksGatedIn, u64{37});
    }

    // 50% digital silence must not be averaged in. Two assertions: the mechanism
    // (blocks were excluded) and the behaviour (the number barely moved).
    void digitalSilenceIsGatedOutRatherThanAveragedIn() {
        const std::vector<f32> toneFrames = sineFrames(0.5, 4.0);

        // 4 s of tone followed by 4 s of digital silence, as ONE 8 s mono sequence that
        // is then duplicated to both channels.
        //
        // The obvious spelling of this -- interleave({toneFrames, silenceFrames}) -- is
        // wrong, and silently so, because `interleave` takes its length from
        // channels.front() and indexes every channel by the same frame number. That
        // does not concatenate the two lists in time; it builds a 4 s signal whose
        // LEFT channel is the tone and whose RIGHT channel is the silence. The result
        // is a hard-panned mono tone, not "tone then quiet", and every block count in
        // this test would be halved rather than wrong in an obvious direction.
        //
        // Concatenating first and duplicating after is what actually produces the
        // signal the assertions below describe: 384000 frames, tone in both channels
        // for the first 192000.
        std::vector<f32> toneThenSilenceMono(toneFrames.size() * 2, 0.0f);
        std::copy(toneFrames.begin(), toneFrames.end(), toneThenSilenceMono.begin());
        const std::vector<f32> toneThenSilence =
                interleave({toneThenSilenceMono, toneThenSilenceMono});

        const LoudnessAnalysis withSilence = measure(toneThenSilence, 2);
        // The comparison baseline: 4 s of the same tone and nothing else.
        const LoudnessAnalysis toneOnly = measure(interleave({toneFrames, toneFrames}), 2);

        // 4 s of tone followed by 4 s of digital silence: 384000 frames, so 77 blocks
        // on the 400 ms / 100 ms grid.
        QCOMPARE(withSilence.blocksConsidered, u64{77});

        // 40, not 41, and the difference is the whole reason this number is worth an
        // exact assertion rather than a `<`.
        //
        // There IS one decay-tail block: block 40 begins exactly where the tone stops,
        // so it contains nothing but the K-filters' own ring-down -- and it is real
        // signal, not leakage. Measured at -48.7085 LUFS, which clears the -70 LUFS
        // ABSOLUTE gate comfortably.
        //
        // It is then dropped by the RELATIVE gate, which sits 10 LU under the
        // absolutely-gated mean: -16.2871 LUFS here. The tail block is 32.42 LU below
        // that. `blocksGatedIn` is the count after BOTH gates (BS.1770 eq. 7, the J_g
        // set), so it is 40.
        //
        // 41 was the count after the absolute gate alone, which is a different set and
        // is not this field. Verified against an independent reimplementation of
        // BS.1770-4's two filters at the published 48 kHz coefficients, which also
        // reproduces this file's other figures exactly -- -6.179878 gated, -7.641152
        // ungated, 1.461274 LU of gating effect -- so the implementation is not what
        // needed changing. Do not "restore" 41; it is the absolute-gate count.
        QCOMPARE(withSilence.blocksGatedIn, u64{40});
        QVERIFY2(withSilence.blocksGatedIn < withSilence.blocksConsidered,
                 "no block was excluded, so nothing was gated");

        // The number barely moves: 0.166 LU. That residual is the K-weighting
        // filters' decay tail, NOT silence leaking through, and the distinction is the
        // point of the test -- a reader seeing "0.166 LU of silence got through" would
        // be wrong about why. Tolerance 0.25 LU: loose enough to absorb the tail,
        // 6x tighter than the 1.461 LU that ungated averaging produces.
        const f64 silenceDisplacement =
                std::abs(withSilence.integratedLufs - toneOnly.integratedLufs);
        QVERIFY2(silenceDisplacement < 0.25,
                 qPrintable(QString("digital silence moved the result by %1 LU")
                                    .arg(silenceDisplacement, 0, 'f', 6)));
        QVERIFY2(std::abs(withSilence.integratedLufs - (-6.179878)) < kLufsTolerance,
                 qPrintable(QString("tone+silence read %1 LUFS, expected -6.179878")
                                    .arg(withSilence.integratedLufs, 0, 'f', 6)));

        // The assertion that makes this test unpassable with gating broken.
        //
        // `ungatedLufs` is the same block distribution with no gates at all. Measured,
        // it reads 1.461274 LU below the gated answer -- so a build whose relative
        // gate did nothing, or whose absolute gate was a no-op, would be 1.46 LU out
        // and could not satisfy this. Asserting the gated value alone would not catch
        // that, because an ungated average happens to be merely plausible.
        QVERIFY2(std::abs(withSilence.ungatedLufs - (-7.641152)) < kLufsTolerance,
                 qPrintable(QString("ungated read %1 LUFS, expected -7.641152")
                                    .arg(withSilence.ungatedLufs, 0, 'f', 6)));
        const f64 gatingEffect =
                withSilence.integratedLufs - withSilence.ungatedLufs;
        QVERIFY2(gatingEffect > 1.0,
                 qPrintable(QString("gating only moved the result by %1 LU; a broken gate "
                                    "would leave this at 0")
                                    .arg(gatingEffect, 0, 'f', 6)));

        // LRA over the gated distribution. Steady tone, so ~0 even with the tail in it.
        QVERIFY2(withSilence.loudnessRangeLu < 0.01,
                 qPrintable(QString("tone+silence reported LRA %1 LU")
                                    .arg(withSilence.loudnessRangeLu, 0, 'f', 6)));
    }

    // Zero decibels must be a bitwise no-op, and a real gain must produce exactly the
    // expected samples. QCOMPARE on floats uses qFuzzyCompare and would accept a
    // 1-ULP difference, which is precisely the failure this codebase has been bitten
    // by twice, so every float comparison here is a u32 bit pattern.
    void gainApplicationIsExactAndZeroDecibelsTouchesNothing() {
        // Values chosen so each one would betray a wrong implementation:
        //   -0.0f and +0.0f  distinguish a pass-through from a multiply
        //   the infinities    would become NaN if the gain were applied twice
        //   a quiet NaN       catches a comparison-based early-out
        //   denorm_min/max    catch clamping or an accidental fabs-based normalise
        std::vector<f32> samples{
                1.0f,
                -1.0f,
                0.0f,
                -0.0f,
                0.5f,
                0.1f,
                -0.1f,
                0.25f,
                3.0f,
                std::numeric_limits<f32>::infinity(),
                -std::numeric_limits<f32>::infinity(),
                std::numeric_limits<f32>::quiet_NaN(),
                std::numeric_limits<f32>::denorm_min(),
                std::numeric_limits<f32>::max(),
        };
        const std::vector<f32> before = samples;

        // 1. 0 dB is a bitwise no-op, checked through linearGainFromDb rather than by
        //    handing 1.0f in directly -- otherwise the test would prove the multiply
        //    is exact and say nothing about the dB path that produces the factor.
        const auto unity = vc::loudness::linearGainFromDb(0.0);
        QVERIFY(unity.has_value());
        QCOMPARE(std::bit_cast<u32>(*unity), std::bit_cast<u32>(1.0f));

        QVERIFY(vc::loudness::applyGain(std::span<f32>(samples), *unity).has_value());
        QCOMPARE(samples.size(), before.size());
        for (usize index = 0; index < before.size(); ++index) {
            QVERIFY2(std::bit_cast<u32>(samples[index]) == std::bit_cast<u32>(before[index]),
                     qPrintable(QString("0 dB altered sample %1: %2 -> %3")
                                    .arg(index)
                                    .arg(static_cast<double>(before[index]))
                                    .arg(static_cast<double>(samples[index]))));
        }

        // 2. A real gain produces exactly the expected samples.
        //
        // 0.5f rather than a dB-derived factor, on purpose. Halving only decrements
        // the exponent, so `x * 0.5f` is exact for every normal f32 and the expected
        // values below are not approximations -- which is what makes a bitwise
        // assertion meaningful. (It is *not* exact for the two subnormals above, so
        // they are checked for bit preservation under a no-op gain instead, above.)
        //
        // The gain is applied to the already-verified untouched buffer, so this also
        // confirms the short-circuit left it usable rather than, say, leaving a
        // sentinel behind.
        QVERIFY(vc::loudness::applyGain(std::span<f32>(samples), 0.5f).has_value());
        for (usize index = 0; index < before.size(); ++index) {
            const f32 expected = before[index] * 0.5f;
            QVERIFY2(std::bit_cast<u32>(samples[index]) == std::bit_cast<u32>(expected),
                     qPrintable(QString("gain 0.5 gave %1 for sample %2, expected %3")
                                    .arg(static_cast<double>(samples[index]))
                                    .arg(index)
                                    .arg(static_cast<double>(expected))));
        }

        // 3. A non-unit gain is not silently skipped, which is the mistake a
        //    "treat 1.0 as a no-op" implementation makes in the other direction.
        QVERIFY(samples[0] != 1.0f);
        QCOMPARE(std::bit_cast<u32>(samples[0]), std::bit_cast<u32>(0.5f));

        // 4. The dB overload is not exact and does not pretend to be: 20*log10(0.5)
        //    has no f32 representation, so the round trip is checked to within a few
        //    ULP. This tolerance is a property of `pow`, not of this code -- which is
        //    the reason the linear overload above is the one that carries an exact
        //    assertion.
        const auto half = vc::loudness::linearGainFromDb(20.0 * std::log10(0.5));
        QVERIFY(half.has_value());
        QVERIFY2(std::abs(static_cast<double>(*half) - 0.5) < 1e-7,
                 qPrintable(QString("linearGainFromDb(-6.0206 dB) gave %1, expected 0.5")
                                    .arg(static_cast<double>(*half))));
        // Measured exact on this build's libm, but asserted loosely on purpose: a
        // 1-ULPO-correct pow is conforming and would fail a bitwise check here.
        QCOMPARE(vc::loudness::linearGainFromDb(1.0e9).error(), LoudnessError::GainOutOfRange);
        QVERIFY(!vc::loudness::linearGainFromDb(
                                   std::numeric_limits<f64>::quiet_NaN())
                         .has_value());

        // 5. The gain plan.
        //
        // On hand-built measurements, not synthesised ones, because `planGain` is a
        // pure function of the struct and the interesting case cannot be reached with a
        // steady tone: for a sine, integrated loudness and true peak are locked
        // together, so aiming a LOUD signal at a QUIETER target only ever asks for
        // attenuation, which no ceiling restricts. The case that needs the ceiling is a
        // quiet-but-peaky signal, and building one means synthesising a dynamic
        // programme to test four lines of arithmetic. Every value below is exactly
        // representable, so these are QCOMPARE against exact numbers.
        LoudnessAnalysis quietButPeaky;
        quietButPeaky.integratedLufs = -30.0;
        quietButPeaky.truePeakDbfs = -0.5;

        const auto plan = vc::loudness::planGain(quietButPeaky,
                                                 LoudnessTarget::StreamingMinus14);
        QVERIFY(plan.has_value());
        QCOMPARE(plan->targetLufs, -14.0);
        QCOMPARE(plan->ceilingDbtp, vc::loudness::kDefaultCeilingDbtp);
        QCOMPARE(plan->requestedGainDb, 16.0);
        QVERIFY2(plan->limitedByCeiling,
                 "+16 dB onto a -0.5 dBTP peak must be ceiling-limited");
        QVERIFY2(plan->alreadyOverCeiling,
                 "-0.5 dBTP is already above the -1.0 dBTP ceiling, which must be said");
        // The ceiling permits exactly ceiling - truePeak = -1.0 - (-0.5) = -0.5 dB.
        // So the plan *attenuates* a peak that was already hot, rather than raising a
        // quiet master -- which is the only direction that can be safe without a
        // limiter, and the reason `alreadyOverCeiling` exists as its own flag.
        QCOMPARE(plan->appliedGainDb, -0.5);
        QCOMPARE(plan->resultingTruePeakDbtp, -1.0);
        QCOMPARE(plan->resultingLufs, -30.5);
        // The honest-report contract: the achieved loudness is NOT the requested one,
        // and the caller is told rather than left to discover it in the finished file.
        QVERIFY(plan->resultingLufs < plan->targetLufs);
        QVERIFY(!plan->gainClampedToMaximum);

        // A signal with room to spare reaches the target untouched.
        LoudnessAnalysis comfortable;
        comfortable.integratedLufs = -18.0;
        comfortable.truePeakDbfs = -6.0;
        const auto comfortablePlan =
                vc::loudness::planGain(comfortable, LoudnessTarget::StreamingMinus14);
        QVERIFY(comfortablePlan.has_value());
        QCOMPARE(comfortablePlan->requestedGainDb, 4.0);
        // The ceiling would allow +5.0 dB here, so +4.0 is granted in full.
        QCOMPARE(comfortablePlan->appliedGainDb, 4.0);
        QVERIFY(!comfortablePlan->limitedByCeiling);
        QVERIFY(!comfortablePlan->alreadyOverCeiling);
        QCOMPARE(comfortablePlan->resultingLufs, -14.0);
        QCOMPARE(comfortablePlan->resultingTruePeakDbtp, -2.0);

        // A track with a -60 dBTP noise floor: the ceiling would permit +59 dB, which
        // is not a gain anyone wants, so kMaxGainDb bounds it and the flag says which
        // bound applied. Without this a -70 LUFS archival master gets amplified into
        // audible hiss and reported as a success.
        LoudnessAnalysis noiseFloor;
        noiseFloor.integratedLufs = -50.0;
        noiseFloor.truePeakDbfs = -60.0;
        const auto clamped = vc::loudness::planGain(noiseFloor,
                                                    LoudnessTarget::StreamingMinus14);
        QVERIFY(clamped.has_value());
        QCOMPARE(clamped->requestedGainDb, 36.0);
        QCOMPARE(clamped->appliedGainDb, vc::loudness::kMaxGainDb);
        QVERIFY(clamped->gainClampedToMaximum);
        QVERIFY(!clamped->limitedByCeiling);

        // Digital silence has no loudness and no peak, so there is nothing to gain
        // towards and +inf dB must be refused rather than reported.
        LoudnessAnalysis silent;
        silent.integratedLufs = -std::numeric_limits<f64>::infinity();
        silent.truePeakDbfs = -std::numeric_limits<f64>::infinity();
        QCOMPARE(vc::loudness::planGain(silent, LoudnessTarget::StreamingMinus14).error(),
                 LoudnessError::GainOutOfRange);

        // The named targets disagree, deliberately, and `Custom` has no number of its
        // own -- returning 0 LUFS there would silently aim at digital full scale.
        QCOMPARE(vc::loudness::loudnessTargetLufs(LoudnessTarget::StreamingMinus14), -14.0);
        QCOMPARE(vc::loudness::loudnessTargetLufs(LoudnessTarget::AppleMusicMinus16), -16.0);
        QCOMPARE(vc::loudness::loudnessTargetLufs(LoudnessTarget::BroadcastR128Minus23),
                 -23.0);
        QCOMPARE(vc::loudness::planGain(comfortable, LoudnessTarget::Custom).error(),
                 LoudnessError::GainOutOfRange);
        QCOMPARE(vc::loudness::planGain(comfortable, -15.5).value().resultingLufs, -15.5);
    }
};

#include "test_LoudnessAnalysis.moc"

int runTestLoudnessAnalysis(int argc, char** argv) {
    TestLoudnessAnalysis t;
    return QTest::qExec(&t, argc, argv);
}
