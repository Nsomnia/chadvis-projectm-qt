#include "FrameGrabber.hpp"
#include <algorithm>
#include <cstring>

namespace vc {

FrameGrabber::FrameGrabber() = default;

FrameGrabber::~FrameGrabber() {
    stop();
}

void FrameGrabber::flipImage(std::vector<u8>& data, u32 width, u32 height) {
    if (data.size() < static_cast<usize>(width) * height * 4)
        return;

    const usize rowSize = static_cast<usize>(width) * 4;
    std::vector<u8> temp(rowSize);

    for (u32 y = 0; y < height / 2; ++y) {
        u8* top = data.data() + static_cast<usize>(y) * rowSize;
        u8* bottom = data.data() + static_cast<usize>(height - 1 - y) * rowSize;

        std::memcpy(temp.data(), top, rowSize);
        std::memcpy(top, bottom, rowSize);
        std::memcpy(bottom, temp.data(), rowSize);
    }
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

} // namespace vc
