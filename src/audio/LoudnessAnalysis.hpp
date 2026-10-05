#pragma once
/**
 * @file LoudnessAnalysis.hpp
 * @file Purpose: EBU R128 / ITU-R BS.1770-4 loudness, true peak, LRA, and a
 *                clipping-safe normalisation gain. Pure, headless, device-free.
 *
 * @section Why this exists
 * Zero hits for `replaygain`, `ebur128` or `loudnorm` across the whole tree, so
 * this is net-new. It is the cheapest "you feel the difference in ten seconds"
 * feature available: Suno's own web player does no loudness normalisation, so a
 * playlist assembled from generated tracks plays at whatever level each master
 * happened to be delivered at.
 *
 * The important design constraint came from a measurement, not from taste. The
 * visualiser is fed raw PCM (`AudioEngine::processAudioBuffer` -> `AudioQueue::
 * pushAll` -> `VisualizerRenderer`), so there is no level metering in the running
 * product and none of the DSP a spectrum meter needs. What the product *does*
 * need is a measurement over a decoded file, and `AudioFileDecoder` can now hand
 * one over. So this is an offline analysis pass that both the playback path and
 * the render path can drive, deliberately free of Qt, of any audio device, and of
 * any file I/O beyond the spans it is handed -- which is the only reason any of it
 * is testable headlessly.
 *
 * @section The K-weighting coefficients, and where they come from
 * ITU-R BS.1770-4 (10/2015) is the reference; its current edition is BS.1770-5
 * (11/2023), whose loudness algorithm is unchanged. Two tables, both for 48 kHz:
 *
 *  - Table 1, "stage 1 of the pre-filter to model a spherical head" -- a
 *    shelving filter, +4 dB above roughly 2 kHz:
 *        b0 =  1.53512485958697
 *        b1 = -2.69169618940638
 *        b2 =  1.19839281085285
 *        a1 = -1.69065929318241
 *        a2 =  0.73248077421585
 *  - Table 2, the second stage, the RLB weighting curve -- a 2nd-order
 *    high-pass at 38.135 Hz:
 *        b0 =  1.0    b1 = -2.0    b2 = 1.0
 *        a1 = -1.99004745483398
 *        a2 =  0.99007225036621
 *
 * BS.1770 states those coefficients are *for 48 kHz* and that other rates "will
 * require different coefficient values, which should be chosen to provide the same
 * frequency response that the specified filter provides at 48 kHz". Rather than
 * transcribe one rate's numbers, `preFilterCoefficients()` and
 * `rlbHighPassCoefficients()` **derive** them at any rate from the normative
 * analogue parameters the recommendation defines -- stage 1: f0 =
 * 1681.974450955533 Hz, G = 3.999843853973347 dB, Q = 0.7071752369554196; stage 2:
 * f0 = 38.13547087602444 Hz, Q = 0.5003270373238773 -- through the bilinear
 * transform. Derived at 48 kHz this reproduces Table 1 and Table 2 to 9e-16 on
 * every b and a coefficient, verified directly against the published literals,
 * which is what makes the derivation safe to use in place of a transcription.
 * (It also caught one: a literal transcribed from memory came out as
 * a2 = 0.73248022221538, differing from the derived value in the seventh digit.
 * The published value is 0.73248077421585, so the derivation was right and the
 * recollection was not. Deriving beat transcribing.)
 *
 * The recommendation also says of these coefficients: "Tests have shown that the
 * performance of the algorithm is not sensitive to small variations in these
 * coefficients." That is true, and it is *not* a licence to be sloppy: measured on
 * this implementation, perturbing a single coefficient by 1e-4 moves the integrated
 * result by 0.005 to 0.051 LU, and by 1e-6 by 0.00005 to 0.0005 LU. A
 * plausible-looking wrong number is the failure mode that matters, so the test pins
 * the exact figure rather than asserting "roughly".
 *
 * @section The two conventions that are easy to get wrong, both measured
 *
 * 1. **Channel weights sum; they do not average.** BS.1770 eq. (1) is
 *    z = (1/N) * sum_i G_i * sum_n y_i(n)^2, where N is the block length in samples
 *    of **one** channel, and Table 3 gives G = 1.0 for L and R. So the same signal
 *    measured as a stereo pair reads exactly 10*log10(2) = 3.010300 LU louder than
 *    measured as mono. Measured here: -23.003604 LUFS mono against -16.983004 LUFS
 *    stereo for the identical -20 dBFS-peak 1 kHz sine.
 *
 * 2. **A biquad has memory, so an interleaved stereo buffer must not be run
 *    through a single filter.** Consecutive interleaved samples are *channels*, not
 *    consecutive instants within one channel, so one recursion over the interleave
 *    lets the left sample advance the state the right sample then reads. Measured
 *    cost of getting this wrong: **0.648226 LU** on that same signal, with the sign
 *    depending on phase -- larger than every other error in this file combined.
 *    Each channel gets its own state; see `KWeightingFilter`.
 *
 * @section Gating
 * BS.1770 eqs. (5)-(7): block loudness l_j = -0.691 + 10*log10(z_j) over 400 ms
 * blocks; the absolute gate G_a = -70 LUFS; the relative gate G_r = -0.691 +
 * 10*log10(mean of the absolutely-gated z) - 10; and the gated set
 * J_g = {j : l_j > G_r and l_j > G_a}. The 400 ms block is normative; the *stride*
 * is not, and 75% overlap (a 100 ms step) is used because it resolves transients a
 * 400 ms stride steps straight over.
 *
 * Silence needs no special case: a digitally silent block has z == 0 and fails
 * `l_j > G_a`, because log10(0) is excluded rather than evaluated. Measured on 4 s
 * of -6 dBFS stereo tone followed by 4 s of digital silence, gating moves the answer
 * **+1.4613 LU** relative to averaging every block. The residual -0.166 LU
 * difference from the tone alone is *not* silence leaking through: it is the
 * K-weighting filters' own decay tail, four blocks of which still sit above the
 * absolute gate after the tone stops and are legitimately included.
 *
 * `ungatedLufs` is reported next to `integratedLufs` so that "gating moved this by
 * 1.46 LU" is answerable. That is a diagnostic a user can act on, and it is also
 * the only way a test can assert that gating *happened* rather than accidentally
 * arriving at the right number.
 *
 * @section True peak: 4x oversampled, and why not the sample peak
 * BS.1770-4 Annex 2 defines true peak as the maximum of the signal in the
 * *continuous* time domain and requires an over-sampling filter. This implements the
 * 4x version.
 *
 * The reason is arithmetic, and it is not small. For a unit sinusoid whose crest
 * falls midway between two samples the sample peak is exactly cos(pi*f/fs), so the
 * inter-sample overshoot a sample-peak meter cannot see is:
 *
 *      1 kHz -> 0.019 dB      8 kHz ->  1.249 dB
 *      4 kHz -> 0.301 dB     12 kHz ->  3.010 dB   (f = fs/4, the worst case)
 *      6 kHz -> 0.688 dB     20 kHz -> 11.740 dB
 *
 * 3.01 dB exceeds what a mastering decision moves, and 11.7 dB at 20 kHz is not an
 * edge case. Reporting a sample peak as "true peak" would therefore lie in exactly
 * the case the number exists to answer: "will this clip?". Both figures are
 * reported, separately named, so neither can be mistaken for the other.
 *
 * The filter is a 4-phase polyphase Kaiser-windowed-sinc interpolator, 33 taps per
 * branch (132 multiply-accumulates per input sample). Design parameters, and what
 * they were measured to give:
 *
 *   - passband magnitude, 100 Hz to 20 kHz: **-0.00085 to +0.00088 dB**
 *   - worst error of the 4x peak estimate against the *analytic* oracle -- the
 *     ideal reconstruction of a unit sinusoid peaks at exactly 1.0, so any
 *     shortfall is pure filter error: **-0.00166 dB** across 0-20 kHz
 *   - per-branch DC gain: 1.0 to within 3.2e-8 in f32
 *
 * The oracle is analytic on purpose. Checking against a second filter design only
 * proves the two agree; checking a unit tone against 1.0 proves the filter is
 * right. An earlier attempt at this filter passed that test while being completely
 * broken: a cutoff term placed exactly on a zero of the sinc collapsed all 68 taps
 * onto one tap, making the filter a pure fractional delay -- a perfect allpass --
 * which is indistinguishable from a perfect lowpass if all you inspect is the
 * magnitude response. That is why both the passband and the estimate are pinned to
 * an absolute reference rather than to each other.
 *
 * Cost: 132 MAC per input sample per channel, so a three-minute stereo 48 kHz track
 * is ~2.3 GMAC. This is a one-shot pass over a decoded file, so that is seconds and
 * not a real-time concern -- which is exactly what buys the right to be accurate
 * instead of cheap.
 *
 * @section The filter's edge behaviour is conservative, and measured
 * A FIR needs samples on both sides of the point it reconstructs, so the buffer is
 * zero-padded and the reconstruction of the first and last `kTruePeakTapsPerPhase - 1`
 * samples ramps in from silence. For a signal with a *hard step* at the file boundary
 * that ramp rings, and the reported peak exceeds the continuous truth:
 *
 *   - a full-scale 12 kHz tone whose crest falls midway between samples reads
 *     **-3.010300 dBFS** as a sample peak (exactly cos(pi/4), as it must) and
 *     **+0.778098 dBTP** as a true peak, where the ideal reconstruction peaks at
 *     0.000000 dBTP;
 *   - excluding the ramped edges the same signal reads **+0.000024 dBTP**, which
 *     recovers the exact cos(pi*f/fs) deficit of 3.010300 to 2.5e-5 dB.
 *
 * So the estimate over-reports by up to ~0.78 dB on a boundary step and never
 * under-reports. That is the right direction for a "will this clip" question, and it
 * is not a defect to be engineered away: suppressing the ramp would mean discarding
 * the reconstruction of the first 33 samples, which could hide a real peak there.
 * Real programme material starts and ends near zero, so the ramp is silent in
 * practice -- a 1 kHz tone reads +0.000000 dBTP edge-inclusive, edge or not.
 *
 * @section Clipping after gain: the gain is capped, and there is no limiter here
 * `planGain()` computes the gain that would reach the target, then **caps it so the
 * post-gain true peak lands on the ceiling**, and reports what it gave up. That is
 * the whole answer, and the absence of a limiter is the decision rather than an
 * omission:
 *
 *  - A true-peak limiter is dynamic, time-varying and non-linear. Applying one here
 *    would mean the integrated loudness and LRA reported here no longer describe the
 *    signal that leaves -- a measurement layer quietly lying about its own output.
 *  - A limiter changes LRA, and LRA is one of the three numbers this file exists to
 *    report. Capping the gain does not alter the shape of the programme at all.
 *  - Real limiting is available and already linked: FFmpeg's `loudnorm` filter has a
 *    two-pass mode with a true-peak limiter, and `alimiter` exists. Both are *filter
 *    graphs*, the thing deliberately absent from this layer, and both belong on the
 *    encode path where a graph already exists.
 *  - Where the target and the ceiling genuinely conflict, the honest behaviour is to
 *    say so. `limitedByCeiling` is set and `resultingLufs` is the loudness actually
 *    achievable, so a UI can say "-14 LUFS would clip; applied +4.2 dB, reaching
 *    -17.4 LUFS at -1.0 dBTP" instead of quietly shipping a number that is not the
 *    one on screen.
 *
 * The default ceiling is **-1.0 dBTP**, not 0. Annex 2 exists precisely because a
 * lossy codec's decoder can reconstruct inter-sample values above the sample peak,
 * so a file peaking at exactly 0 dBFS can still clip in a listener's player. One
 * decibel of margin is the standard delivery allowance for that.
 *
 * @section Targets: there is no single standard, and pretending otherwise is wrong
 * The services do not agree, and neither does broadcast:
 *
 *   - Spotify        -14 LUFS integrated, normalised on playback
 *   - Amazon Music   -14 LUFS integrated
 *   - YouTube        loudness-normalised to about -14 LUFS
 *   - Apple Music    -16 LUFS integrated
 *   - Deezer         -15 LUFS integrated
 *   - EBU R128 / EBU Tech 3342 broadcast: -23 LUFS, the -23 LUFS reference level
 *
 * They differ too on whether a true-peak ceiling is specified at all. So the target
 * is a **named enum the user picks**, not a constant buried in this file, and
 * `Custom` exists so somebody can ask for -15.5. One hardcoded -14 would be wrong
 * for an Apple Music user, wrong for a broadcast deliverable, and dishonest about
 * the fact that nobody has settled this.
 *
 * EBU Tech 3342 additionally recommends an LRA of at most 9 LU for distribution.
 * That is reported, not enforced: this layer measures, and refusing a file because
 * its producer left 28 LU of dynamics in it is a product decision, not a DSP one.
 *
 * @section Why the gain is a scalar over PCM
 * The obvious alternative is a filter graph, and a measurement settles it: in the
 * FFmpeg 9.0.2 this project builds against, **`swr_set_volume` no longer exists** in
 * libswresample. The only volume control left is `rematrix_volume`, a parameter of
 * `swr_set_matrix`, and it applies solely where channel rematrixing occurs -- so it
 * cannot express a programme gain on a stereo-to-stereo path at all. A scalar
 * multiply is not the tidier option here; it is the only one available on this
 * build.
 *
 * It is also the right *place*. Applied to the decoded f32 buffer *before* it
 * reaches the existing `swr` context, it costs one multiply per sample on data that
 * already exists, needs no second pass, no graph and no new dependency, and
 * `VideoRecorderFFmpeg` gains nothing by being taught programme gain. And because a
 * scalar is a pure function of one number, it is exactly testable -- the property
 * the limiter above gives up.
 *
 * `applyGain()` takes a **linear** factor, not decibels, so the 0 dB case is a
 * structural no-op rather than an incidental one: `linearGainFromDb(0.0)` is exactly
 * 1.0f, and multiplying by exactly 1.0f preserves every bit of every f32, including
 * the sign of zero, both infinities and a quiet NaN. `applyGain()` short-circuits
 * anyway rather than leaning on that, because "exactly 1.0f" is a property of IEEE
 * 754 multiplication whereas "does not touch the buffer" is a property of the
 * function.
 *
 * Dither is deliberately absent, and it is not an oversight: the destination is f32
 * PCM throughout this path, so a gain change moves a mantissa and never
 * requantises. The day a gain feeds an s16 sink, dither becomes part of the same
 * change rather than a follow-up.
 *
 * @section Precision, and the one thing a caller must not do
 * Everything above the PCM is f64: coefficients, filter state, block accumulation,
 * the gated distribution, the peak dot products. Input samples are f32 -- that is
 * the format the decoders produce -- and are widened on entry. f32 accumulation
 * across a 400 ms block is not merely untidy: the block sum is 19200 terms and the
 * measurement budget here is 0.01 LU, which f32 has no room for.
 *
 * The recursion is Direct Form I with a0 normalised to 1, in f64. TDF-II is the more
 * robust choice in general; DF-I is used because it is the form every reference
 * figure in this file was measured against, and the poles sit far enough inside the
 * unit circle (a0 = 1.005 at 48 kHz) that f64 DF-I holds roughly twelve orders of
 * magnitude of headroom over that tolerance.
 *
 * A caller must not measure a mono file and a stereo file expecting one number; see
 * convention (1) above. `AudioFileDecoder` deliberately duplicates a mono source at
 * unity rather than at -3 dB, so a mono input arrives here as *stereo* and measures
 * 3.010300 LU louder than its true mono figure. That is not this layer compensating
 * for anything -- it is the swresample -3 dB rematrix trap documented in
 * `AudioFileDecoder.hpp` resurfacing as a loudness number, and it is why a -3.01 dB
 * disagreement with another meter deserves investigating before it is dismissed.
 */

