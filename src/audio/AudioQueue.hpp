// Version: 1.0.0
// Last Edited: 2026-03-29 12:00:00
// Description: Lock-free SPSC audio queues using moodycamel::ReaderWriterQueue
//              Two-queue pattern: viz path (visualizer) + rec path (recorder)

#pragma once

#include "util/Types.hpp"
#include <atomic>
#include <cstddef>
#include <readerwriterqueue.h>
#include <cstring>

namespace vc {

// Frame size optimized for cache lines (64 bytes)
// 8 samples * 2 channels * 4 bytes = 64 bytes
inline constexpr u32 AUDIO_FRAME_SAMPLES = 8;

// Default capacity: 3 seconds at 48kHz stereo
// 48000 * 2 * 3 = 288000 samples / 8 = 36000 frames
inline constexpr u32 DEFAULT_QUEUE_CAPACITY = 36000;

// Cache-line aligned audio frame for optimal SPSC performance
struct alignas(64) AudioFrame {
    float samples[AUDIO_FRAME_SAMPLES * 2]; // Stereo interleaved
    u32 sampleCount;  // Number of valid samples (may be < 8 at boundaries)
    u32 channels;     // Always 2 (stereo) after conversion
    u32 sampleRate;   // Sample rate (typically 48000)

    AudioFrame() : sampleCount(0), channels(2), sampleRate(48000) {
        std::memset(samples, 0, sizeof(samples));
    }
};

/**
 * Lock-free audio queue manager with two-queue pattern.
 * 
 * AudioEngine (producer) pushes to both queues.
 * VisualizerRenderer (consumer) pops from vizQueue.
 * VideoRecorderThread (consumer) pops from recQueue.
 * 
 * Thread safety: SPSC semantics - single producer, single consumer per queue.
 * No mutexes in the audio callback path.
 */
class AudioQueue {
public:
    explicit AudioQueue(u32 capacity = DEFAULT_QUEUE_CAPACITY)
        : vizQueue_(capacity)
        , recQueue_(capacity)
        , vizDropCount_(0)
        , recDropCount_(0)
        , totalPushed_(0)
    {}

    // Non-copyable, non-movable (atomic members)
    AudioQueue(const AudioQueue&) = delete;
    AudioQueue& operator=(const AudioQueue&) = delete;
    AudioQueue(AudioQueue&&) = delete;
    AudioQueue& operator=(AudioQueue&&) = delete;

    // ========================================================================
    // Producer API (called from AudioEngine audio callback)
    // ========================================================================

    /**
     * Push audio data to visualizer queue.
     * @param data Interleaved stereo samples
     * @param frames Number of frames (samples per channel)
     * @param channels Number of channels (will be converted to stereo)
     * @param sampleRate Sample rate
     * @return true if pushed successfully, false if dropped
     */
    bool pushViz(const float* data, u32 frames, u32 channels, u32 sampleRate) {
        return pushInternal(vizQueue_, vizDropCount_, data, frames, channels, sampleRate);
    }

    /**
     * Push audio data to recorder queue.
     * @param data Interleaved stereo samples
     * @param frames Number of frames (samples per channel)
     * @param channels Number of channels (will be converted to stereo)
     * @param sampleRate Sample rate
     * @return true if pushed successfully, false if dropped
     */
    bool pushRec(const float* data, u32 frames, u32 channels, u32 sampleRate) {
        return pushInternal(recQueue_, recDropCount_, data, frames, channels, sampleRate);
    }

    /**
     * Push to both queues atomically (convenience method).
     * Used when both visualizer and recorder need the same data.
     */
    void pushBoth(const float* data, u32 frames, u32 channels, u32 sampleRate) {
        pushViz(data, frames, channels, sampleRate);
        pushRec(data, frames, channels, sampleRate);
    }

    // ========================================================================
    // Consumer API (called from VisualizerRenderer / VideoRecorderThread)
    // ========================================================================

    /**
     * Pop a frame from visualizer queue.
     * @param frame Output frame
     * @return true if frame was popped, false if queue empty
     */
    bool popViz(AudioFrame& frame) {
        return vizQueue_.try_dequeue(frame);
    }

    /**
     * Pop a frame from recorder queue.
     * @param frame Output frame
     * @return true if frame was popped, false if queue empty
     */
    bool popRec(AudioFrame& frame) {
        return recQueue_.try_dequeue(frame);
    }

