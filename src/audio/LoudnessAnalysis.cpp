#include "audio/LoudnessAnalysis.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <utility>

namespace vc::loudness {

namespace {

// ─────────────────────────────────────────────────────────────────────────────
// K-weighting: BS.1770-4's normative analogue parameters.
//
// These are the primary source. BS.1770 publishes the 48 kHz direct-form
// coefficients in Tables 1 and 2 as a convenience, but the recommendation is
// explicit that other rates "should be chosen to provide the same frequency
// response that the specified filter provides at 48 kHz" -- i.e. the frequency
// response is the specification and the coefficient table is one realisation of
// it. Deriving from the analogue parameters is the faithful reading, not a
// shortcut.
//
// Derived at 48000 Hz these reproduce the published tables to 9e-16 (pre-filter b
// and a) and 2e-16 (RLB a), checked against the published literals directly.
// ─────────────────────────────────────────────────────────────────────────────

/// BS.1770-4 Table 1, stage-1 shelving filter: f0, gain, Q.
constexpr f64 kPreFilterF0Hz = 1681.974450955533;
constexpr f64 kPreFilterGainDb = 3.999843853973347;
constexpr f64 kPreFilterQ = 0.7071752369554196;

/// The exponent BS.1770's own derivation applies to the shelf gain when forming
/// the numerator's intermediate term.
///
/// Validated rather than guessed: it is the value that reproduces the published
/// Table 1 to 9e-16. Worth being explicit that the resulting DC gain is 0.99996
/// rather than exactly 1.0 -- b0+b1+b2 = 0.04181986 against a0+a1+a2 = 0.04182148
/// -- and that the published table carries the very same discrepancy to the same
/// digits. So the derivation reproduces the standard rather than an idealised
/// version of it.
constexpr f64 kPreFilterVbExponent = 0.4996667741545416;

/// BS.1770-4 Table 2, RLB weighting high-pass: f0 and Q. Q sits a hair under 0.5,
/// which is the critically damped case (1/Q = 2 cos(theta) as theta -> 0).
constexpr f64 kRlbHighPassF0Hz = 38.13547087602444;
constexpr f64 kRlbHighPassQ = 0.5003270373238773;

constexpr usize kTaps = kTruePeakTapsPerPhase;
constexpr u32 kPhases = kOversamplingFactor;

/// The rate `truePeakPassbandWorstDeviationDb()` measures at. The table itself is
/// rate-independent -- it is a fractional-delay interpolator, and the rate only
/// decides which input frequencies map to which -- so this is a convenience, not
/// a parameter of the filter.
constexpr f64 kTruePeakProbeRate = 48000.0;

/// g_p[i] = sinc(i + p/4), Kaiser-windowed (beta = 8) about branch p's own centre,
/// each branch normalised to sum to exactly 1. 33 taps per branch.
///
/// Two design choices that were not obvious, both settled by measurement:
///
///  - Worst error of the 4x peak estimate against the *analytic* oracle -- a unit
///    sinusoid's ideal reconstruction peaks at exactly 1.0, so any shortfall is
///    pure filter error -- is -0.00166 dB across 0-20 kHz. Cost: 132 MAC per input
///    sample per channel, so ~2.3 GMAC for a three-minute stereo 48 kHz track. A
///    one-shot offline pass, which is what buys the right to be accurate rather
///    than cheap.
///
///  - No cutoff tilt term. Tilt was swept across 17/21/25/29 taps and five Kaiser
///    betas and is strictly worse here: at cutoff 0.90 the worst 0-20 kHz error is
///    -2.889 dB against -0.00166 dB at 1.00. Tilt rolls the passband off early,
///    and a rolled-off passband *is* an under-read peak.
///
/// Phase 0's off-centre taps are exact zeros, written as such: sinc(integer) is
/// exactly 0 and the computed table held only 1e-17 of rounding noise there.
/// Substituting 0 moved the measured estimate error by nothing at six decimal
/// places, and it makes the Kronecker delta legible.
constexpr std::array<std::array<f64, kTaps>, kPhases> kTruePeakTable{{
    {// phase 0
     {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
      1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0}},
    {// phase 1
     {-5.21779439e-05, 0.000188941674, -0.000468045706, 0.000969248242, -0.00179552508,
      0.00307568163, -0.00496855658, 0.00767165329, -0.0114399167, 0.0166267976,
      -0.023775816, 0.0338370651, -0.0487402454, 0.0732271075, -0.122971512, 0.297645718,
      0.899489045, -0.175991729, 0.0928694308, -0.0592599474, 0.0404921472,
      -0.0283559971, 0.0199101921, -0.0138281481, 0.00940357801, -0.00620456832,
      0.0039342423, -0.00236971467, 0.00133434404, -0.000685020117, 0.000306233356,
      -0.000106908861, 3.23949971e-05}},
    {// phase 2
     {-0.00010829878, 0.000343132939, -0.000804565963, 0.00161329214, -0.00292368303,
      0.00492780283, -0.00786243007, 0.0120238606, -0.0178001877, 0.0257420465,
      -0.0367220081, 0.0523200631, -0.0758842006, 0.116148449, -0.20532681,
      0.634290993, 0.634290993, -0.20532681, 0.116148449, -0.0758842006, 0.0523200631,
      -0.0367220081, 0.0257420465, -0.0178001877, 0.0120238606, -0.00786243007,
      0.00492780283, -0.00292368303, 0.00161329214, -0.000804565963, 0.000343132939,
      -0.00010829878, 4.51190863e-05}},
    {// phase 3
     {-0.000106908963, 0.000306233647, -0.000685020816, 0.00133434532, -0.00236971676,
      0.00393424602, -0.00620457437, 0.00940358732, -0.0138281621, 0.0199102107,
      -0.028356025, 0.0404921845, -0.0592600033, 0.0928695202, -0.175991908,
      0.899489939, 0.297646016, -0.122971632, 0.073227182, -0.0487402938,
      0.0338370986, -0.0237758383, 0.0166268144, -0.0114399279, 0.00767166074,
      -0.0049685617, 0.00307568442, -0.00179552683, 0.000969249231, -0.000468046172,
      0.000188941864, -5.21779948e-05, 3.14280114e-05}},
}};

/// Bilinear-transformed second-order shelving section, BS.1770 Table 1.
[[nodiscard]] BiquadCoefficients derivePreFilter(f64 sampleRate) noexcept {
    const f64 K = std::tan(std::numbers::pi_v<f64> * kPreFilterF0Hz / sampleRate);
    const f64 Vh = std::pow(10.0, kPreFilterGainDb / 20.0);
    const f64 Vb = std::pow(Vh, kPreFilterVbExponent);
    const f64 a0 = 1.0 + K / kPreFilterQ + K * K;

    BiquadCoefficients c;
    c.b0 = (Vh + Vb * K / kPreFilterQ + K * K) / a0;
    c.b1 = 2.0 * (K * K - Vh) / a0;
    c.b2 = (Vh - Vb * K / kPreFilterQ + K * K) / a0;
    c.a1 = 2.0 * (K * K - 1.0) / a0;
    c.a2 = (1.0 - K / kPreFilterQ + K * K) / a0;
    return c;
}

/// Bilinear-transformed RLB weighting high-pass, BS.1770 Table 2.
[[nodiscard]] BiquadCoefficients deriveRlbHighPass(f64 sampleRate) noexcept {
    const f64 K = std::tan(std::numbers::pi_v<f64> * kRlbHighPassF0Hz / sampleRate);
    const f64 a0 = 1.0 + K / kRlbHighPassQ + K * K;

    BiquadCoefficients c;
    c.b0 = 1.0;
    c.b1 = -2.0;
    c.b2 = 1.0;
    c.a1 = 2.0 * (K * K - 1.0) / a0;
    c.a2 = (1.0 - K / kRlbHighPassQ + K * K) / a0;
    return c;
}

[[nodiscard]] bool rateInRange(f64 rate) noexcept {
    return std::isfinite(rate) && rate >= LoudnessAnalyser::kMinSampleRate &&
           rate <= LoudnessAnalyser::kMaxSampleRate;
}

/// `std::unexpected` spelled once, matching the identical helper in
/// `AudioFileDecoder.cpp`: libc++'s `expected` has no implicit conversion from the
/// error type, so every error return would otherwise repeat the wrapping.
[[nodiscard]] std::unexpected<LoudnessError> asUnexpected(LoudnessError error) noexcept {
    return std::unexpected<LoudnessError>(error);
}

/// BS.1770 eq. (2)'s mapping from block mean square to loudness.
[[nodiscard]] f64 blockLoudness(f64 meanSquare) noexcept {
    return LoudnessAnalyser::kLoudnessOffset + 10.0 * std::log10(meanSquare);
}

/// Percentile of an ascending vector, linearly interpolated between closest ranks
/// (EBU Tech 3342 s5's convention). Callers pass an already-sorted span.
[[nodiscard]] f64 percentile(std::span<const f64> ascending, f64 fraction) noexcept {
    if (ascending.empty()) {
        return 0.0;
    }
    if (ascending.size() == 1) {
        return ascending.front();
    }
    const f64 position = fraction * static_cast<f64>(ascending.size() - 1);
    const usize low = static_cast<usize>(std::floor(position));
    const usize high = std::min(low + 1, ascending.size() - 1);
    return ascending[low] +
           (ascending[high] - ascending[low]) * (position - static_cast<f64>(low));
}

/// f32 sample magnitude in dBFS, or -inf for digital silence.
[[nodiscard]] f64 peakToDb(f64 peak) noexcept {
    return peak > 0.0 ? 20.0 * std::log10(peak) : -std::numeric_limits<f64>::infinity();
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Errors, targets
// ─────────────────────────────────────────────────────────────────────────────

std::string_view loudnessErrorName(LoudnessError error) noexcept {
    switch (error) {
        case LoudnessError::EmptySignal:
            return "EmptySignal";
        case LoudnessError::SignalTooShort:
            return "SignalTooShort";
        case LoudnessError::UnsupportedChannelCount:
            return "UnsupportedChannelCount";
        case LoudnessError::SampleRateOutOfRange:
            return "SampleRateOutOfRange";
        case LoudnessError::NonFiniteInput:
            return "NonFiniteInput";
        case LoudnessError::GainOutOfRange:
            return "GainOutOfRange";
    }
    return "UnrecognisedLoudnessError";
}

std::string_view loudnessTargetName(LoudnessTarget target) noexcept {
    switch (target) {
        case LoudnessTarget::StreamingMinus14:
            return "StreamingMinus14";
        case LoudnessTarget::AppleMusicMinus16:
            return "AppleMusicMinus16";
        case LoudnessTarget::BroadcastR128Minus23:
            return "BroadcastR128Minus23";
        case LoudnessTarget::Custom:
            return "Custom";
    }
    return "UnrecognisedLoudnessTarget";
}

f64 loudnessTargetLufs(LoudnessTarget target) noexcept {
    switch (target) {
        case LoudnessTarget::StreamingMinus14:
            return -14.0;
        case LoudnessTarget::AppleMusicMinus16:
            return -16.0;
        case LoudnessTarget::BroadcastR128Minus23:
            return -23.0;
        case LoudnessTarget::Custom:
            // Custom carries its own value through the f64 overload; there is no
            // number to report here and inventing one would be a lie.
            break;
    }
    return 0.0;
}

// ─────────────────────────────────────────────────────────────────────────────
// K-weighting
// ─────────────────────────────────────────────────────────────────────────────

std::expected<BiquadCoefficients, LoudnessError>
preFilterCoefficients(f64 sampleRate) noexcept {
    if (!rateInRange(sampleRate)) {
        return asUnexpected(LoudnessError::SampleRateOutOfRange);
    }
    return derivePreFilter(sampleRate);
}

std::expected<BiquadCoefficients, LoudnessError>
rlbHighPassCoefficients(f64 sampleRate) noexcept {
    if (!rateInRange(sampleRate)) {
        return asUnexpected(LoudnessError::SampleRateOutOfRange);
    }
    return deriveRlbHighPass(sampleRate);
}

f64 KWeightingFilter::directFormOne(f64 sample, const BiquadCoefficients& c, f64& x1, f64& x2,
                                    f64& y1, f64& y2) noexcept {
    // BS.1770-4 Fig. 3, with a0 normalised to 1 and the a1/a2 signs folded in here
    // rather than stored negated, so the stored coefficients are the published ones
    // verbatim and a test can compare them against the table without a sign rule.
    const f64 y = c.b0 * sample + c.b1 * x1 + c.b2 * x2 - c.a1 * y1 - c.a2 * y2;
    x2 = x1;
    x1 = sample;
    y2 = y1;
    y1 = y;
    return y;
}

void KWeightingFilter::configure(const BiquadCoefficients& preFilter,
                                 const BiquadCoefficients& rlbHighPass) noexcept {
    pre_ = preFilter;
    rlb_ = rlbHighPass;
    reset();
}

void KWeightingFilter::reset() noexcept {
    preX1_ = preX2_ = preY1_ = preY2_ = 0.0;
    rlbX1_ = rlbX2_ = rlbY1_ = rlbY2_ = 0.0;
}

f64 KWeightingFilter::process(f64 sample) noexcept {
    const f64 shelved = directFormOne(sample, pre_, preX1_, preX2_, preY1_, preY2_);
    return directFormOne(shelved, rlb_, rlbX1_, rlbX2_, rlbY1_, rlbY2_);
}

// ─────────────────────────────────────────────────────────────────────────────
// Analysis
// ─────────────────────────────────────────────────────────────────────────────

struct LoudnessAnalyser::Impl {
    // Configuration. `configure` is the only way in and it cannot report an error,
    // so a bad rate or channel count is recorded here and surfaced by `result()`.
    f64 sampleRate{0.0};
    u32 channels{0};
    bool poisoned{false};
    BiquadCoefficients preFilter{};
    BiquadCoefficients rlbHighPass{};
    std::array<KWeightingFilter, LoudnessAnalyser::kMaxChannels> filters{};

    // Block grid. `blockFrames` is defined as 4 * `stepFrames` rather than rounded
    // independently from 0.400 s, so the four-quarter window below is exact even at
    // a rate where the two roundings disagree. Both are exact at every common rate
    // (48000 -> 19200/4800, 44100 -> 17640/4410, 96000 -> 38400/9600).
    u64 stepFrames{0};
    u64 blockFrames{0};

    // Quarter-ring window sum. Five slots because a block needs the four most
    // recently completed quarters and the writer must not land on one of them.
    std::array<f64, 5> quarterSum{};
    f64 quarterAccumulator{0.0};
    u64 quartersCompleted{0};
    u64 framesInQuarter{0};
    std::vector<f64> blockMeanSquares;

    // Peaks.
    f64 samplePeak{0.0};
    f64 truePeak{0.0};
    std::array<std::array<f32, kTaps>, LoudnessAnalyser::kMaxChannels> truePeakHistory{};
    std::array<usize, LoudnessAnalyser::kMaxChannels> truePeakHead{};

    u64 framesTotal{0};

    /// Feeds one sample of one channel to the 4x interpolator and folds the four
    /// reconstructed sub-samples into the running true-peak estimate.
    ///
    /// y[4n + p] = sum_k g_p[k] * x[n - k], so the ring's newest entry is x[n] and
    /// the oldest is x[n - kTaps + 1]. Unwritten slots hold 0.0f, which is exactly
    /// the zero padding a real signal has before it starts -- so the filter's
    /// start-up needs no special case.
    void advanceTruePeak(u32 channel, f64 sample) noexcept {
        auto& history = truePeakHistory[channel];
        const usize written = truePeakHead[channel];
        history[written] = static_cast<f32>(sample);
        const usize next = (written + 1) % kTaps;
        truePeakHead[channel] = next;

        // Walk backwards from the newest tap with a decrementing index rather than a
        // modulo per tap: 132 divisions per sample is not a cost worth paying on the
        // hot path for a table lookup that is already sequential.
        usize index = (next + kTaps - 1) % kTaps;
        for (u32 phase = 0; phase < kPhases; ++phase) {
            const auto& coefficients = kTruePeakTable[phase];
            f64 accumulator = 0.0;
            for (usize tap = 0; tap < kTaps; ++tap) {
                accumulator += coefficients[tap] * static_cast<f64>(history[index]);
                index = (index == 0) ? kTaps - 1 : index - 1;
            }
            truePeak = std::max(truePeak, std::abs(accumulator));
        }
    }

    /// Runs the interpolator's ring-down across trailing silence.
    ///
    /// A file is followed by silence in any real playback, so the reconstruction of
    /// its final samples legitimately rings down into that silence and the peaks
    /// found there are real. Skipping it would under-report, which for a
    /// "will this clip" question is the wrong direction to be wrong in.
    f64 completeTruePeak() noexcept {
        for (usize frame = 0; frame + 1 < kTaps; ++frame) {
            for (u32 channel = 0; channel < channels; ++channel) {
                advanceTruePeak(channel, 0.0);
            }
        }
        return truePeak;
    }

    void commitQuarter() {
        quarterSum[quartersCompleted % quarterSum.size()] = quarterAccumulator;
        quarterAccumulator = 0.0;
        ++quartersCompleted;
        if (quartersCompleted < 4) {
            return;
        }
        const u64 first = quartersCompleted - 4;
        f64 total = 0.0;
        for (u64 offset = 0; offset < 4; ++offset) {
            total += quarterSum[(first + offset) % quarterSum.size()];
        }
        // BS.1770 eq. (1): z = (1/N) * sum_i G_i * sum_n y_i(n)^2, with N the block
        // length of ONE channel and G = 1.0. The per-channel weights were folded in
        // by the caller as each frame arrived, so `total` is already the weighted
        // sum and only the 1/N is left.
        blockMeanSquares.push_back(total / static_cast<f64>(blockFrames));
    }

    void resetAccumulators() noexcept {
        quarterSum.fill(0.0);
        quarterAccumulator = 0.0;
        quartersCompleted = 0;
        framesInQuarter = 0;
        blockMeanSquares.clear();
        samplePeak = 0.0;
        truePeak = 0.0;
        framesTotal = 0;
        poisoned = false;
        for (auto& filter : filters) {
            filter.reset();
        }
        for (auto& history : truePeakHistory) {
            history.fill(0.0f);
        }
        truePeakHead.fill(0);
    }
};

LoudnessAnalyser::LoudnessAnalyser() : impl_(std::make_unique<Impl>()) {}
LoudnessAnalyser::~LoudnessAnalyser() = default;
LoudnessAnalyser::LoudnessAnalyser(LoudnessAnalyser&&) noexcept = default;
LoudnessAnalyser& LoudnessAnalyser::operator=(LoudnessAnalyser&&) noexcept = default;

void LoudnessAnalyser::configure(f64 sampleRate, u32 channels) noexcept {
    Impl& impl = *impl_;
    impl.sampleRate = sampleRate;
    impl.channels = channels;

    // Cleared before the range check, not inside it. Leaving the previous grid in
    // place on a failed reconfigure would let `push` keep consuming samples against
    // coefficients derived from a *different* rate -- silently measuring at one rate
    // while claiming another, which is the kind of plausible wrong number this whole
    // file exists to avoid producing.
    impl.stepFrames = 0;
    impl.blockFrames = 0;

    const bool usable = rateInRange(sampleRate) && channels >= 1 &&
                        channels <= kMaxChannels;
    if (usable) {
        const auto pre = preFilterCoefficients(sampleRate);
        const auto rlb = rlbHighPassCoefficients(sampleRate);
        // Guarded by the same range check as `usable`, so both hold a value.
        if (pre.has_value() && rlb.has_value()) {
            impl.preFilter = *pre;
            impl.rlbHighPass = *rlb;
            impl.stepFrames = static_cast<u64>(std::llround(sampleRate * kStepSeconds));
            impl.blockFrames = 4 * impl.stepFrames;
            for (auto& filter : impl.filters) {
                filter.configure(impl.preFilter, impl.rlbHighPass);
            }
        }
    }

    // configure() doubles as a restart: re-running it begins a fresh measurement, so a
    // caller can measure two signals with one instance without a separate reset() that
    // could be forgotten.
    impl.resetAccumulators();
}

bool LoudnessAnalyser::push(std::span<const f32> interleaved, usize frames) {
    Impl& impl = *impl_;
    if (impl.poisoned) {
        return false;
    }
    const u32 channels = impl.channels;
    if (channels < 1 || channels > kMaxChannels || impl.stepFrames == 0) {
        // Not configured. `result()` names the reason; refusing here keeps the
        // accumulators from filling with nonsense in the meantime.
        return false;
    }
    if (interleaved.size() < frames * channels) {
        return false;
    }

    constexpr f64 weight = kChannelWeight;

    for (usize frame = 0; frame < frames; ++frame) {
        for (u32 channel = 0; channel < channels; ++channel) {
            const f32 raw = interleaved[frame * channels + channel];
            if (!std::isfinite(raw)) {
                // One NaN poisons every block after it, and the result would be a
                // confidently wrong loudness. Refused rather than averaged.
                impl.poisoned = true;
                return false;
            }
            const f64 sample = static_cast<f64>(raw);

            impl.samplePeak = std::max(impl.samplePeak, std::abs(sample));

            // Per channel, with per-channel state. This is the whole reason
            // KWeightingFilter exists as a one-channel object.
            const f64 weighted = impl.filters[channel].process(sample);
            impl.quarterAccumulator += weight * weighted * weighted;

            impl.advanceTruePeak(channel, sample);
        }

        ++impl.framesTotal;
        ++impl.framesInQuarter;
        if (impl.framesInQuarter == impl.stepFrames) {
            impl.framesInQuarter = 0;
            impl.commitQuarter();
        }
    }
    return true;
}

std::expected<LoudnessAnalysis, LoudnessError> LoudnessAnalyser::result() {
    Impl& impl = *impl_;

    if (impl.poisoned) {
        return asUnexpected(LoudnessError::NonFiniteInput);
    }
    if (!rateInRange(impl.sampleRate)) {
        return asUnexpected(LoudnessError::SampleRateOutOfRange);
    }
    if (impl.channels < 1 || impl.channels > kMaxChannels) {
        return asUnexpected(LoudnessError::UnsupportedChannelCount);
    }
    if (impl.framesTotal == 0) {
        return asUnexpected(LoudnessError::EmptySignal);
    }
    if (impl.blockMeanSquares.empty()) {
        return asUnexpected(LoudnessError::SignalTooShort);
    }

    const f64 truePeak = impl.completeTruePeak();

    // The absolute gate. A digitally silent block has z == 0 and is excluded rather
    // than having log10(0) evaluated -- which is why silence needs no special case.
    std::vector<f64> absolutelyGated;
    absolutelyGated.reserve(impl.blockMeanSquares.size());
    std::vector<f64> absoluteLevels;
    absoluteLevels.reserve(impl.blockMeanSquares.size());
    f64 ungatedTotal = 0.0;
    u64 ungatedCount = 0;
    for (const f64 meanSquare : impl.blockMeanSquares) {
        if (!(meanSquare > 0.0)) {
            continue;
        }
        ungatedTotal += meanSquare;
        ++ungatedCount;
        const f64 level = blockLoudness(meanSquare);
        if (level > kAbsoluteGateLufs) {
            absolutelyGated.push_back(meanSquare);
            absoluteLevels.push_back(level);
        }
    }
    if (absolutelyGated.empty()) {
        // Every block fell below -70 LUFS. BS.1770 has no answer here: the gated set
        // J_g is empty, so there is no integrated loudness to report. Digital silence
        // is the common way to get here, which is why EmptySignal covers both this
        // and "nothing was pushed".
        return asUnexpected(LoudnessError::EmptySignal);
    }

    // BS.1770 eq. (6): the relative gate, 10 LU below the absolutely-gated mean.
    f64 absoluteTotal = 0.0;
    for (const f64 meanSquare : absolutelyGated) {
        absoluteTotal += meanSquare;
    }
    const f64 relativeGateLufs =
            blockLoudness(absoluteTotal / static_cast<f64>(absolutelyGated.size())) -
            kRelativeGateOffsetLu;

    // BS.1770 eq. (7): J_g = {j : l_j > G_r and l_j > G_a}. The absolute gate has
    // already been applied, so only the relative gate remains.
    f64 gatedTotal = 0.0;
    u64 gatedCount = 0;
    for (const f64 meanSquare : absolutelyGated) {
        if (blockLoudness(meanSquare) > relativeGateLufs) {
            gatedTotal += meanSquare;
            ++gatedCount;
        }
    }
    if (gatedCount == 0) {
        return asUnexpected(LoudnessError::EmptySignal);
    }

    // EBU Tech 3342 s5: LRA is the 95th minus the 10th percentile of the
    // *absolutely* gated block loudness values -- not the relatively gated set, and
    // not the integrated figure.
    std::sort(absoluteLevels.begin(), absoluteLevels.end());

    LoudnessAnalysis analysis;
    analysis.integratedLufs =
            blockLoudness(gatedTotal / static_cast<f64>(gatedCount));
    analysis.ungatedLufs =
            ungatedCount > 0 ? blockLoudness(ungatedTotal / static_cast<f64>(ungatedCount))
                             : 0.0;
    analysis.loudnessRangeLowLufs = percentile(absoluteLevels, 0.10);
    analysis.loudnessRangeHighLufs = percentile(absoluteLevels, 0.95);
    analysis.loudnessRangeLu =
            analysis.loudnessRangeHighLufs - analysis.loudnessRangeLowLufs;
    analysis.truePeakDbfs = peakToDb(truePeak);
    analysis.samplePeakDbfs = peakToDb(impl.samplePeak);
    analysis.sampleRate = impl.sampleRate;
    analysis.channels = impl.channels;
    analysis.framesTotal = impl.framesTotal;
    analysis.blocksConsidered = impl.blockMeanSquares.size();
    analysis.blocksGatedIn = gatedCount;
    analysis.preFilter = impl.preFilter;
    analysis.rlbHighPass = impl.rlbHighPass;
    return analysis;
}

// ─────────────────────────────────────────────────────────────────────────────
// Gain planning
// ─────────────────────────────────────────────────────────────────────────────

std::expected<GainPlan, LoudnessError> planGain(const LoudnessAnalysis& analysis,
                                                f64 targetLufs, f64 ceilingDbtp) {
    if (!std::isfinite(targetLufs) || !std::isfinite(ceilingDbtp)) {
        return asUnexpected(LoudnessError::GainOutOfRange);
    }
    if (!std::isfinite(analysis.integratedLufs) || !std::isfinite(analysis.truePeakDbfs)) {
        // Digital silence has no loudness and no peak, so there is nothing to gain
        // towards. Refusing is the only honest answer: the alternative is +inf dB.
        return asUnexpected(LoudnessError::GainOutOfRange);
    }

    GainPlan plan;
    plan.targetLufs = targetLufs;
    plan.ceilingDbtp = ceilingDbtp;
    plan.requestedGainDb = targetLufs - analysis.integratedLufs;
    plan.alreadyOverCeiling = analysis.truePeakDbfs > ceilingDbtp;

    // The ceiling is the real constraint, and the reason no limiter exists here: the
    // largest gain that keeps the reconstructed peak on the ceiling is exactly
    // ceiling - currentTruePeak. Anything above it clips, so it is not offered.
    const f64 gainAllowedByCeiling = ceilingDbtp - analysis.truePeakDbfs;
    plan.appliedGainDb = std::min(plan.requestedGainDb, gainAllowedByCeiling);
    plan.limitedByCeiling = plan.requestedGainDb > gainAllowedByCeiling;

    if (plan.appliedGainDb > kMaxGainDb) {
        plan.appliedGainDb = kMaxGainDb;
        plan.gainClampedToMaximum = true;
    } else if (plan.appliedGainDb < -kMaxGainDb) {
        plan.appliedGainDb = -kMaxGainDb;
        plan.gainClampedToMaximum = true;
    }

    plan.resultingLufs = analysis.integratedLufs + plan.appliedGainDb;
    plan.resultingTruePeakDbtp = analysis.truePeakDbfs + plan.appliedGainDb;
    return plan;
}

std::expected<GainPlan, LoudnessError> planGain(const LoudnessAnalysis& analysis,
                                                LoudnessTarget target, f64 ceilingDbtp) {
    const f64 targetLufs = loudnessTargetLufs(target);
    if (target == LoudnessTarget::Custom) {
        // `Custom` has no number of its own; a caller who wants one uses the f64
        // overload. Returning 0 LUFS here would silently aim at digital full scale.
        return asUnexpected(LoudnessError::GainOutOfRange);
    }
    return planGain(analysis, targetLufs, ceilingDbtp);
}

// ─────────────────────────────────────────────────────────────────────────────
// Gain application
// ─────────────────────────────────────────────────────────────────────────────

std::expected<f32, LoudnessError> linearGainFromDb(f64 decibels) noexcept {
    if (!std::isfinite(decibels) || std::abs(decibels) > 200.0) {
        // 200 dB rather than kMaxGainDb, because this is the general converter and
        // its job is to refuse values f32 cannot represent. f32 overflows somewhere
        // near 192 dB, and a silent multiply by infinity is not a gain.
        return asUnexpected(LoudnessError::GainOutOfRange);
    }
    // Exactly 1.0f for exactly 0.0: pow(10.0, 0.0) is 1.0 and the narrowing is
    // exact. That is what lets applyGain() treat 0 dB as a structural no-op.
    return static_cast<f32>(std::pow(10.0, decibels / 20.0));
}

std::expected<void, LoudnessError> applyGain(std::span<f32> interleaved,
                                             f32 linearGain) noexcept {
    if (!std::isfinite(linearGain)) {
        return asUnexpected(LoudnessError::GainOutOfRange);
    }
    if (linearGain == 1.0f) {
        // Deliberately short-circuited rather than relying on the IEEE guarantee.
        // Multiplying by exactly 1.0f does preserve every bit, but "does not touch
        // the buffer" is a property of this function that a reader can check, whereas
        // the multiplication's behaviour is something they have to know.
        return {};
    }
    for (f32& sample : interleaved) {
        sample *= linearGain;
    }
    return {};
}

std::expected<void, LoudnessError> applyGainDb(std::span<f32> interleaved,
                                               f64 decibels) noexcept {
    const auto gain = linearGainFromDb(decibels);
    if (!gain) {
        return asUnexpected(gain.error());
    }
    return applyGain(interleaved, *gain);
}

// ─────────────────────────────────────────────────────────────────────────────
// True-peak filter, exposed
// ─────────────────────────────────────────────────────────────────────────────

std::span<const f64> truePeakPhase(u32 phase) noexcept {
    if (phase >= kPhases) {
        return {};
    }
    return {kTruePeakTable[phase].data(), kTruePeakTable[phase].size()};
}

f64 truePeakPassbandWorstDeviationDb() noexcept {
    // Reassemble the 4x prototype from the branches, because the branches are what
    // the analyser runs and reassembling them is what the design describes.
    std::array<f64, kTaps * kPhases> prototype{};
    for (u32 phase = 0; phase < kPhases; ++phase) {
        for (usize tap = 0; tap < kTaps; ++tap) {
            prototype[phase + tap * kPhases] = kTruePeakTable[phase][tap];
        }
    }

    f64 worst = 0.0;
    const f64 radianScale =
            2.0 * std::numbers::pi_v<f64> / (kTruePeakProbeRate * static_cast<f64>(kPhases));
    for (int hertz = 100; hertz <= 20000; hertz += 10) {
        const f64 w = radianScale * static_cast<f64>(hertz);
        f64 real = 0.0;
        f64 imaginary = 0.0;
        for (usize n = 0; n < prototype.size(); ++n) {
            const f64 phase = w * static_cast<f64>(n);
            real += prototype[n] * std::cos(phase);
            imaginary -= prototype[n] * std::sin(phase);
        }
        const f64 magnitudeDb =
                20.0 * std::log10(std::hypot(real, imaginary) / static_cast<f64>(kPhases));
        worst = std::max(worst, std::abs(magnitudeDb));
    }
    return worst;
}

} // namespace vc::loudness
