#pragma once
/**
 * @file ResamplerEngine.hpp
 * @file Purpose: choose libswresample's resampling engine, observably.
 *
 * @section Why the default is worth replacing
 * libswresample ships three engines and picks one for you. Left alone, every
 * `swr` context in this tree uses `SWR_ENGINE_SWR` -- libswresample's own
 * resampler. That is a defensible default: it is present wherever
 * libswresample is present, it is bit-exact and deterministic, and it has no
 * extra dependency. It is also the lowest-fidelity of the three, and for *this*
 * product that is a quality claim rather than a footnote: the whole point is
 * turning an arbitrary source file into a finished video, so an arbitrary
 * source rate has to reach whatever the encoder asked for, and `soxr` is
 * measurably better in both passband and imaging. This is also the quality
 * feature that survives if every Suno surface disappears.
 *
 * @section `swr_set_engine` does not exist -- and there is no way to find out
 * without asking the library
 * The name to reach for is `swr_set_engine`, and it is **not callable on this
 * project's FFmpeg** (measured against FFmpeg 9.0.2 / libswresample 7.1.102,
 * the versions `pkg-config --modversion libswresample` reports):
 *
 *  - `swresample.h` declares `enum SwrEngine { SWR_ENGINE_SWR, SWR_ENGINE_SOXR,
 *    SWR_ENGINE_NB }` but **no `swr_set_engine` function**.
 *  - `nm -gU libswresample.7.1.102.dylib` lists 20 `swr_` symbols; none of them
 *    is `swr_set_engine`.
 *
 * Engine selection goes through the AVOptions interface instead:
 * `av_opt_set(ctx, "engine", "soxr", 0)`. So the "engine" AVOption is the
 * contract, and this file is deliberately written against *that* rather than
 * against a convenience wrapper whose availability varies by FFmpeg release.
 *
 * @section Two independent gates, and the second one is the one that bites
 * Having `libsoxr` installed on the machine says **nothing** about whether the
 * `libswresample` this build links was compiled with `--enable-libsoxr`. FFmpeg
 * drops the `engine` option entirely when soxr support is off, and that cannot
 * be detected at configure time. Measured on this machine, which is the case
 * that matters:
 *
 *  - `pkg-config --exists soxr` exits 1; `brew --prefix soxr` reports no such
 *    formula; `ffmpeg -buildconf` has no `--enable-libsoxr`.
 *  - `av_opt_find(swr, "engine", ...)` returns **NULL**, while the same call
 *    finds `filter_size`, `dither_method`, `phase_shift` and `resampler` -- so
 *    the mechanism works and `engine` specifically is absent, which is the
 *    proof that this is not a broken probe.
 *  - `av_opt_set(swr, "engine", "soxr", 0)` returns `AVERROR_OPTION_NOT_FOUND`
 *    (-1414549496, `av_strerror` -> "Option not found").
 *  - `swr_init` then **succeeds** (returns 0) and the context runs on
 *    `SWR_ENGINE_SWR`. Measured. So the degradation is free and non-fatal --
 *    but only for a caller that *checks* the return, which is the rule the rest
 *    of this tree already follows for every other libav return
 *    (`VideoRecorderFFmpeg::initAudioStream` checks both of the returns it used
 *    to discard, precisely because the next line dereferenced the context).
 *
 * Hence the split: `chooseEngine()` is pure and takes the already-known result
 * of the attempt, so **both** branches are assertable in a build that has no
 * soxr at all; `applyEngine()` is the thin impure seam that asks libswresample
 * and logs the outcome.
 *
 * Note what is *deliberately absent*: a `#if` on whether libsoxr was found at
 * configure time. Such a gate would be a lie in both directions -- the probe
 * cannot see this FFmpeg's configuration, and gating the runtime attempt out
 * would make the "soxr actually selected" case untestable on a machine that has
 * no soxr at all. `CHADVIS_USE_SOXR_ENGINE` therefore exists only as a manual
 * off-switch for A/B comparison, and means "do not try", never "it will work".
 *
 * @section Where this actually changes the audio
 * Not equally everywhere, and saying so is the point. `AudioFileDecoder`
 * genuinely rate-converts: an 8 kHz or 44.1 kHz source becomes the 48 kHz the
 * queue and the encoder want, and that is the conversion whose passband this
 * choice improves. The recorder's own `swr` context currently declares
 * `in_rate == out_rate == settings.audio.sampleRate`
 * (`VideoRecorderFFmpeg.cpp:904-912` passes the same value twice), so it does
 * format conversion and no rate conversion at all -- soxr buys it nothing
 * today. It is applied there anyway, for one line, so the moment that pin is
 * corrected the higher-quality engine is already the one in use.
 */

#include "util/Types.hpp"

#include <optional>
#include <string>
#include <string_view>

