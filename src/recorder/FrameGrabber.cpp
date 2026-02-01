#include "FrameGrabber.hpp"
#include <QOpenGLContext>
#include <QOpenGLFunctions_3_3_Core>
#include <algorithm>
#include "core/Logger.hpp"

namespace vc {

FrameGrabber::FrameGrabber() = default;

FrameGrabber::~FrameGrabber() {
    stop();
}

void FrameGrabber::setSize(u32 width, u32 height) {
    width_ = width;
    height_ = height;
}

void FrameGrabber::grab(RenderTarget& target, i64 timestamp) {
    if (!running_)
        return;

    if (!this->initializeOpenGLFunctions())
        return;

    GrabbedFrame frame;
    frame.width = target.width();
    frame.height = target.height();
    frame.timestamp = timestamp;
    frame.frameNumber = frameNumber_++;
    frame.data.resize(frame.width * frame.height * 4);

    target.readPixels(frame.data.data(), GL_RGBA, GL_UNSIGNED_BYTE);

    if (flipVertical_) {
        if (useGPUFlip_) {
            flipImageGPU(frame.data, frame.width, frame.height);
        } else {
            flipImage(frame.data, frame.width, frame.height);
        }
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

void FrameGrabber::grabScreen(u32 width, u32 height, i64 timestamp) {
    if (!running_)
        return;

    if (!this->initializeOpenGLFunctions())
        return;

    GrabbedFrame frame;
    frame.width = width;
    frame.height = height;
    frame.timestamp = timestamp;
    frame.frameNumber = frameNumber_++;
    frame.data.resize(width * height * 4);

    this->glReadPixels(
            0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, frame.data.data());

    if (flipVertical_) {
        if (useGPUFlip_) {
            flipImageGPU(frame.data, width, height);
        } else {
            flipImage(frame.data, width, height);
        }
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

    if (!queueCond_.wait_for(
                lock, std::chrono::milliseconds(timeoutMs), [this] {
                    return !frameQueue_.empty() || !running_;
                })) {
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

void FrameGrabber::flipImageGPU(std::vector<u8>& data, u32 width, u32 height) {
    // GPU-optimized flip using OpenGL texture copy with flipped coordinates
    // This avoids the O(N) CPU row-swapping by using GPU memory operations
    
    if (!this->initializeOpenGLFunctions()) {
        // Fall back to CPU flip if OpenGL not available
        flipImage(data, width, height);
        return;
    }
    
    // Create a temporary texture from the pixel data
    GLuint srcTex, dstTex, fbo;
    this->glGenTextures(1, &srcTex);
    this->glGenTextures(1, &dstTex);
    this->glGenFramebuffers(1, &fbo);
    
    // Upload source data to texture
    this->glBindTexture(GL_TEXTURE_2D, srcTex);
    this->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data.data());
    this->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    this->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    
    // Create destination texture
    this->glBindTexture(GL_TEXTURE_2D, dstTex);
    this->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    this->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    this->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    
    // Bind FBO and attach destination texture
    this->glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    this->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, dstTex, 0);
    
    // Set up viewport for flipped rendering
    this->glViewport(0, 0, width, height);
    
    // Clear
    this->glClearColor(0, 0, 0, 0);
    this->glClear(GL_COLOR_BUFFER_BIT);
    
    // Use a simple pass-through approach with flipped Y coordinates
    // Instead of using a shader, we'll use glCopyTexSubImage2D with flipped coordinates
    // Actually, let's use a more efficient approach: render quad with flipped texcoords
    
    // For now, use the simpler approach: read back with glReadPixels after setting up
    // a projection matrix flip or use texture coordinate flipping
    
    // Simple approach: use glBlitFramebuffer with flipped coordinates
    this->glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    this->glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, srcTex, 0);
    
    this->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);
    this->glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, dstTex, 0);
    
    // Blit with flipped Y coordinates (srcY0 > srcY1 to flip)
    this->glBlitFramebuffer(0, height, width, 0,  // Source: flip Y (bottom-to-top)
                           0, 0, width, height,   // Dest: normal orientation
                           GL_COLOR_BUFFER_BIT, GL_NEAREST);
    
    // Read back the flipped data
    this->glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    this->glReadBuffer(GL_COLOR_ATTACHMENT0);
    this->glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, data.data());
    