#include "util/Types.hpp"

#include <expected>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace vc::loudness {

/// Why an analysis could not be produced. Named rather than a bare string so a
/// caller can branch on the class and a test can assert the *kind*, matching the
/// pattern `AudioFileDecoder` established.
enum class LoudnessError : u8 {
    /// Nothing measurable: either nothing was pushed, or every block fell below the
    /// -70 LUFS absolute gate so BS.1770's J_g is empty and the algorithm has no
    /// answer. Digital silence is the ordinary way to land here, and it genuinely has
    /// no integrated loudness.
    EmptySignal,
    /// Fewer frames than one 400 ms gating block. BS.1770's measurement interval
    /// is 400 ms, so a shorter signal has no valid integrated loudness, and
    /// inventing one from a partial block is the "reports a success it never had"
    /// shape this repo keeps being bitten by.
    SignalTooShort,
    /// Not 1 or 2 channels. The BS.1770 Table 3 surround weights are deliberately
    /// not implemented, because nothing here can produce a surround signal:
    /// `AudioFrame` is stereo-only (`AudioQueue.hpp:31`) and `AudioFileDecoder::
    /// kMaxChannels` is 2.
    UnsupportedChannelCount,
    /// Outside [kMinSampleRate, kMaxSampleRate]. Checked before any state is
    /// touched, so it cannot be blamed on the input.
    SampleRateOutOfRange,
    /// A non-finite sample arrived. Refused rather than folded into an average:
    /// one NaN poisons every block after it, and the result would be a confident
    /// wrong loudness.
    NonFiniteInput,
    /// A non-finite or out-of-range gain was requested.
    GainOutOfRange,
};

