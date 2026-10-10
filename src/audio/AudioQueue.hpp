// Version: 1.1.0
// Last Edited: 2026-08-25 12:00:00
// Description: Lock-free SPSC audio queues using moodycamel::ReaderWriterQueue
//              Two-queue pattern: viz (visualizer) + rec (recorder)
//              Producers hand over an AudioChunk view; queues store AudioFrame values.
//              The third queue used to be ana (analyzer); its only consumer was the
//              engine's analyzer worker, which computed a spectrum nothing read, so
//              the queue only ever filled and dropped.

#pragma once

#include <atomic>
#include <cstddef>
#include <cstring>
#include <readerwriterqueue.h>
#include "AudioChunk.hpp"
#include "util/Types.hpp"

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
    u32 sampleCount;                        // Number of valid samples (may be < 8 at boundaries)
    u32 channels;                           // Always 2 (stereo) after conversion
    u32 sampleRate;                         // Sample rate (typically 48000)

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
        : vizQueue_(capacity), recQueue_(capacity), vizDropCount_(0), recDropCount_(0),
          totalPushed_(0) {}

    // Non-copyable, non-movable (atomic members)
    AudioQueue(const AudioQueue&) = delete;
    AudioQueue& operator=(const AudioQueue&) = delete;
    AudioQueue(AudioQueue&&) = delete;
    AudioQueue& operator=(AudioQueue&&) = delete;

    // ========================================================================
    // Producer API (called from AudioEngine audio callback)
    // ========================================================================

    /**
     * Push interleaved PCM to all three consumer queues (viz/rec/ana).
     * @param chunk Non-owning view of interleaved samples; must stay valid
     *              until this call returns.
     * @return true if pushed successfully, false if dropped or empty input
     */
    bool pushAll(const AudioChunk& chunk) {
        // Publish the rate the producer observed the sink running at, so the
        // visualizer's per-tick batch sizer stops assuming 48 kHz. Relaxed:
        // an off-audio-thread reader gets a value at worst one push stale.
        if (chunk.sampleRate > 0) {
            lastPushedSampleRate_.store(chunk.sampleRate, std::memory_order_relaxed);
        }
        const bool a = pushInternal(vizQueue_, vizDropCount_, chunk);
        const bool b = pushInternal(recQueue_, recDropCount_, chunk);
        return a && b;
    }

    /// Raw-pointer overload retained for callers that have not adopted AudioChunk.
    bool pushAll(const float* data, u32 frames, u32 channels, u32 sampleRate) {
        if (!data || frames == 0 || channels == 0) return false;
        return pushAll(AudioChunk{
                .samples = {data, data + static_cast<usize>(frames) * channels},
                .channels = channels,
                .sampleRate = sampleRate,
        });
    }

    // ========================================================================
    // Consumer API (called from VisualizerRenderer / VideoRecorderThread)
    // ========================================================================

    /**
     * Pop a frame from visualizer queue.
     * @param frame Output frame
     * @return true if frame was popped, false if queue empty
     */
    bool popViz(AudioFrame& frame) { return vizQueue_.try_dequeue(frame); }

    /**
     * Pop a frame from recorder queue.
     * @param frame Output frame
     * @return true if frame was popped, false if queue empty
     */
    bool popRec(AudioFrame& frame) { return recQueue_.try_dequeue(frame); }

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
    u32 vizDepth() const { return static_cast<u32>(vizQueue_.size_approx()); }

    /** Get approximate depth of recorder queue */
    u32 recDepth() const { return static_cast<u32>(recQueue_.size_approx()); }

    /** Get total frames dropped from visualizer queue */
    u64 vizDropCount() const { return vizDropCount_.load(std::memory_order_relaxed); }

    /** Get total frames dropped from recorder queue */
    u64 recDropCount() const { return recDropCount_.load(std::memory_order_relaxed); }
    /** Get total frames pushed (for diagnostics) */
    u64 totalPushed() const { return totalPushed_.load(std::memory_order_relaxed); }

    /** The sample rate of the most recent push, as the producer observed the
     *  sink running. The 48 kHz default only survives until the first push;
     *  a 44.1 kHz sink must not be batch-sized as 48 kHz or projectM is
     *  misfed ~9% of each second's PCM and beat detection drifts. */
    u32 sampleRate() const { return lastPushedSampleRate_.load(std::memory_order_relaxed); }
    /** Reset all counters */
    void resetCounters() {
        vizDropCount_.store(0, std::memory_order_relaxed);
        recDropCount_.store(0, std::memory_order_relaxed);
        totalPushed_.store(0, std::memory_order_relaxed);
    }

    /** Clear both queues */
    void clear() {
        AudioFrame frame;
        while (vizQueue_.try_dequeue(frame)) {
        }
        while (recQueue_.try_dequeue(frame)) {
        }
    }

private:
    using Queue = moodycamel::ReaderWriterQueue<AudioFrame>;

    Queue vizQueue_;
    Queue recQueue_;

    std::atomic<u64> vizDropCount_;
    std::atomic<u64> recDropCount_;
    std::atomic<u64> totalPushed_;
    std::atomic<u32> lastPushedSampleRate_{48'000};

    bool pushInternal(Queue& queue, std::atomic<u64>& dropCount, const AudioChunk& chunk) {
        const float* data = chunk.samples.data();
        const u32 frames = chunk.frameCount();
        const u32 channels = chunk.channels;
        if (!data || frames == 0 || channels == 0) return false;

        totalPushed_.fetch_add(frames, std::memory_order_relaxed);

        // Convert to stereo frames and push in chunks
        u32 processed = 0;
        while (processed < frames) {
            AudioFrame frame;
            frame.sampleRate = chunk.sampleRate;
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
                // The whole remainder is lost, not just the chunk that was
                // refused: the loop returns here, so `remaining` samples never
                // reach this queue and were never offered to it either.
                // Counting only `chunkSize` (one AUDIO_FRAME_SAMPLES, i.e. 8)
                // made a 512-frame push into a capacity-1 queue report 8 drops
                // instead of 504, while totalPushed_ still counted all 512 --
                // so "pushed minus dropped" claimed 504 samples had reached the
                // visualizer and the recorder when 8 had. dropCount has to mean
                // "frames this queue refused" to be comparable with
                // totalPushed_ at all.
                dropCount.fetch_add(remaining, std::memory_order_relaxed);
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
