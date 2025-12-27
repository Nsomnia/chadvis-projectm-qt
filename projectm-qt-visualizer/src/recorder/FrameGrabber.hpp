#pragma once
// FrameGrabber.hpp - OpenGL frame capture
// Stealing pixels from the GPU like a pro

#include "util/Types.hpp"
#include "visualizer/RenderTarget.hpp"
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>

namespace vc {

struct GrabbedFrame {
    std::vector<u8> data;
    u32 width{0};
    u32 height{0};
    i64 timestamp{0};  // microseconds
    u32 frameNumber{0};
};

class FrameGrabber {
public:
    FrameGrabber();
    ~FrameGrabber();
    
    // Configuration
    void setSize(u32 width, u32 height);
    void setFlipVertical(bool flip) { flipVertical_ = flip; }
    
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
    u32 droppedFrames() const { return droppedFrames_; }
    void resetStats();
    
    // Control
    void start();
    void stop();
    void clear();
    
private:
    void flipImage(std::vector<u8>& data, u32 width, u32 height);
    
    u32 width_{1920};
    u32 height_{1080};
    bool flipVertical_{true};  // OpenGL is bottom-up
    
    std::queue<GrabbedFrame> frameQueue_;
    mutable std::mutex queueMutex_;
    std::condition_variable queueCond_;
    
    std::atomic<bool> running_{false};
    std::atomic<u32> frameNumber_{0};
    std::atomic<u32> droppedFrames_{0};
    
    static constexpr usize MAX_QUEUE_SIZE = 30;  // ~0.5 sec at 60fps
};

// PBO-based async frame grabber for better performance
class AsyncFrameGrabber {
public:
    AsyncFrameGrabber();
    ~AsyncFrameGrabber();
    
    // Initialize with size and PBO count
    Result<void> init(u32 width, u32 height, u32 pboCount = 3);
    void shutdown();
    
    // Start async read (non-blocking)
    void startRead(RenderTarget& target, i64 timestamp);
    
    // Get completed frame (non-blocking)
    bool getCompletedFrame(GrabbedFrame& frame);
    
    // Resize (recreates PBOs)
    Result<void> resize(u32 width, u32 height);
    
private:
    struct PBOSlot {
        GLuint pbo{0};
        bool inUse{false};
        bool ready{false};
        i64 timestamp{0};
        u32 frameNumber{0};
    };
    
    std::vector<PBOSlot> pboSlots_;
    u32 currentSlot_{0};
    u32 width_{0};
    u32 height_{0};
    u32 frameNumber_{0};
    bool initialized_{false};
};

} // namespace vc