[[nodiscard]] std::string_view loudnessErrorName(LoudnessError error) noexcept;

// ─────────────────────────────────────────────────────────────────────────────
// K-weighting
// ─────────────────────────────────────────────────────────────────────────────

/// One normalised biquad section (a0 == 1), in Direct Form I.
///
/// BS.1770 Fig. 3 is the same topology: three input delays, three output delays,
/// one coefficient each.
struct BiquadCoefficients {
    f64 b0{1.0};
    f64 b1{0.0};
    f64 b2{0.0};
    f64 a1{0.0};
    f64 a2{0.0};
};

/// BS.1770-4 Table 1 (stage 1, the spherical-head shelf), derived at `sampleRate`.
///
/// At 48000 Hz this reproduces the published b and a coefficients to 9e-16.
[[nodiscard]] std::expected<BiquadCoefficients, LoudnessError>
preFilterCoefficients(f64 sampleRate) noexcept;

/// BS.1770-4 Table 2 (stage 2, the RLB weighting high-pass), derived at
/// `sampleRate`.
///
/// At 48000 Hz this reproduces the published coefficients to 2e-16.
[[nodiscard]] std::expected<BiquadCoefficients, LoudnessError>
rlbHighPassCoefficients(f64 sampleRate) noexcept;

/// K-weighting state for one channel.
///
/// One instance per channel, and that is the entire point: a single recursion over
/// an interleaved stereo buffer is wrong by a measured 0.648226 LU, because the
/// biquad's memory would carry the left channel into the right one.
class KWeightingFilter {
public:
    KWeightingFilter() = default;

