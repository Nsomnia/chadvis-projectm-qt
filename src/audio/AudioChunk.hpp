#pragma once
/**
 * @file AudioChunk.hpp
 * @file Purpose: lightweight non-owning view over interleaved PCM audio
 *                threaded through AudioQueue producers/consumers.
 *                Does NOT own sample storage and does NOT enter lock-free queue
 *                internals (queues still store AudioFrame values).
 *
 * @version 1.0.0 - 2026-08-25
 */

#include <span>
#include "util/Types.hpp"

namespace vc {

/**
 * @brief Non-owning view of interleaved float PCM samples.
 *
 * Passed by value; samples must outlive the chunk (callers pass engine-owned
 * buffers such as AudioEngine::scratchBuffer_).
 */
struct AudioChunk {
    std::span<const f32> samples;
    u32 channels{2};
    u32 sampleRate{48000};

    [[nodiscard]] u32 frameCount() const {
        return channels > 0 ? static_cast<u32>(samples.size()) / channels : 0;
    }
};

} // namespace vc