/// FFmpeg's `struct SwrContext`, forward-declared so this header stays free of
/// FFmpeg includes and both the pure and the impure half can be reasoned about
/// (and tested) without one.
///
/// At *global* scope, deliberately: declaring it inside `namespace vc` would
/// create a second, unrelated `vc::SwrContext`, and every call below would then
/// fail to convert -- a mistake that costs four errors per call site and is not
/// obvious from the first one. The `extern "C"` on FFmpeg's own declaration does
/// not change its linkage here, only its name, so an unqualified global forward
/// declaration is the match.
struct SwrContext;

namespace vc {

/// Which resampling engine a context will run on.
enum class ResamplerEngine : u8 {
    /// `SWR_ENGINE_SWR` -- libswresample's own resampler. Always available,
    /// bit-exact, deterministic, and the correct answer when soxr is not.
    Default,
    /// `SWR_ENGINE_SOXR`. Better passband and imaging; requires a libswresample
    /// built with `--enable-libsoxr`.
    Soxr,
};

/// Why the default engine was kept. Named rather than a bare string so a test
/// can assert the *class* of degradation, and so a log line can say something
/// actionable instead of "resampling quality: normal".
enum class SoxrFallbackReason : u8 {
    /// soxr was selected. Nothing to report.
    None,
    /// The attempt was never made: `CHADVIS_USE_SOXR_ENGINE=0`, or a null
    /// context. Says nothing about what the library would have said.
    NotAttempted,
    /// The attempt was made and libswresample does not expose an `engine`
    /// option: this FFmpeg was configured without `--enable-libsoxr`. Measured
    /// as `AVERROR_OPTION_NOT_FOUND` on this machine.
    OptionMissing,
    /// The option exists but `av_opt_set` refused for some other reason. Kept
    /// distinct from `OptionMissing` because it means the diagnosis is not
    /// "rebuild FFmpeg", and conflating the two would send someone to rebuild
    /// a FFmpeg that was configured correctly.
    Refused,
};

/// The decision, and the evidence behind it.
struct EngineChoice {
    ResamplerEngine engine{ResamplerEngine::Default};
    SoxrFallbackReason fallback{SoxrFallbackReason::None};
    /// `av_strerror` text for the failing `av_opt_set` return, or empty when
    /// no libav error is in play. Never the only thing a log says -- see
    /// `describe()`.
    std::string detail;

    [[nodiscard]] bool usingSoxr() const noexcept {
        return engine == ResamplerEngine::Soxr;
    }

    /// One line, always non-empty, naming either the engine in use or the
    /// reason the default was kept. This is the string a log emits; a caller
    /// that wants the machine-readable form reads the two enums instead.
    [[nodiscard]] std::string describe() const;
};

/// The pure decision. `setEngineResult` is what `av_opt_set(ctx, "engine",
/// "soxr", 0)` returned, or `std::nullopt` when the caller declined to attempt
/// it (`CHADVIS_USE_SOXR_ENGINE=0`, or a null context).
///
/// Pure over its argument and free of globals, so a test can assert every branch
/// -- including both fallback classes -- in a build that has no soxr at all.
/// Returns `Soxr`/`None` only for a genuine 0.
[[nodiscard]] EngineChoice chooseEngine(std::optional<int> setEngineResult) noexcept;

/// `av_strerror` text for `avCode`, or empty when `avCode` is 0.
[[nodiscard]] std::string describeAvError(int avCode);

/// The impure seam: ask `ctx` for the soxr engine, before `swr_init`, and log
/// the decision. Returns the choice so the caller can surface it.
///
/// Logs at most one line per outcome per process, so a render worker that builds
/// a context per file does not turn a routine fact into noise.
///
/// Safe to call on a null `ctx` (reports `NotAttempted`) so the caller needs no
/// guard of its own. Does not call `swr_init` -- the FFmpeg header requires the
/// context to be "allocated, not yet initialized" for options to be settable,
/// which is the same ordering constraint `swr_set_matrix` has and which
/// `AudioFileDecoder::Impl::buildResampler` already documents.
///
/// Deliberately NOT `[[nodiscard]]`, unlike `chooseEngine` beside it. Both
/// production call sites want the engine chosen and nothing else -- the whole
/// point of the fallback is that a caller can ignore the answer -- and marking
/// it nodiscard would have had to be defeated with `(void)` at each of them,
/// which reads like a suppression of a real warning. A caller that *does* want
/// to surface it (a test, or a diagnostics report) has the return.
EngineChoice applyEngine(SwrContext* ctx, std::string_view contextName);

/// Stable identifiers, for logs and test assertions.
[[nodiscard]] std::string_view resamplerEngineName(ResamplerEngine engine) noexcept;
[[nodiscard]] std::string_view soxrFallbackReasonName(SoxrFallbackReason reason) noexcept;

} // namespace vc