    /// Installs coefficients from both stages. Clears prior state, so a reused
    /// instance cannot splice two signals together across a filter's decay.
    void configure(const BiquadCoefficients& preFilter,
                   const BiquadCoefficients& rlbHighPass) noexcept;

    /// Filters one sample. Deliberately not a range: the caller owns the
    /// de-interleave, because that is the step that has to be right.
    [[nodiscard]] f64 process(f64 sample) noexcept;

    void reset() noexcept;

private:
    BiquadCoefficients pre_{};
    BiquadCoefficients rlb_{};

    // BS.1770-4 Fig. 3, Direct Form I, a0 normalised to 1.
    f64 preX1_{0.0};
    f64 preX2_{0.0};
    f64 preY1_{0.0};
    f64 preY2_{0.0};
    f64 rlbX1_{0.0};
    f64 rlbX2_{0.0};
    f64 rlbY1_{0.0};
    f64 rlbY2_{0.0};

    /// BS.1770-4 Fig. 3: three input delays, three output delays, a0 normalised to 1
    /// and the a1/a2 signs applied here rather than stored negated -- so the stored
    /// coefficients are the published ones verbatim and a test can compare them
    /// against Table 1 and Table 2 without knowing a sign convention.
    static f64 directFormOne(f64 sample, const BiquadCoefficients& coefficients, f64& x1,
                             f64& x2, f64& y1, f64& y2) noexcept;
};

