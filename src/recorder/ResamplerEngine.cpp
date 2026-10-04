/**
 * @file ResamplerEngine.cpp
 * @file Purpose: the soxr engine choice, and the honest reason it is refused.
 *
 * See ResamplerEngine.hpp for the measurements. The short version: the entry
 * point is `av_opt_set(ctx, "engine", "soxr", 0)` rather than the
 * `swr_set_engine` that does not exist in this project's libswresample, and
 * that call failing with `AVERROR_OPTION_NOT_FOUND` is a *normal* outcome on a
 * correctly-built FFmpeg.
 */

#include "recorder/ResamplerEngine.hpp"

extern "C" {
#include <libavutil/error.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

#include <array>
#include <mutex>

#include "core/Logger.hpp"

namespace vc {

namespace {

// Two log lines per process, at most. A render worker builds a swr context per
// file and a recorder builds one per recording, so an unconditional log would
// turn a routine fact into noise -- and noise is how a real diagnostic gets
// scrolled past. `applyEngine` is documented as logging the decision "once",
// and these two flags are how that is kept true.
std::once_flag g_loggedSoxrSelected;
std::once_flag g_loggedFallback;

// `CHADVIS_USE_SOXR_ENGINE` defaults to ON when undefined. That default is
// deliberate and it is the opposite of what the configure-time probe finds: the
// probe only reports whether libsoxr is installed on the *machine*, and having
// it installed says nothing about whether this libswresample was configured
// with --enable-libsoxr (measured: the option is absent from the option table
// entirely on a machine with no libsoxr, and the two answers happen to agree
// here by coincidence, not by construction). So the compile flag means "the
// developer asked not to try", never "we are certain it would work" -- and the
// runtime probe stays the only authority.
#if defined(CHADVIS_USE_SOXR_ENGINE) && !CHADVIS_USE_SOXR_ENGINE
constexpr bool kEngineAttemptAllowed = false;
#else
constexpr bool kEngineAttemptAllowed = true;
#endif

} // namespace

std::string_view resamplerEngineName(ResamplerEngine engine) noexcept {
    switch (engine) {
    case ResamplerEngine::Default:
        return "swr (libswresample)";
    case ResamplerEngine::Soxr:
        return "soxr";
    }
    return "unknown";
}

std::string_view soxrFallbackReasonName(SoxrFallbackReason reason) noexcept {
    switch (reason) {
    case SoxrFallbackReason::None:
        return "soxr selected";
    case SoxrFallbackReason::NotAttempted:
        return "soxr not attempted (CHADVIS_USE_SOXR_ENGINE=0)";
    case SoxrFallbackReason::OptionMissing:
        return "this libswresample was built without --enable-libsoxr";
    case SoxrFallbackReason::Refused:
        return "libswresample refused the soxr engine";
    }
    return "unknown";
}

std::string describeAvError(int avCode) {
    if (avCode == 0) return {};
    std::array<char, 128> buf{};
    // av_strerror writes at most buflen-1 bytes and NUL-terminates. A failure to
    // build the message is itself not worth propagating: the numeric code is
    // already in the choice and `soxrFallbackReasonName` carries the diagnosis.
    if (av_strerror(avCode, buf.data(), static_cast<std::size_t>(buf.size())) < 0)
        return "AVERROR(" + std::to_string(avCode) + ")";
    return {buf.data()};
}

EngineChoice chooseEngine(std::optional<int> setEngineResult) noexcept {
    EngineChoice choice{};
    if (!setEngineResult.has_value()) {
        choice.fallback = SoxrFallbackReason::NotAttempted;
        return choice;
    }
    if (*setEngineResult == 0) {
        choice.engine = ResamplerEngine::Soxr;
        choice.fallback = SoxrFallbackReason::None;
        return choice;
    }

    choice.detail = describeAvError(*setEngineResult);
    // Measured: on a correctly-configured FFmpeg without --enable-libsoxr this
    // is AVERROR_OPTION_NOT_FOUND (-1414549496), av_strerror -> "Option not
    // found", because libswresample omits the `engine` option entirely rather
    // than registering it and refusing at init. Distinguishing it from a generic
    // failure matters: only this one is fixed by rebuilding FFmpeg, and a
    // message that said "refused" would send someone to reconfigure a build
    // that was already correct.
    choice.fallback = (*setEngineResult == AVERROR_OPTION_NOT_FOUND)
                              ? SoxrFallbackReason::OptionMissing
                              : SoxrFallbackReason::Refused;
    return choice;
}

std::string EngineChoice::describe() const {
    if (usingSoxr())
        return "soxr";
    std::string out = "libswresample's own resampler -- ";
    out += soxrFallbackReasonName(fallback);
    if (!detail.empty()) {
        out += " (";
        out += detail;
        out += ")";
    }
    return out;
}

EngineChoice applyEngine(SwrContext* ctx, std::string_view contextName) {
    if (!ctx || !kEngineAttemptAllowed)
        return chooseEngine(std::nullopt);

    const EngineChoice choice = chooseEngine(av_opt_set(ctx, "engine", "soxr", 0));

    // INFO, not WARN, for the fallback: it is the expected outcome on any FFmpeg
    // configured without soxr, it changes no output, and a WARN that fires on
    // every startup trains people to ignore warnings. The message still names
    // the reason so the answer to "why is the quality lower than I expected"
    // is in the log rather than in a rebuild of this project.
    if (choice.usingSoxr()) {
        std::call_once(g_loggedSoxrSelected, [&] {
            LOG_INFO("{} resampler: {}", contextName, choice.describe());
        });
    } else {
        std::call_once(g_loggedFallback, [&] {
            LOG_INFO("{} resampler: {}", contextName, choice.describe());
        });
    }
    return choice;
}

} // namespace vc
