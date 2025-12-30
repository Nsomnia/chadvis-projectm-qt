#include "FrameGrabber.hpp"
#include "core/Logger.hpp"
#include "util/GLIncludes.hpp"
#include <algorithm>

namespace vc {

// ================== FrameGrabber ==================

FrameGrabber::FrameGrabber() = default;

FrameGrabber::~FrameGrabber() {
    stop();
}

void FrameGrabber::setSize(u32 width, u32 height) {
    width_ = width;
    height_ = height;
}

void FrameGrabber::grab(RenderTarget& target, i64 timestamp) {
    if (!running_) return;
    
    GrabbedFrame frame;
    frame.width = target.width();
    frame.height = target.height();
    frame.timestamp = timestamp;
    frame.frameNumber = frameNumber_++;
    frame.data.resize(frame.width * frame.height * 4);
    
    // Read pixels from render target
    target.readPixels(frame.data.data(), GL_RGBA, GL_UNSIGNED_BYTE);
    
    // Flip if needed
    if (flipVertical_) {
        flipImage(frame.data, frame.width, frame.height);
    }
    
    // Add to queue
    {
        std::lock_guard lock(queueMutex_);
        
        if (frameQueue_.size() >= MAX_QUEUE_SIZE) {
            // Drop oldest frame
            frameQueue_.pop();
            ++droppedFrames_;
        }
        
        frameQueue_.push(std::move(frame));
    }
    queueCond_.notify_one();
}

void FrameGrabber::grabScreen(u32 width, u32 height, i64 timestamp) {
    if (!running_) return;
    
    GrabbedFrame frame;
    frame.width = width;
    frame.height = height;
    frame.timestamp = timestamp;
    frame.frameNumber = frameNumber_++;
    frame.data.resize(width * height * 4);
    
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, frame.data.data());
    
    if (flipVertical_) {
        flipImage(frame.data, width, height);
    }
    
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

bool FrameGrabber::getNextFrame(GrabbedFrame& frame, u32 timeoutMs) {
    std::unique_lock lock(queueMutex_);
    
    if (!queueCond_.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                              [this] { return !frameQueue_.empty() || !running_; })) {
        return false;
    }
    
    if (frameQueue_.empty()) {
        return false;
    }
    
    frame = std::move(frameQueue_.front());
    frameQueue_.pop();
    return true;
}

bool FrameGrabber::hasFrames() const {
    std::lock_guard lock(queueMutex_);
    return !frameQueue_.empty();
}

usize FrameGrabber::queueSize() const {
    std::lock_guard lock(queueMutex_);
    return frameQueue_.size();
}

void FrameGrabber::resetStats() {
    droppedFrames_ = 0;
    frameNumber_ = 0;
}

void FrameGrabber::start() {
    running_ = true;
    resetStats();
}

void FrameGrabber::stop() {
    running_ = false;
    queueCond_.notify_all();
}

void FrameGrabber::clear() {
    std::lock_guard lock(queueMutex_);
    while (!frameQueue_.empty()) {
        frameQueue_.pop();
    }
}

void FrameGrabber::flipImage(std::vector<u8>& data, u32 width, u32 height) {
    u32 rowSize = width * 4;
    std::vector<u8> temp(rowSize);
    
    for (u32 y = 0; y < height / 2; ++y) {
        u8* top = data.data() + y * rowSize;
        u8* bottom = data.data() + (height - 1 - y) * rowSize;
        
        std::memcpy(temp.data(), top, rowSize);
        std::memcpy(top, bottom, rowSize);
        std::memcpy(bottom, temp.data(), rowSize);
    }
}

// ================== AsyncFrameGrabber ==================

AsyncFrameGrabber::AsyncFrameGrabber() = default;

AsyncFrameGrabber::~AsyncFrameGrabber() {
    shutdown();
}

Result<void> AsyncFrameGrabber::init(u32 width, u32 height, u32 pboCount) {
    shutdown();
    
    width_ = width;
    height_ = height;
    
    usize bufferSize = width * height * 4;
    pboSlots_.resize(pboCount);
    
    for (auto& slot : pboSlots_) {
        glGenBuffers(1, &slot.pbo);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, slot.pbo);
        glBufferData(GL_PIXEL_PACK_BUFFER, bufferSize, nullptr, GL_STREAM_READ);
        slot.inUse = false;
        slot.ready = false;
    }
    
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    
    initialized_ = true;
    LOG_DEBUG("AsyncFrameGrabber initialized: {}x{} with {} PBOs", width, height, pboCount);
    
    return Result<void>::ok();
}

void AsyncFrameGrabber::shutdown() {
    if (!initialized_) return;
    
    for (auto& slot : pboSlots_) {
        if (slot.pbo) {
            glDeleteBuffers(1, &slot.pbo);
            slot.pbo = 0;
        }
    }
    pboSlots_.clear();
    initialized_ = false;
}

void AsyncFrameGrabber::startRead(RenderTarget& target, i64 timestamp) {
    if (!initialized_) return;
    
    // Find free slot
    auto& slot = pboSlots_[currentSlot_];
    
    if (slot.inUse && !slot.ready) {
        // Previous read not complete, skip
        return;
    }
    
    slot.inUse = true;
    slot.ready = false;
    slot.timestamp = timestamp;
    slot.frameNumber = frameNumber_++;
    
    // Bind render target for reading
    glBindFramebuffer(GL_READ_FRAMEBUFFER, target.fbo());
    
    // Start async read to PBO
    glBindBuffer(GL_PIXEL_PACK_BUFFER, slot.pbo);
    glReadPixels(0, 0, width_, height_, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    
    // Move to next slot
    currentSlot_ = (currentSlot_ + 1) % pboSlots_.size();
}

bool AsyncFrameGrabber::getCompletedFrame(GrabbedFrame& frame) {
    if (!initialized_) return false;
    
    // Check oldest slot for completion
    for (auto& slot : pboSlots_) {
        if (slot.inUse && !slot.ready) {
            // Check if read is complete (fence would be better)
            glBindBuffer(GL_PIXEL_PACK_BUFFER, slot.pbo);
            
            void* ptr = glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
            if (ptr) {
                frame.width = width_;
                frame.height = height_;
                frame.timestamp = slot.timestamp;
                frame.frameNumber = slot.frameNumber;
                frame.data.resize(width_ * height_ * 4);
                std::memcpy(frame.data.data(), ptr, frame.data.size());
                
                glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
                glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
                
                slot.inUse = false;
                slot.ready = true;
                
                // Flip image (OpenGL is bottom-up)
                u32 rowSize = width_ * 4;
                std::vector<u8> temp(rowSize);
                for (u32 y = 0; y < height_ / 2; ++y) {
                    u8* top = frame.data.data() + y * rowSize;
                    u8* bottom = frame.data.data() + (height_ - 1 - y) * rowSize;
                    std::memcpy(temp.data(), top, rowSize);
                    std::memcpy(top, bottom, rowSize);
                    std::memcpy(bottom, temp.data(), rowSize);
                }
                
                return true;
            } else {
                // Map failed - unbind and mark slot as ready to prevent leak
                glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
                slot.inUse = false;
                slot.ready = true;
                LOG_WARN("AsyncFrameGrabber: Failed to map PBO, slot leaked prevention");
            }
        }
    }
    
    return false;
}

Result<void> AsyncFrameGrabber::resize(u32 width, u32 height) {
    if (width == width_ && height == height_) {
        return Result<void>::ok();
    }
    
    u32 pboCount = pboSlots_.size();
    shutdown();
    return init(width, height, pboCount);
}

} // namespace vc