// ─────────────────────────────────────────────────────────────────────────────
// Result
// ─────────────────────────────────────────────────────────────────────────────

/// Everything one measurement produced.
///
/// The two peak fields are separately named on purpose. `samplePeakDbfs` is the
/// largest f32 sample; `truePeakDbfs` is the 4x oversampled estimate and is the
/// only one of the two that answers "will this clip". Merging them, or filing the
/// sample peak under the name "true peak", is the specific lie this type exists to
/// prevent.
struct LoudnessAnalysis {
    /// BS.1770 eq. (5): the gated programme loudness. Negative is quiet.
    f64 integratedLufs{0.0};

    /// The same block distribution averaged with **no** gating at all.
    ///
    /// Reported because "gating moved this by 1.46 LU" is a diagnostic a user can
    /// act on, and `integratedLufs` alone is not. It is also the only way a test
    /// can assert that gating happened rather than coincidentally reaching the
    /// right number.
    f64 ungatedLufs{0.0};

    /// Loudness range, EBU Tech 3342 s5: the 95th minus the 10th percentile of the
    /// absolutely-gated block loudness values. 0.0 for a steady signal.
    ///
    /// This is the "quiet or dynamic" number. EBU Tech 3342 recommends at most 9 LU
    /// for distribution; not enforced here, because refusing a file over its
    /// producer's choices is a product decision rather than a DSP one.
    f64 loudnessRangeLu{0.0};
    f64 loudnessRangeLowLufs{0.0};
    f64 loudnessRangeHighLufs{0.0};

