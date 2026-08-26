#pragma once
// FrameGrabber.hpp - OpenGL frame capture
// Stealing pixels from the GPU like a pro

#include <QOpenGLFunctions_3_3_Core>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>
#include "util/Types.hpp"
#include "visualizer/RenderTarget.hpp"

namespace vc {

struct GrabbedFrame {
    std::vector<u8> data;
    u32 width{0};
    u32 height{0};
    i64 timestamp{0}; // microseconds
    u32 frameNumber{0};
};

class FrameGrabber : protected QOpenGLFunctions_3_3_Core {
public:
    FrameGrabber();
    ~FrameGrabber();

    // Configuration
    void setSize(u32 width, u32 height);
    void setFlipVertical(bool flip) {
        flipVertical_ = flip;
    }

    // Grab frame from render target
    void grab(RenderTarget& target, i64 timestamp);

    // Grab from current framebuffer
    void grabScreen(u32 width, u32 height, i64 timestamp);

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
    void flipImage(std::vector<u8>& data, u32 width, u32 height);
    void flipImageGPU(std::vector<u8>& data, u32 width, u32 height);

    u32 width_{1920};
    u32 height_{1080};
    bool flipVertical_{true}; // OpenGL is bottom-up
    bool useGPUFlip_{true};   // Use GPU acceleration for flipping

    std::queue<GrabbedFrame> frameQueue_;
    mutable std::mutex queueMutex_;
    std::condition_variable queueCond_;

    std::atomic<bool> running_{false};
    std::atomic<u32> frameNumber_{0};
    std::atomic<u32> droppedFrames_{0};

    static constexpr usize MAX_QUEUE_SIZE = 30; // ~0.5 sec at 60fps
};

} // namespace vc