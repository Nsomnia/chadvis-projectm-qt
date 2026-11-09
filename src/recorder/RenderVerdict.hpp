#pragma once
// RenderVerdict.hpp -- the batch renderer's threading verdict and its name.
//
// Minimal slice absorbed from OffscreenRenderSpike (T0400): the probe (.cpp,
// ~1000 lines of live GL measurement) moved to test-only, and only the
// vocabulary types production needs — RenderThreadVerdict + toString +
// offscreenVerdictMaxConcurrency — remain compiled into the app. The measured
// answer is carried here in prose, not re-measured in production code.

#include "util/Types.hpp"

#include <cstdint>

namespace vc {

/// What a render worker should do about background-thread rendering.
enum class RenderThreadVerdict {
    Supported,
    NeedsMainThread,
    Unsupported,
};

[[nodiscard]] const char* toString(RenderThreadVerdict verdict) noexcept;

/// Concurrency ceiling a verdict permits for RenderQueue::setMaxConcurrent.
/// -1: no ceiling (frame-cost decides). 1: marshal onto the GUI thread.
[[nodiscard]] int offscreenVerdictMaxConcurrency(RenderThreadVerdict verdict) noexcept;

} // namespace vc
