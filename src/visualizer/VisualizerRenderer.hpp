/**
 * @file VisualizerRenderer.hpp
 * @brief OpenGL rendering logic for projectM.
 * @version 1.1.0
 * @last-edited 2026-08-26
 *
 * This file defines the VisualizerRenderer class which handles the low-level
 * OpenGL operations, including PBO capture for recording, and the bridge to
 * the projectM library. It is decoupled from the Qt Window system to allow for
 * easier testing and potential off-screen rendering.
 *
 * @section Destination
 * projectM v4 draws its final image into the default framebuffer and nothing
 * else: ProjectM.cpp binds framebuffer 0 for the closing texture copy and
 * carries the upstream "ToDo: Allow external apps to provide a custom target
 * framebuffer". Everything here therefore reads and writes framebuffer 0. An
 * FBO exists on this class only to rescale a readback when the recording
 * resolution differs from the window's.
 *
 * @section Patterns
 * - Renderer: Encapsulates all rendering commands.
 */

#pragma once
#include "RenderTarget.hpp"
#include "projectm/Bridge.hpp"
#include "util/Types.hpp"

#include <QOpenGLFunctions_3_3_Core>
#include <memory>
#include <vector>

namespace vc {

class AudioQueue;
class PresetManager;

} // namespace vc

namespace vc {

class VisualizerRenderer : protected QOpenGLFunctions_3_3_Core {
public:
    explicit VisualizerRenderer(PresetManager& presetManager);
    ~VisualizerRenderer();

    void initialize(u32 width, u32 height);
    void cleanup();

    void render(u32 width, u32 height, bool isExposed);
    void render(u32 x, u32 y, u32 width, u32 height, bool isExposed);

    void setAudioQueue(AudioQueue* queue) { audioQueue_ = queue; }
    AudioQueue* audioQueue() const { return audioQueue_; }

    // The rate the caller actually drives render() at. Feeds the per-frame
    // audio batch size and the recording frame-rate guardrail.
    void setTargetFps(u32 fps) { targetFps_ = fps; }

    /// PCM frames to pop per render tick: one tick's worth of audio at the
    /// rate the queue actually carries — the observed sink rate, 44.1 kHz
    /// included — clamped to the pop buffer. The clamp is load-bearing: the
    /// stack buffer holds 4096 stereo frames, and a 48 kHz stream at the
    /// config-minimum 10 fps asks for 4800.
    [[nodiscard]] static constexpr u32
    framesPerRenderTick(const u32 sampleRate, const u32 targetFps, const u32 bufferFrames) {
        const u32 wanted = (sampleRate + targetFps - 1) / targetFps;
        return wanted < bufferFrames ? wanted : bufferFrames;
    }

    // Recording
    void setRecordingSize(u32 width, u32 height);
    void startRecording();
    void stopRecording();
    bool isRecording() const { return recording_; }

    // ProjectM access
    pm::Bridge& projectM() { return projectM_; }
    const pm::Bridge& projectM() const { return projectM_; }

    // Signals (proxied via parent window or custom)
    Signal<std::vector<u8>, u32, u32, i64> frameCaptured;

private:
    void renderFrame(u32 x, u32 y, u32 w, u32 h);
    void setupPBOs();
    void destroyPBOs();
    void captureDefaultFramebuffer(u32 width, u32 height);
    void captureAsync(GLuint readFramebuffer);

    pm::Bridge projectM_;
    // Readback scaling target only, created lazily when the recording
    // resolution differs from the window's. projectM never draws into it.
    RenderTarget captureTarget_;

    bool recording_{false};
    u32 recordWidth_{1920};
    u32 recordHeight_{1080};
    GLuint pbos_[2]{0, 0};
    u32 pboIndex_{0};
    bool pboAvailable_{false};

    AudioQueue* audioQueue_{nullptr};
    u32 targetFps_{60};

    bool initialized_{false};
    bool presetLoading_{false};
};

} // namespace vc
