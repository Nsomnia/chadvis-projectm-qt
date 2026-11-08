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
    /// T0026: teardown when no usable GL context exists (the window's
    /// makeCurrent failed at destruction). Abandons every GL-owning handle --
    /// projectM instance, PBOs, capture target -- without issuing a single GL
    /// call, so nothing can be deleted with no context or the wrong context
    /// current. Everything abandoned leaks; see Engine::abandon() for why a
    /// leak is the correct trade. cleanup() after this is a safe no-op.
    void abandonGlObjects();

    void render(u32 width, u32 height, bool isExposed);
    void render(u32 x, u32 y, u32 width, u32 height, bool isExposed);

    void setAudioQueue(AudioQueue* queue) { audioQueue_ = queue; }
    AudioQueue* audioQueue() const { return audioQueue_; }

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

    /// What `captureAsync` hands to the encoder on one call. Kept a pure
    /// decision so the frame-0 path is testable without a GL context: the
    /// first rendered frame used to reach no emit at all, because the map
    /// block was gated on a flag that started false (T0171). The pipelined
    /// tail is drained by `stopRecording` (T0443), not by a further render.
    enum class CaptureEmit : u8 {
        /// Complete this call's own readback inline and emit it now: the first
        /// call since setup, when nothing is pending in the sibling buffer.
        InlineOwnFrame = 0,
        /// Emit the sibling buffer's completed readback. The pipelined steady
        /// state: one frame of latency hides the readback behind the next
        /// render. The frame this leaves pending is the tail `stopRecording`
        /// drains so the recording is not one frame short.
        EmitPendingFrame = 1,
        /// Emit nothing: this call's readback becomes the next call's pending
        /// frame, seating the pipeline once.
        EmitNothing = 2,
    };

    /// Which buffer `captureAsync` emits for the state setupPBOs left
    /// behind. The first frame after setup must emit, or its recording
    /// starts a frame late (or empty, if stop comes before a second render).
    [[nodiscard]] static constexpr CaptureEmit
    captureEmit(bool firstCapture, bool hasPendingFrame) noexcept {
        if (firstCapture) return CaptureEmit::InlineOwnFrame;
        return hasPendingFrame ? CaptureEmit::EmitPendingFrame : CaptureEmit::EmitNothing;
    }

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
    /// Map the PBO at `index` and hand it to the encoder as one captured
    /// frame, stamped with the capture time of the frame it holds.
    void emitPboFrame(u32 index, u32 size, i64 timestampUs);
    /// Emit the pending sibling readback, if any, through `emitPboFrame` at
    /// its own capture timestamp, and clear it. Called only by
    /// `stopRecording`, before the PBOs are destroyed (T0443). A stop with
    /// no pending frame is a no-op: no extra frame, no null readback.
    void drainPendingFrame();

    pm::Bridge projectM_;
    // Readback scaling target only, created lazily when the recording
    // resolution differs from the window's. projectM never draws into it.
    RenderTarget captureTarget_;

    bool recording_{false};

    /// Audio-ingress diagnostics. A static canvas has exactly two shapes -- the
    /// producer never pushed, or this consumer is not the queue the producer
    /// wrote to -- and nothing else in the tree can tell them apart. One INFO
    /// line when the first batch arrives, then at most one WARN per ~10 s while
    /// the queue stays empty. Bounded on purpose: a per-frame line would be a
    /// log flood, and silence is what made the original report unactionable.
    bool audioArrivalLogged_{false};
    u32 framesWithoutAudio_{0};
    /// T0026: set by abandonGlObjects() when teardown has no usable GL
    /// context. cleanup() then touches no GL state at all -- the destructor
    /// still calls cleanup(), and it must not try to delete what was
    /// deliberately leaked.
    bool abandoned_{false};
    u32 recordWidth_{1920};
    u32 recordHeight_{1080};
    GLuint pbos_[2]{0, 0};
    u32 pboIndex_{0};
    /// True when both PBOs were created AND allocated in setupPBOs. Derived
    /// from the init outcome and set only there; the capture path reads it
    /// and never assigns it. It used to be initialised false and flipped to
    /// true by the first captureAsync, which is also what gated the emit --
    /// so the first frame's map was skipped by construction (T0171).
    bool pboAvailable_{false};
    /// True until the first captureAsync after setup: that call completes
    /// its own readback inline so frame 0 reaches the encoder from the render
    /// that produced it, not from a second render that may never come.
    bool pboFirstCapture_{true};
    /// A completed readback sits in the sibling buffer, waiting for the next
    /// call to map and emit it, or for `stopRecording` to drain it as the
    /// recording tail (T0443). False after the inline first frame (its
    /// readback was consumed in its own call) and after setup.
    bool pboHasPendingFrame_{false};
    /// Capture time (microseconds) of the pending readback above, so its
    /// emitted frame is stamped with its content's time, not its emit time.
    /// Behavior-neutral in steady state: the anchor moves by the same tick.
    i64 pboPendingTimestampUs_{0};

    AudioQueue* audioQueue_{nullptr};
    // The rate the caller actually drives render() at. Feeds the per-frame
    // audio batch size and the recording frame-rate guardrail. Configured from
    // vizConfig; there is deliberately no setter after init.
    u32 targetFps_{60};

    bool initialized_{false};
    bool presetLoading_{false};
};

} // namespace vc