    // Cleanup
    this->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    this->glDeleteFramebuffers(1, &fbo);
    this->glDeleteTextures(1, &srcTex);
    this->glDeleteTextures(1, &dstTex);
}

AsyncFrameGrabber::AsyncFrameGrabber() = default;

AsyncFrameGrabber::~AsyncFrameGrabber() {
    shutdown();
}

Result<void> AsyncFrameGrabber::init(u32 width, u32 height, u32 pboCount) {
    if (!this->initializeOpenGLFunctions()) {
        return Result<void>::err(
                "Failed to initialize OpenGL functions for AsyncFrameGrabber");
    }

    shutdown();

    width_ = width;
    height_ = height;

    usize bufferSize = static_cast<usize>(width) * height * 4;
    pboSlots_.resize(pboCount);

    for (auto& slot : pboSlots_) {
        this->glGenBuffers(1, &slot.pbo);
        this->glBindBuffer(GL_PIXEL_PACK_BUFFER, slot.pbo);
        this->glBufferData(
                GL_PIXEL_PACK_BUFFER, bufferSize, nullptr, GL_STREAM_READ);
        slot.inUse = false;
        slot.ready = false;
    }

    this->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

    initialized_ = true;
    LOG_DEBUG("AsyncFrameGrabber initialized: {}x{} with {} PBOs",
              width,
              height,
              pboCount);

    return Result<void>::ok();
}

void AsyncFrameGrabber::shutdown() {
    if (!initialized_)
        return;

    auto* ctx = QOpenGLContext::currentContext();
    if (ctx && this->initializeOpenGLFunctions()) {
        for (auto& slot : pboSlots_) {
            if (slot.pbo) {
                this->glDeleteBuffers(1, &slot.pbo);
                slot.pbo = 0;
            }
        }
    } else {
        for (auto& slot : pboSlots_) {
            slot.pbo = 0;
        }
    }

    pboSlots_.clear();
    initialized_ = false;
}

void AsyncFrameGrabber::startRead(RenderTarget& target, i64 timestamp) {
    if (!initialized_)
        return;

    if (!this->initializeOpenGLFunctions())
        return;

    auto& slot = pboSlots_[currentSlot_];

    if (slot.inUse && !slot.ready) {
        return;
    }

    slot.inUse = true;
    slot.ready = false;
    slot.timestamp = timestamp;
    slot.frameNumber = frameNumber_++;

    this->glBindFramebuffer(GL_READ_FRAMEBUFFER, target.fbo());

    this->glBindBuffer(GL_PIXEL_PACK_BUFFER, slot.pbo);
    this->glReadPixels(
            0, 0, width_, height_, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    this->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

    this->glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);

    currentSlot_ = (currentSlot_ + 1) % pboSlots_.size();
}

bool AsyncFrameGrabber::getCompletedFrame(GrabbedFrame& frame) {
    if (!initialized_)
        return false;

    if (!this->initializeOpenGLFunctions())
        return false;

    for (auto& slot : pboSlots_) {
        if (slot.inUse && !slot.ready) {
            this->glBindBuffer(GL_PIXEL_PACK_BUFFER, slot.pbo);

            void* ptr = this->glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
            if (ptr) {
                frame.width = width_;
                frame.height = height_;
                frame.timestamp = slot.timestamp;
                frame.frameNumber = slot.frameNumber;
                frame.data.resize(static_cast<usize>(width_) * height_ * 4);
                std::memcpy(frame.data.data(), ptr, frame.data.size());

                this->glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
                this->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

                slot.inUse = false;
                slot.ready = true;

                u32 rowSize = width_ * 4;
                std::vector<u8> temp(rowSize);
                for (u32 y = 0; y < height_ / 2; ++y) {
                    u8* top = frame.data.data() + y * rowSize;
                    u8* bottom =
                            frame.data.data() + (height_ - 1 - y) * rowSize;
                    std::memcpy(temp.data(), top, rowSize);
                    std::memcpy(top, bottom, rowSize);
                    std::memcpy(bottom, temp.data(), rowSize);
                }

                return true;
            } else {
                this->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
                slot.inUse = false;
                slot.ready = true;
                LOG_WARN(
                        "AsyncFrameGrabber: Failed to map PBO, slot leaked "
                        "prevention");
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