    /// 4x oversampled peak, in dBTP (0 dBTP == 0 dBFS digital). BS.1770-4 Annex 2.
    /// May exceed 0.
    f64 truePeakDbfs{0.0};
    /// Largest raw sample magnitude, in dBFS. Never exceeds `truePeakDbfs`.
    f64 samplePeakDbfs{0.0};

    f64 sampleRate{0.0};
    u32 channels{0};
    u64 framesTotal{0};

    /// Blocks the 400 ms/100 ms grid actually covered.
    u64 blocksConsidered{0};
    /// Blocks that survived both gates. Less than `blocksConsidered` whenever
    /// anything sat below -70 LUFS -- which is the gating test's first assertion.
    u64 blocksGatedIn{0};

    /// The K-weighting actually applied, so a diagnostics view can show it rather
    /// than asserting a coefficient table by name.
    BiquadCoefficients preFilter{};
    BiquadCoefficients rlbHighPass{};
};

// ─────────────────────────────────────────────────────────────────────────────
// Analysis
// ─────────────────────────────────────────────────────────────────────────────

/// A streaming BS.1770-4 loudness measurement over interleaved f32 PCM.
///
/// Feed frames in order with `push()`, then call `result()` once. Memory is O(1) in
/// signal length apart from the gated block distribution, which is one f64 per
/// 100 ms and exists because LRA is percentiles over it and cannot be computed any
/// other way -- a ten-minute track costs 6000 doubles.
///
/// One instance measures one continuous stretch of audio. There is no public
/// `reset()` on purpose: splicing two files into one measurement yields a number
/// describing neither, and the class that would do that is the caller's to write
/// using two instances.
class LoudnessAnalyser {
public:
    static constexpr f64 kMinSampleRate = 8000.0;
    static constexpr f64 kMaxSampleRate = 384000.0;
    static constexpr u32 kMaxChannels = 2;

    /// BS.1770's measurement interval. Normative.
    static constexpr f64 kBlockSeconds = 0.400;
    /// Block stride. 75% overlap, which resolves transients a 400 ms stride steps
    /// straight over.
    static constexpr f64 kStepSeconds = 0.100;
    /// BS.1770 eq. (7): the absolute gate.
    static constexpr f64 kAbsoluteGateLufs = -70.0;
    /// BS.1770 eq. (6): the relative gate sits this far below the absolutely-gated
    /// mean.
    static constexpr f64 kRelativeGateOffsetLu = 10.0;
    /// BS.1770 eq. (2)'s -0.691. Exposed because a test has to reproduce it, and
    /// because it is the single easiest term in the whole algorithm to drop.
    static constexpr f64 kLoudnessOffset = -0.691;
    /// BS.1770 Table 3: G_L = G_R = 1.0.
    static constexpr f64 kChannelWeight = 1.0;

