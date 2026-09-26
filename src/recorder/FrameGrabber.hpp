#pragma once
// FrameGrabber.hpp - recorded frame queue
//
// The pixels themselves are produced by VisualizerRenderer, which reads them
// out of framebuffer 0 where projectM draws. This class owns the ownership
// hand-off to the encoder worker and the row-order convention that the encoder
// expects, nothing else.

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <vector>
#include "util/Types.hpp"

namespace vc {

struct GrabbedFrame {
    std::vector<u8> data;
    u32 width{0};
    u32 height{0};
    i64 timestamp{0}; // microseconds
    u32 frameNumber{0};
};

class FrameGrabber {
public:
    FrameGrabber();
    ~FrameGrabber();

    // glReadPixels returns rows from the bottom of the framebuffer upwards.
    // Every consumer of a frame here (the RGBA upload into libavcodec, QImage)
    // wants top-down rows, so this flip is not optional: without it the encoded
    // file plays upside down.
    static void flipImage(std::vector<u8>& data, u32 width, u32 height);

    // Get next frame (blocking)
    bool getNextFrame(GrabbedFrame& frame, u32 timeoutMs = 100);

    // Check if frames available
    bool hasFrames() const;
    usize queueSize() const;

    // Statistics
    u32 droppedFrames() const {
        return droppedFrames_;
    }
    void resetStats();

    // Control
    void start();
    void stop();
    void clear();

    // Push frame directly
    void pushFrame(GrabbedFrame&& frame) {
        if (!running_)
            return;
        {
            std::lock_guard lock(queueMutex_);
            if (frameQueue_.size() >= MAX_QUEUE_SIZE) {
                frameQueue_.pop();
                ++droppedFrames_;
            }
            frameQueue_.push(std::move(frame));
        }
        queueCond_.notify_one();
    }

private:
    std::queue<GrabbedFrame> frameQueue_;
    mutable std::mutex queueMutex_;
    std::condition_variable queueCond_;

    std::atomic<bool> running_{false};
    std::atomic<u32> frameNumber_{0};
    std::atomic<u32> droppedFrames_{0};

    static constexpr usize MAX_QUEUE_SIZE = 30; // ~0.5 sec at 60fps
};

} // namespace vc