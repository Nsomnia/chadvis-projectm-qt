/**
 * @file VisualizerRenderer.hpp
 * @brief OpenGL rendering logic for projectM.
 *
 * This file defines the VisualizerRenderer class which handles the low-level
 * OpenGL operations, including FBO management, PBO capture for recording,
 * and the bridge to the projectM library. It is decoupled from the Qt Window
 * system to allow for easier testing and potential off-screen rendering.
 *
 * @section Dependencies
 * - projectM (via Bridge)
 * - Qt OpenGL (QOpenGLFunctions_3_3_Core)
 *
 * @section Patterns
 * - Renderer: Encapsulates all rendering commands.
 */

#pragma once
#include "RenderTarget.hpp"
#include "projectm/Bridge.hpp"
#include "util/GLIncludes.hpp"
#include "util/Types.hpp"

#include <QOpenGLBuffer>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <array>
#include <memory>
#include <mutex>
#include <vector>

namespace vc {

// Simple circular buffer for audio queue (avoids O(N) vector erase)
template<typename T, usize Size>
class AudioCircularBuffer {
public:
    void push(const T* data, usize count) {
        for (usize i = 0; i < count && count_ < Size; ++i) {
            buffer_[head_] = data[i];
            head_ = (head_ + 1) % Size;
            ++count_;
        }
    }
    
    void pop(usize count) {
        count = std::min(count, count_);
        tail_ = (tail_ + count) % Size;
        count_ -= count;
    }
    
    const T* data() const {
        return buffer_.data() + tail_;
    }
    
    usize size() const { return count_; }
    bool empty() const { return count_ == 0; }
    void clear() { head_ = tail_ = count_ = 0; }
    
    // Get pointer and contiguous size (may need two calls if wrapped)
    std::pair<const T*, usize> getContiguous(usize maxFrames) const {
        usize frames = std::min(maxFrames * 2, count_); // 2 channels
        if (tail_ + frames <= Size) {
            return {buffer_.data() + tail_, frames};
        } else {
            return {buffer_.data() + tail_, Size - tail_};
        }
    }
    
private:
    std::array<T, Size> buffer_{};
    usize head_ = 0;
    usize tail_ = 0;
    usize count_ = 0;
}; // AudioCircularBuffer

} // namespace vc

namespace vc {

class OverlayEngine;

class VisualizerRenderer : protected QOpenGLFunctions_3_3_Core {
public:
    VisualizerRenderer();
    ~VisualizerRenderer();

    void initialize(u32 width, u32 height);
    void cleanup();

    void render(u32 width, u32 height, bool isExposed);
    void feedAudio(const f32* data, u32 frames, u32 channels, u32 sampleRate);

    // Recording
    void setRecordingSize(u32 width, u32 height);
    void startRecording();
    void stopRecording();
    bool isRecording() const {
        return recording_;
    }

    // ProjectM access
    pm::Bridge& projectM() {
        return projectM_;
    }
    const pm::Bridge& projectM() const {
        return projectM_;
    }

    RenderTarget& renderTarget() {
        return renderTarget_;
    }

    // Overlay
    void setOverlayEngine(OverlayEngine* engine) {
        overlayEngine_ = engine;
    }

    // Signals (proxied via parent window or custom)
    Signal<std::vector<u8>, u32, u32, i64> frameCaptured;

private:
    void renderFrame(u32 w, u32 h);
    void initBlitResources();
    void drawTexture(GLuint textureId, u32 w, u32 h);
    void setupPBOs();
    void destroyPBOs();
    void captureAsync();

    pm::Bridge projectM_;
    OverlayEngine* overlayEngine_{nullptr};
    RenderTarget renderTarget_;
    RenderTarget overlayTarget_;

    std::unique_ptr<QOpenGLShaderProgram> blitProgram_;
    QOpenGLVertexArrayObject blitVao_;
    QOpenGLBuffer blitVbo_;

    bool recording_{false};
    u32 recordWidth_{1920};
    u32 recordHeight_{1080};
    GLuint pbos_[2]{0, 0};
    u32 pboIndex_{0};
    bool pboAvailable_{false};

    std::mutex audioMutex_;
    // Circular buffer for O(1) audio queue operations (fixes O(N) vector erase)
    static constexpr usize AUDIO_QUEUE_SIZE = 16384; // ~170ms at 48kHz stereo
    AudioCircularBuffer<f32, AUDIO_QUEUE_SIZE> audioQueue_;
    u32 audioSampleRate_{48000};
    u32 targetFps_{60};

    bool initialized_{false};
    bool presetLoading_{false};
};

} // namespace vc