    LoudnessAnalyser();
    ~LoudnessAnalyser();

    LoudnessAnalyser(const LoudnessAnalyser&) = delete;
    LoudnessAnalyser& operator=(const LoudnessAnalyser&) = delete;
    LoudnessAnalyser(LoudnessAnalyser&&) noexcept;
    LoudnessAnalyser& operator=(LoudnessAnalyser&&) noexcept;

    /// Selects rate and channel count. Must precede the first `push()`.
    ///
    /// `sampleRate` is a double because a decoder hands out a rational rate, and
    /// rounding it before the filters see it is a silent level error: the
    /// coefficients are derived from the rate.
    void configure(f64 sampleRate, u32 channels) noexcept;

    /// Consumes interleaved frames. `frames` counts frames, not samples; a trailing
    /// partial frame is the caller's arithmetic and is not silently dropped here.
    ///
    /// Returns false once a non-finite sample has been seen. The instance is then
    /// poisoned and `result()` reports `NonFiniteInput`. Also false when the
    /// analyser was never configured, or when `interleaved` is shorter than
    /// `frames * channels` -- in both cases nothing was consumed and `result()` names
    /// the reason.
    ///
    /// Not `noexcept`: it appends to the gated block distribution, which allocates
    /// once per 100 ms of audio. Pretending otherwise would turn an out-of-memory
    /// condition into a terminate.
    [[nodiscard]] bool push(std::span<const f32> interleaved, usize frames);

    /// The measurement. Must be called once.
    ///
    /// Completes the true-peak filter's ring-down across kTruePeakTapsPerPhase-1
    /// frames of trailing silence, which is legitimate rather than pessimistic: a
    /// file is followed by silence in any real playback, and excluding the ring-down
    /// would under-report.
    [[nodiscard]] std::expected<LoudnessAnalysis, LoudnessError> result();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// ─────────────────────────────────────────────────────────────────────────────
// Gain planning
// ─────────────────────────────────────────────────────────────────────────────

/// Where to aim. An enum because the services disagree -- see the header note.
enum class LoudnessTarget : u8 {
    /// -14 LUFS. Spotify, Amazon Music, and roughly what YouTube normalises to.
    StreamingMinus14,
    /// -16 LUFS. Apple Music.
    AppleMusicMinus16,
    /// -23 LUFS. EBU R128 / EBU Tech 3342 broadcast, and the R128 reference level.
    BroadcastR128Minus23,
    /// Whatever the caller wants; there is no standard for it.
    Custom,
};

[[nodiscard]] std::string_view loudnessTargetName(LoudnessTarget target) noexcept;

/// The integrated loudness a named target asks for, in LUFS.
[[nodiscard]] f64 loudnessTargetLufs(LoudnessTarget target) noexcept;

/// Gain beyond this is refused rather than applied.
///
/// A track whose true peak sits at -60 dBTP has a -60 dBTP noise floor, and raising
/// it to a -1 dBTP ceiling amplifies that floor into audible hiss. 24 dB is the
/// largest correction a real mastering chain would attempt.
inline constexpr f64 kMaxGainDb = 24.0;

/// The default delivery ceiling. Not 0: BS.1770-4 Annex 2 exists because a lossy
/// decoder can reconstruct inter-sample values above the sample peak, so a 0 dBFS
/// file can still clip in a listener's player. 1 dB of margin is the convention.
inline constexpr f64 kDefaultCeilingDbtp = -1.0;

/// What a gain would cost in true peak, and what it would actually achieve.
///
/// The point of the type is that `appliedGainDb` and `resultingLufs` are the honest
/// numbers, and `requestedGainDb` is the one that might not be achievable.
struct GainPlan {
    /// Target minus measured. What you would apply with no constraints at all.
    f64 requestedGainDb{0.0};
    /// What to actually apply, after the ceiling and maximum-gain bounds.
    f64 appliedGainDb{0.0};

    /// `integratedLufs + appliedGainDb`: the loudness the file will have.
    f64 resultingLufs{0.0};
    /// `truePeakDbfs + appliedGainDb`: the loudest reconstructed sample after gain.
    f64 resultingTruePeakDbtp{0.0};

