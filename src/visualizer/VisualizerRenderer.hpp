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
#include <memory>
#include <mutex>
#include <vector>

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
    std::vector<f32> audioQueue_;
    u32 audioSampleRate_{48000};
    u32 targetFps_{60};

    bool initialized_{false};
    bool presetLoading_{false};
};

} // namespace vc