    /**
     * Pop multiple frames from visualizer queue into a flat buffer.
     * Useful for batch processing in visualizer.
     * @param buffer Output buffer for interleaved samples
     * @param maxFrames Maximum frames to pop
     * @return Number of frames actually popped
     */
    u32 popVizBatch(float* buffer, u32 maxFrames) {
        return popBatchInternal(vizQueue_, buffer, maxFrames);
    }

    /**
     * Pop multiple frames from recorder queue into a flat buffer.
     * @param buffer Output buffer for interleaved samples
     * @param maxFrames Maximum frames to pop
     * @return Number of frames actually popped
     */
    u32 popRecBatch(float* buffer, u32 maxFrames) {
        return popBatchInternal(recQueue_, buffer, maxFrames);
    }

    // ========================================================================
    // Metrics (thread-safe via atomics)
    // ========================================================================

    /** Get approximate depth of visualizer queue */
    u32 vizDepth() const {
        return static_cast<u32>(vizQueue_.size_approx());
    }

    /** Get approximate depth of recorder queue */
    u32 recDepth() const {
        return static_cast<u32>(recQueue_.size_approx());
    }

    /** Get total frames dropped from visualizer queue */
    u64 vizDropCount() const {
        return vizDropCount_.load(std::memory_order_relaxed);
    }

    /** Get total frames dropped from recorder queue */
    u64 recDropCount() const {
        return recDropCount_.load(std::memory_order_relaxed);
    }

    /** Get total frames pushed (for diagnostics) */
    u64 totalPushed() const {
        return totalPushed_.load(std::memory_order_relaxed);
    }

    /** Reset all counters */
    void resetCounters() {
        vizDropCount_.store(0, std::memory_order_relaxed);
        recDropCount_.store(0, std::memory_order_relaxed);
        totalPushed_.store(0, std::memory_order_relaxed);
    }

    /** Clear both queues */
    void clear() {
        AudioFrame frame;
        while (vizQueue_.try_dequeue(frame)) {}
        while (recQueue_.try_dequeue(frame)) {}
    }

private:
    using Queue = moodycamel::ReaderWriterQueue<AudioFrame>;

    Queue vizQueue_;
    Queue recQueue_;

    std::atomic<u64> vizDropCount_;
    std::atomic<u64> recDropCount_;
    std::atomic<u64> totalPushed_;

    bool pushInternal(Queue& queue, std::atomic<u64>& dropCount,
                      const float* data, u32 frames, u32 channels, u32 sampleRate) {
        if (!data || frames == 0) return false;

        totalPushed_.fetch_add(frames, std::memory_order_relaxed);

        // Convert to stereo frames and push in chunks
        u32 processed = 0;
        while (processed < frames) {
            AudioFrame frame;
            frame.sampleRate = sampleRate;
            frame.channels = 2;

            u32 remaining = frames - processed;
            u32 chunkSize = std::min(remaining, AUDIO_FRAME_SAMPLES);
            frame.sampleCount = chunkSize;

            // Convert to stereo interleaved
            for (u32 i = 0; i < chunkSize; ++i) {
                u32 srcIdx = (processed + i) * channels;
                if (channels >= 2) {
                    frame.samples[i * 2] = data[srcIdx];
                    frame.samples[i * 2 + 1] = data[srcIdx + 1];
                } else if (channels == 1) {
                    // Mono to stereo
                    frame.samples[i * 2] = data[srcIdx];
                    frame.samples[i * 2 + 1] = data[srcIdx];
                }
            }

            if (!queue.try_enqueue(frame)) {
                dropCount.fetch_add(chunkSize, std::memory_order_relaxed);
                return false;
            }

            processed += chunkSize;
        }

        return true;
    }

    u32 popBatchInternal(Queue& queue, float* buffer, u32 maxFrames) {
        if (!buffer || maxFrames == 0) return 0;

        u32 popped = 0;
        AudioFrame frame;

        while (popped < maxFrames && queue.try_dequeue(frame)) {
            u32 toCopy = std::min(frame.sampleCount, maxFrames - popped);
            std::memcpy(buffer + popped * 2, frame.samples, toCopy * 2 * sizeof(float));
            popped += toCopy;
        }

        return popped;
    }
};

} // namespace vc