    f64 targetLufs{0.0};
    f64 ceilingDbtp{0.0};

    /// The true-peak ceiling bound the gain. Check this first: it is the case a
    /// user must be told about, because the number on screen is not the number they
    /// will get.
    bool limitedByCeiling{false};
    /// `kMaxGainDb` bound the gain instead.
    bool gainClampedToMaximum{false};
    /// The signal already exceeds the ceiling at unity gain, so this plan is
    /// *attenuating* a hot master rather than raising a quiet one. No amount of gain
    /// in the other direction fixes that; only a limiter can.
    bool alreadyOverCeiling{false};
};

/// Computes a gain plan. Pure: no I/O, no state, no side effects.
[[nodiscard]] std::expected<GainPlan, LoudnessError>
planGain(const LoudnessAnalysis& analysis, f64 targetLufs,
         f64 ceilingDbtp = kDefaultCeilingDbtp);

/// `planGain` against a named target.
[[nodiscard]] std::expected<GainPlan, LoudnessError>
planGain(const LoudnessAnalysis& analysis, LoudnessTarget target,
         f64 ceilingDbtp = kDefaultCeilingDbtp);

// ─────────────────────────────────────────────────────────────────────────────
// Gain application
// ─────────────────────────────────────────────────────────────────────────────

/// dB to linear amplitude factor, as f32.
///
/// Exactly 1.0f for an input of exactly 0.0, which is what lets a 0 dB application
/// be a structural no-op rather than a lucky one. `GainOutOfRange` for a
/// non-finite input, or `|db|` above 200: f32 overflows somewhere around 192 dB and
/// a silent multiply by infinity is not a gain.
[[nodiscard]] std::expected<f32, LoudnessError> linearGainFromDb(f64 decibels) noexcept;

/// Multiplies every sample by `linearGain`, in place.
///
/// Bit-exact: a factor of exactly 1.0f leaves every bit of every f32 alone,
/// including the sign of zero, both infinities and a quiet NaN. It short-circuits
/// rather than relying on that, so the guarantee belongs to the function and not to
/// IEEE 754 multiplication.
[[nodiscard]] std::expected<void, LoudnessError>
applyGain(std::span<f32> interleaved, f32 linearGain) noexcept;

/// `applyGain` with the factor derived from decibels.
///
/// Not bit-exactly testable away from 0 dB, because `pow` is not exact and no
/// 6.0206 dB value maps to a representable 0.5f on every libm. It exists for
/// convenience; a caller that needs an exact result takes a linear factor.
[[nodiscard]] std::expected<void, LoudnessError> applyGainDb(std::span<f32> interleaved,
                                                            f64 decibels) noexcept;

// ─────────────────────────────────────────────────────────────────────────────
// True-peak filter, exposed for inspection
// ─────────────────────────────────────────────────────────────────────────────

/// 4x oversampling factor. BS.1770-4 Annex 2 requires at least 4.
inline constexpr u32 kOversamplingFactor = 4;
/// Taps per polyphase branch. 132 MAC per input sample per channel.
inline constexpr usize kTruePeakTapsPerPhase = 33;

/// The filter, for a test or a diagnostics view. Rows are the four phases; the
/// coefficients are exactly the f32 values `LoudnessAnalyser` uses, promoted to f64
/// without further rounding.
///
/// Built as g_p[i] = sinc(i + p/4) with a Kaiser window (beta = 8) about each
/// branch's own centre, then normalised so each branch sums to exactly 1. No cutoff
/// tilt term is applied, and the reason is worth recording: adding one costs
/// accuracy, not saves it. Measured worst-case 4x estimate error over 0-20 kHz was
/// -2.889 dB at cutoff 0.90 versus **-0.00166 dB** at 1.00, because a tilted cutoff
/// rolls the passband off early and the shortfall *is* an under-read peak.
[[nodiscard]] std::span<const f64> truePeakPhase(u32 phase) noexcept;

/// Worst magnitude deviation across the 100 Hz-20 kHz passband, in dB, measured on
/// this table: 0.00088 dB. Exposed so the filter can be checked without synthesising
/// 96 kHz of audio.
[[nodiscard]] f64 truePeakPassbandWorstDeviationDb() noexcept;

} // namespace vc::loudness
