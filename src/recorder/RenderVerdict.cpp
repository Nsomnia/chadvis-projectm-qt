#include "recorder/RenderVerdict.hpp"

namespace vc {

const char* toString(RenderThreadVerdict verdict) noexcept {
    switch (verdict) {
        case RenderThreadVerdict::Supported:
            return "Supported";
        case RenderThreadVerdict::NeedsMainThread:
            return "NeedsMainThread";
        case RenderThreadVerdict::Unsupported:
            return "Unsupported";
    }
    return "Unsupported";
}

int offscreenVerdictMaxConcurrency(RenderThreadVerdict verdict) noexcept {
    switch (verdict) {
        case RenderThreadVerdict::Supported:
            // No ceiling follows from this verdict, and -1 says so rather than
            // substituting another invented constant for the one being replaced.
            return -1;
        case RenderThreadVerdict::NeedsMainThread:
            return 1;
        case RenderThreadVerdict::Unsupported:
            return 0;
    }
    return 0;
}

} // namespace vc
