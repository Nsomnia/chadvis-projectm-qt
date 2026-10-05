#include "recorder/RenderExecutor.hpp"

#include "audio/AudioQueue.hpp"
#include "core/Logger.hpp"
#include "recorder/AudioFileDecoder.hpp"
#include "recorder/VideoRecorderCore.hpp"
#include "util/JThread.hpp"
#include "util/Result.hpp"

#include <QDateTime>
#include <QElapsedTimer>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <limits>
#include <mutex>

namespace vc {
namespace {

/// Microseconds per second, for the frame timestamp.
///
/// The encoder's timeline comes from `VideoRecorderFFmpeg::presentationTimestampFor`,
/// which anchors the origin to 0 and rescales real capture microseconds. Frame N
/// of a job is therefore *always* `N/fps` seconds regardless of when the GL thread
/// got round to it -- which is the property that makes frame-level interleaving
/// timeline-safe. A wall-clock-derived timestamp would let a job that lost a turn
/// to another job come out with a hole in it.
constexpr i64 kMicrosecondsPerSecond = 1'000'000;

[[nodiscard]] i64 frameTimestampUs(const u64 frameIndex, const u32 fps) noexcept {
    if (fps == 0) return 0;
    return static_cast<i64>((frameIndex * static_cast<u64>(kMicrosecondsPerSecond)) / fps);
}

[[nodiscard]] u64 audioFramesForVideoFrames(const u64 videoFrames, const u32 sampleRate,
                                            const u32 fps) noexcept {
    if (fps == 0) return 0;
    return (videoFrames * static_cast<u64>(sampleRate)) / static_cast<u64>(fps);
}

[[nodiscard]] QString utcNowIso() { return QDateTime::currentDateTimeUtc().toString(Qt::ISODate); }

// ── the schema-slug -> enum adapter ──────────────────────────────────────────
// RenderJob.hpp freezes the vocabulary at schema v1 and says so explicitly: "the
// slug -> enum mapping is the render worker's one-time adapter job". This is it.
// Deliberately a *lookup* and not a cast: an enum ordinal is not a wire value, and
// `isKnownToken` already guarantees membership, so an unknown slug is a defect
// here rather than a runtime surprise inside `VideoRecorderFFmpeg`.

[[nodiscard]] VideoCodec videoCodecFromSlug(std::string_view slug) noexcept {
    if (slug == "h264") return VideoCodec::H264;
    if (slug == "h265") return VideoCodec::H265;
    if (slug == "vp9") return VideoCodec::VP9;
    if (slug == "av1") return VideoCodec::AV1;
    if (slug == "prores") return VideoCodec::ProRes;
    if (slug == "ffv1") return VideoCodec::FFV1;
    if (slug == "h264_nvenc") return VideoCodec::H264_NVENC;
    if (slug == "h265_nvenc") return VideoCodec::H265_NVENC;
    if (slug == "h264_vaapi") return VideoCodec::H264_VAAPI;
    if (slug == "h265_vaapi") return VideoCodec::H265_VAAPI;
    return VideoCodec::H264;
}

[[nodiscard]] PixelFormat pixelFormatFromSlug(std::string_view slug) noexcept {
    if (slug == "yuv422p") return PixelFormat::YUV422P;
    if (slug == "yuv444p") return PixelFormat::YUV444P;
    if (slug == "rgb24") return PixelFormat::RGB24;
    if (slug == "nv12") return PixelFormat::NV12;
    if (slug == "p010le") return PixelFormat::P010LE;
    return PixelFormat::YUV420P;
}

[[nodiscard]] Container containerFromSlug(std::string_view slug) noexcept {
    if (slug == "mkv") return Container::MKV;
    if (slug == "webm") return Container::WebM;
    if (slug == "mov") return Container::MOV;
    if (slug == "avi") return Container::AVI;
    return Container::MP4;
}

[[nodiscard]] EncoderPreset encoderPresetFromSlug(std::string_view slug) noexcept {
    if (slug == "ultrafast") return EncoderPreset::Ultrafast;
    if (slug == "superfast") return EncoderPreset::Superfast;
    if (slug == "veryfast") return EncoderPreset::Veryfast;
    if (slug == "faster") return EncoderPreset::Faster;
    if (slug == "fast") return EncoderPreset::Fast;
    if (slug == "slow") return EncoderPreset::Slow;
    if (slug == "slower") return EncoderPreset::Slower;
    if (slug == "veryslow") return EncoderPreset::Veryslow;
    if (slug == "placebo") return EncoderPreset::Placebo;
    return EncoderPreset::Medium;
}

[[nodiscard]] AudioCodec audioCodecFromSlug(std::string_view slug) noexcept {
    if (slug == "opus") return AudioCodec::Opus;
    if (slug == "flac") return AudioCodec::FLAC;
    if (slug == "mp3") return AudioCodec::MP3;
    if (slug == "pcm") return AudioCodec::PCM;
    return AudioCodec::AAC;
}

[[nodiscard]] HWAccelDevice hwAccelFromSlug(std::string_view slug) noexcept {
    if (slug == "nvenc") return HWAccelDevice::NVENC;
    if (slug == "vaapi") return HWAccelDevice::VAAPI;
    if (slug == "amf") return HWAccelDevice::AMF;
    if (slug == "quicksync") return HWAccelDevice::QuickSync;
    return HWAccelDevice::None;
}

[[nodiscard]] EncoderSettings encoderSettingsFor(const RenderJob& job) {
    EncoderSettings s;
    s.video.codec = videoCodecFromSlug(job.videoCodec);
    s.video.width = job.width;
    s.video.height = job.height;
    s.video.fps = job.fps;
    s.video.crf = job.crf;
    s.video.preset = encoderPresetFromSlug(job.encoderPreset);
    s.video.pixelFormat = pixelFormatFromSlug(job.pixelFormat);
    s.video.gopSize = job.gopSize;
    s.video.twoPass = job.twoPass;
    s.video.hwAccel = hwAccelFromSlug(job.hardwareAccel);
    s.audio.codec = audioCodecFromSlug(job.audioCodec);
    s.audio.sampleRate = job.audioSampleRate;
    s.audio.channels = job.audioChannels;
    s.audio.bitrate = job.audioBitrateKbps;
    s.container = containerFromSlug(job.container);
    s.outputPath = job.outputPath;
    s.title = job.label;
    return s;
}

} // namespace

// ═════════════════════════════════════════════════════════════════════════════
// Names
// ═════════════════════════════════════════════════════════════════════════════

std::string_view slotVerdictName(const SlotVerdict verdict) noexcept {
    switch (verdict) {
        case SlotVerdict::Render:
            return "Render";
        case SlotVerdict::Skip:
            return "Skip";
        case SlotVerdict::Finish:
            return "Finish";
        case SlotVerdict::Retire:
            return "Retire";
    }
    return "?";
}

std::string_view renderTopologyName(const RenderTopology topology) noexcept {
    switch (topology) {
        case RenderTopology::Unsupported:
            return "Unsupported";
        case RenderTopology::GuiThreadShared:
            return "GuiThreadShared";
        case RenderTopology::GuiThreadExclusive:
            return "GuiThreadExclusive";
        case RenderTopology::WorkerPerJob:
            return "WorkerPerJob";
    }
    return "?";
}

// ═════════════════════════════════════════════════════════════════════════════
// FrameInterleaver — the pure policy
// ═════════════════════════════════════════════════════════════════════════════

FrameInterleaver::FrameInterleaver(const u32 quantumFrames, const u64 frameCap)
    : quantumFrames_(quantumFrames == 0 ? 1u : quantumFrames), frameCap_(frameCap) {}

/// The parameter is `table`, not `slots`: Qt's `slots` keyword macro is live in
/// this tree, and an empty macro in front of `std::move(slots)` silently becomes a
/// zero-argument call rather than a diagnostic that points at the macro.
void FrameInterleaver::sync(std::vector<ScheduleSlot> table) {
    slots_ = std::move(table);
    if (cursor_ >= slots_.size()) {
        cursor_ = 0;
    }
}

void FrameInterleaver::setQuantumFrames(const u32 frames) noexcept {
    quantumFrames_ = frames == 0 ? 1u : frames;
}

void FrameInterleaver::setLiveVisualizerActive(const bool active) noexcept {
    liveVisualizer_ = active;
}

bool FrameInterleaver::exhausted(const ScheduleSlot& slot) const noexcept {
    // Two bounds, and the second exists because `expectedFrames == 0` is legal
    // (reported as -1% progress). An unbounded render with a misbehaving decoder
    // is an infinite job, so the cap is a real backstop rather than a policy.
    if (slot.framesExpected > 0 && slot.framesDone >= slot.framesExpected) return true;
    return slot.framesDone >= frameCap_;
}

ScheduleStep FrameInterleaver::next() noexcept {
    ScheduleStep step;
    if (slots_.empty()) {
        step.verdict = SlotVerdict::Skip;
        return step;
    }

    // Rotation: at most `slots_.size()` candidates, because every candidate is
    // either served or stepped over exactly once before the cursor comes back.
    for (std::size_t probe = 0; probe < slots_.size(); ++probe) {
        const std::size_t index = cursor_;
        cursor_ = (cursor_ + 1) % slots_.size();
        const ScheduleSlot& slot = slots_[index];

        step.slotIndex = index;
        step.jobId = slot.jobId;
        step.frames = 0;

        // Terminal intents are decided before any gating, and they are decided
        // for the *head* candidate: a cancel on slot 0 is therefore honoured at
        // the next step rather than after slot 0's next quantum. That is the
        // whole cancellation-latency claim.
        if (slot.cancelRequested) {
            step.verdict = SlotVerdict::Retire;
            return step;
        }
        if (exhausted(slot)) {
            step.verdict = SlotVerdict::Finish;
            return step;
        }

        // The audio gate. Stepping over costs one comparison per pass and is
        // counted, because a job that is permanently blocked has to be visible
        // rather than merely slow.
        if (slot.audioBehind) {
            ++step.skippedBehind;
            continue;
        }

        u32 frames = quantumFrames_;
        if (liveVisualizer_) {
            frames = std::min(frames, kLiveVisualizerQuantumFrames);
        }
        if (slot.framesExpected > 0) {
            const u64 remaining = slot.framesExpected - slot.framesDone;
            frames = static_cast<u32>(std::min<u64>(frames, remaining));
        }
        if (frames == 0) {
            // Only reachable for a slot whose remaining count is 0, which
            // `exhausted` already caught. Guarded anyway so a future change to
            // the quantum cannot produce a Render step with zero frames, which
            // would spin the turn without doing work.
            ++step.skippedBehind;
            continue;
        }
        step.verdict = SlotVerdict::Render;
        step.frames = frames;
        return step;
    }

    // Every candidate was blocked. Not a retirement: no slot is dropped, and the
    // caller should come back rather than settle anything.
    step.slotIndex = 0;
    step.jobId.clear();
    step.verdict = SlotVerdict::Skip;
    return step;
}

std::optional<std::size_t> sceneIndexForFrame(const RenderJob& job, const u64 frameIndex) noexcept {
    const u32 fps = job.fps == 0 ? 1u : job.fps;
    u64 cursorFrame = 0;
    for (std::size_t i = 0; i < job.scenes.size(); ++i) {
        const auto frames =
                static_cast<u64>(job.scenes[i].durationSeconds * static_cast<f64>(fps) + 0.5);
        // A scene with a non-positive duration still gets one frame rather than
        // zero: a zero-frame scene would be unreachable, and a preset the author
        // wrote would silently never load.
        const u64 span = frames == 0 ? 1u : frames;
        if (frameIndex < cursorFrame + span) return i;
        cursorFrame += span;
    }
    return std::nullopt;
}

// ═════════════════════════════════════════════════════════════════════════════
// decideConcurrency
// ═════════════════════════════════════════════════════════════════════════════

ConcurrencyDecision decideConcurrency(const RenderThreadVerdict verdict,
                                      const FrameCalibration& calibration,
                                      const double throughputBudgetMs) noexcept {
    ConcurrencyDecision d;

    switch (verdict) {
        case RenderThreadVerdict::Unsupported:
            d.topology = RenderTopology::Unsupported;
            d.maxConcurrent = 0;
            d.reason = "no drawable on any thread (or the main-thread control failed too): "
                       "batch rendering is not possible here and must degrade to a visible error";
            return d;

        case RenderThreadVerdict::NeedsMainThread:
            // This is a topology, and the ceiling that follows from it is *how many
            // jobs to interleave* rather than how many contexts to hold. The
            // context count is fixed at one by the platform and no knob reaches
            // it; the job count is ours. That is why this is not 1.
            d.topology = RenderTopology::GuiThreadShared;
            d.maxConcurrent = kDefaultInterleaveDepth;
            d.reason = "measured NeedsMainThread: one GUI-thread drawable, so N jobs are N "
                       "interleaved jobs over one context; maxConcurrent now bounds admitted "
                       "jobs, not contexts";
            return d;

        case RenderThreadVerdict::Supported:
            d.topology = RenderTopology::WorkerPerJob;
            if (!calibration.measured || calibration.meanFrameMs <= 0.0) {
                d.maxConcurrent = 1;
                d.requiresCalibration = true;
                d.reason = "measured Supported: a worker thread can hold a drawable, so the "
                           "right executor is worker-per-job and its ceiling comes from a "
                           "frame-cost pass on this machine -- no number is invented here";
                return d;
            }
            // Only reachable for a caller that measured. Kept because the
            // computation is the one the report says must happen, and it is
            // here rather than in a comment so it can be tested.
            d.maxConcurrent =
                    std::clamp(static_cast<int>(throughputBudgetMs / calibration.meanFrameMs), 1,
                               kCalibratedCeilingCap);
            d.reason = "measured Supported and calibrated at " +
                       std::to_string(calibration.meanFrameMs) + " ms/frame over " +
                       std::to_string(calibration.frames) + " frames";
            return d;
    }

    d.topology = RenderTopology::Unsupported;
    d.maxConcurrent = 0;
    d.reason = "unknown verdict value";
    return d;
}

// ═════════════════════════════════════════════════════════════════════════════
// RenderExecutor
// ═════════════════════════════════════════════════════════════════════════════

/// One in-flight job. `AudioFileDecoder` and `VideoRecorder` live here so they can
/// be *created and started* on the pump thread -- `avformat_open_input` and
/// `avio_open` are blocking disk I/O and must not be on the GUI thread. Neither
/// touches GL.
struct RenderExecutor::JobSlot {
    RenderJob job;

    /// SPSC and owned: one producer (this slot's pump), one consumer (this slot's
    /// encoder thread). Sharing one `AudioQueue` across jobs would be MPSC, which
    /// `moodycamel::ReaderWriterQueue` is not.
    std::unique_ptr<AudioQueue> audio;
    std::unique_ptr<AudioFileDecoder> decoder;
    std::unique_ptr<VideoRecorder> recorder;

    /// Pump thread. Its `JobSlot*` is stable because the holder is a
    /// `unique_ptr`, so it stays valid until `join()` -- and the GUI thread
    /// always joins before erasing.
    JThread pump;
    std::mutex pumpMutex;
    std::condition_variable pumpCv;

    std::atomic<u64> framesProduced{0};
    /// PCM frames that actually reached the `rec` queue.
    std::atomic<u64> audioFramesReady{0};
    /// Sum of PCM frames the queue refused. Carried into the receipt's note; a
    /// dropped audio frame is a permanent desync and must never be silent.
    std::atomic<u64> audioFramesDropped{0};
    /// Mirrors `pump.request_stop()`. `JThread`'s public surface is
    /// `request_stop`/`join`/`joinable` on both the `std::jthread` alias and the
    /// fallback, and the fallback has no `get_stop_token`, so the pump polls this
    /// rather than depending on which one it got.
    std::atomic<bool> stopRequested{false};

    std::atomic<bool> setupDone{false};
    std::atomic<bool> setupFailed{false};
    std::string setupError;

    /// Scene index whose preset is currently loaded; `noScene` means none yet, so
    /// the first frame always loads.
    static constexpr u32 kNoScene = std::numeric_limits<u32>::max();
    u32 loadedScene{kNoScene};

    QElapsedTimer clock;
    std::vector<u8> frame;
    std::string note;
    bool cancelled{false};
};

namespace {

/// `AudioQueue::pushAll` writes **both** queues and returns `a && b`, and nothing
/// ever drains a render job's `viz` queue -- there is no visualizer attached to a
/// batch job. So the `viz` half fills and then refuses forever, and every
/// subsequent push reports a loss that never touched the recorder.
///
/// Draining it here is the fix that needs no change to `AudioQueue`, which is a
/// file this executor does not own. It is a straight copy of what was just pushed
/// into a reused scratch buffer, so the cost is one memcpy of the chunk, and it is
/// the reason `audioQueueEntriesFor` can be small. **A per-queue `pushRecOnly` on
/// `AudioQueue` would delete this function entirely**, and that is the change a
/// follow-up should make.
void drainUnreadVizQueue(AudioQueue& queue, const u32 frames) {
    if (frames == 0) return;
    static thread_local std::vector<float> scratch;
    scratch.resize(static_cast<std::size_t>(frames) * 2);
    queue.popVizBatch(scratch.data(), frames);
}

} // namespace

RenderExecutor::RenderExecutor(RenderQueue* queue, RenderFrameBackend* backend, QObject* parent)
    : QObject(parent), queue_(queue), backend_(backend), interleaver_(kDefaultQuantumFrames) {
    if (queue_ == nullptr) {
        LOG_ERROR("RenderExecutor: no RenderQueue; refusing to exist");
    }
}

RenderExecutor::~RenderExecutor() {
    stop();
    // Reverse order: the pump must stop before the decoder it is using, and the
    // recorder before the audio queue it drains.
    for (auto& slot : slots_) {
        slot->stopRequested.store(true, std::memory_order_release);
        slot->pump.request_stop();
        slot->pumpCv.notify_all();
    }
    for (auto& slot : slots_) {
        if (slot->pump.joinable()) slot->pump.join();
        if (slot->recorder) (void)slot->recorder->stop();
    }
    slots_.clear();
    if (glObjectCounter_ > 0 && backend_ != nullptr) {
        backend_->destroy();
        glObjectCounter_ = 0;
    }
}

void RenderExecutor::setConfig(const Config& config) {
    config_ = config;
    config_.audioChunkFrames =
            std::clamp<u32>(config_.audioChunkFrames, 64u, AudioFileDecoder::kMaxChunkFrames);
    interleaver_.setQuantumFrames(config_.quantumFrames);
    interleaver_.setFrameCap(config_.frameCap);
}

std::expected<void, std::string>
RenderExecutor::configureForVerdict(const RenderThreadVerdict verdict) {
    decision_ = decideConcurrency(verdict, FrameCalibration{}, config_.throughputBudgetMs);
    LOG_INFO("RenderExecutor: verdict {} -> topology {} maxConcurrent={} ({})", toString(verdict),
             renderTopologyName(decision_.topology), decision_.maxConcurrent, decision_.reason);

    switch (decision_.topology) {
        case RenderTopology::Unsupported:
            return std::unexpected(std::string("batch rendering is unavailable: ") +
                                   decision_.reason);
        case RenderTopology::WorkerPerJob:
            return std::unexpected(
                    std::string("this machine measured a working off-thread drawable, which "
                                "needs a worker-per-job executor; RenderExecutor owns one "
                                "GUI-thread context and will not fake the other shape") +
                    (decision_.requiresCalibration ? " (no frame-cost pass has run)" : "") +
                    ". Calibrated ceiling would be " + std::to_string(decision_.maxConcurrent) +
                    ".");
        case RenderTopology::GuiThreadShared:
        case RenderTopology::GuiThreadExclusive:
            break;
    }

    queue_->setMaxConcurrent(decision_.maxConcurrent);
    return {};
}

JobRunner RenderExecutor::jobRunner() {
    return [this](RenderJob job) {
        // The queue may destroy its own copy the instant this returns, so the
        // job is moved into the slot and nothing borrows from the parameter.
        const std::string id = job.id;
        if (slotFor(id) != nullptr) {
            queue_->fail(id, RenderFailure{RenderFailureKind::Permanent, 0,
                                           "executor already holds a slot for this id"});
            return;
        }
        if (backend_ == nullptr) {
            queue_->fail(id, RenderFailure{RenderFailureKind::Permanent, 0,
                                           "no RenderFrameBackend: offline rendering has "
                                           "no drawable owner in this build"});
            return;
        }

        auto slot = std::make_unique<JobSlot>();
        slot->job = std::move(job);
        slot->audio = std::make_unique<AudioQueue>(audioQueueEntriesFor(
                config_.quantumFrames, slot->job.audioSampleRate, slot->job.fps));
        slot->decoder = std::make_unique<AudioFileDecoder>(AudioFileDecoder::OutputSpec{
                .sampleRate = slot->job.audioSampleRate,
                .channels = std::clamp<u32>(slot->job.audioChannels, 1u,
                                            AudioFileDecoder::kMaxChannels),
                .chunkFrames = config_.audioChunkFrames,
        });
        slot->recorder = std::make_unique<VideoRecorder>();
        slot->clock.start();

        // `audioBehind` starts true: zero audio has been produced, so this job is
        // behind by definition and the gate holds until its pump says otherwise.
        JobSlot* raw = slot.get();
        slots_.push_back(std::move(slot));
        raw->pump = JThread([this, raw](const StopToken& token) { pumpLoop(raw); });
        raw->pumpCv.notify_all();

        if (auto created = ensureBackend(); !created) {
            retireSlot(slots_.size() - 1, "backend create failed: " + created.error());
        }
    };
}

std::expected<void, std::string> RenderExecutor::ensureBackend() {
    if (glObjectCounter_ > 0) return {};
    if (backend_ == nullptr) {
        return std::unexpected("no RenderFrameBackend");
    }
    if (auto created = backend_->create(); !created) {
        return std::unexpected(created.error());
    }
    // The context, the surface and the `pm::Engine` now exist, once, for the
    // process. No job's create or destroy path touches this counter again.
    ++glObjectCounter_;
    LOG_INFO("RenderExecutor: created the GL backend once; per-job GL objects = 0");
    return {};
}

CancelHook RenderExecutor::cancelHook() {
    return [this](const std::string& jobId) {
        if (JobSlot* slot = slotFor(jobId); slot != nullptr) {
            slot->cancelled = true;
            slot->stopRequested.store(true, std::memory_order_release);
            slot->pump.request_stop();
            slot->pumpCv.notify_all();
        }
    };
}

void RenderExecutor::start() {
    if (started_) return;
    if (timer_ == nullptr) {
        timer_ = new QTimer(this);
        timer_->setTimerType(Qt::PreciseTimer);
        QObject::connect(timer_, &QTimer::timeout, this, [this] {
            if (renderTurn() == 0 && isIdle()) stop();
        });
    }
    timer_->start(0);
    started_ = true;
}

void RenderExecutor::stop() {
    if (timer_ != nullptr) timer_->stop();
    started_ = false;
}

void RenderExecutor::setLiveVisualizerActive(const bool active) noexcept {
    interleaver_.setLiveVisualizerActive(active);
}

bool RenderExecutor::liveVisualizerActive() const noexcept {
    return interleaver_.liveVisualizerActive();
}

void RenderExecutor::cancelAll() {
    for (auto& slot : slots_) {
        slot->cancelled = true;
        slot->stopRequested.store(true, std::memory_order_release);
        slot->pump.request_stop();
        slot->pumpCv.notify_all();
    }
}

std::size_t RenderExecutor::activeSlots() const noexcept { return slots_.size(); }

u32 RenderExecutor::audioQueueEntriesFor(const u32 quantumFrames, const u32 sampleRate,
                                         const u32 fps) {
    // One quantum of video, in `AudioFrame` entries (AUDIO_FRAME_SAMPLES PCM
    // frames each), plus the chunk in flight, plus a margin. Sizing the queue from
    // the quantum rather than from `DEFAULT_QUEUE_CAPACITY` is what makes the
    // audio gate unreachable in practice: the producer may be at most one quantum
    // ahead by construction, and the queue holds that plus slack. At 60 fps, 8
    // frames and 48 kHz this is 1280 entries = 74 KiB per job, against the 3-second
    // 2.3 MiB default -- a render job needs a rendezvous, not a reservoir.
    const u64 quantumAudioFrames =
            audioFramesForVideoFrames(std::max(1u, quantumFrames), sampleRate, fps);
    const u64 entries = quantumAudioFrames / AUDIO_FRAME_SAMPLES + 128;
    return static_cast<u32>(std::min<u64>(entries, std::numeric_limits<u32>::max()));
}

RenderExecutor::JobSlot* RenderExecutor::slotFor(const std::string& jobId) noexcept {
    for (auto& slot : slots_) {
        if (slot->job.id == jobId) return slot.get();
    }
    return nullptr;
}

bool RenderExecutor::audioSatisfiesFrame(JobSlot& slot, const u32 sampleRate, const u32 fps,
                                         const u32 quantumFrames) noexcept {
    // One video frame is exactly `sampleRate / fps` PCM frames, so what a job owes
    // for the frame it is about to render is arithmetic on its own counter. No wall
    // clock appears here, which is the whole reason a slow context cannot desync it.
    const u64 produced = slot.framesProduced.load(std::memory_order_acquire);
    const u64 required = audioFramesForVideoFrames(produced + 1, sampleRate, fps);
    const u64 slack = audioFramesForVideoFrames(std::max(1u, quantumFrames), sampleRate, fps);
    const u64 have = slot.audioFramesReady.load(std::memory_order_acquire);
    if (slot.setupFailed.load(std::memory_order_acquire)) return true; // retired anyway
    return have + slack >= required;
}

void RenderExecutor::beginPass() {
    std::vector<ScheduleSlot> table;
    table.reserve(slots_.size());
    for (auto& slot : slots_) {
        ScheduleSlot s;
        s.jobId = slot->job.id;
        s.framesDone = slot->framesProduced.load(std::memory_order_acquire);
        s.framesExpected = slot->job.expectedFrames;
        s.cancelRequested = slot->cancelled;
        s.audioBehind = !audioSatisfiesFrame(*slot, slot->job.audioSampleRate, slot->job.fps,
                                             config_.quantumFrames);
        table.push_back(std::move(s));
    }
    interleaver_.sync(std::move(table));
}

u32 RenderExecutor::renderTurn() {
    if (slots_.empty()) return 0;
    beginPass();

    const ScheduleStep step = interleaver_.next();
    switch (step.verdict) {
        case SlotVerdict::Skip:
            // Every job is waiting on its audio. Come back; nothing is settled.
            return 0;
        case SlotVerdict::Finish:
            finishSlot(step.slotIndex);
            return 0;
        case SlotVerdict::Retire:
            retireSlot(step.slotIndex, "cancelled");
            return 0;
        case SlotVerdict::Render:
            break;
    }

    if (step.slotIndex >= slots_.size()) return 0;
    renderSlot(step.slotIndex, step.frames);
    return step.frames;
}

void RenderExecutor::renderSlot(std::size_t index, const u32 frames) {
    JobSlot& slot = *slots_[index];
    if (slot.setupFailed.load(std::memory_order_acquire)) {
        retireSlot(index, slot.setupError);
        return;
    }
    if (!slot.setupDone.load(std::memory_order_acquire)) {
        // The pump has not opened the decoder yet. `audioBehind` is already true so
        // the interleaver should have skipped it; reaching here means the two
        // disagreed, and doing nothing is the safe answer.
        return;
    }

    const u64 base = slot.framesProduced.load(std::memory_order_relaxed);

    for (u32 i = 0; i < frames; ++i) {
        // Preset switching only on a scene boundary: a 12-scene job pays 12
        // `loadPreset` calls instead of 1800. `crossfadeSeconds` is the backend's
        // to honour via projectM's soft-cut, not ours.
        const auto scene = sceneIndexForFrame(slot.job, base + i);
        if (scene && *scene != slot.loadedScene) {
            const u32 previous = slot.loadedScene;
            if (auto resolved = backend_->resolvePreset(slot.job.scenes[*scene].presetName);
                !resolved) {
                retireSlot(index, "preset \"" + slot.job.scenes[*scene].presetName +
                                          "\": " + resolved.error());
                return;
            } else if (auto selected = backend_->selectPreset(*resolved); !selected) {
                retireSlot(index, "preset load failed: " + selected.error());
                return;
            }
            // A hard cut on a scene change with no crossfade; a soft-cut preset
            // boundary is the backend's decision and is recorded in the receipt
            // note so a re-render can tell the two apart.
            if (previous != JobSlot::kNoScene && slot.job.scenes[*scene].crossfadeSeconds <= 0.0) {
                slot.note += "hard cut at frame " + std::to_string(base + i) + "; ";
            }
            slot.loadedScene = static_cast<u32>(*scene);
        }

        if (auto rendered = backend_->renderFrame(slot.job.width, slot.job.height, slot.frame);
            !rendered) {
            retireSlot(index, "render failed: " + rendered.error());
            return;
        }

        if (queue_->isCancelRequested(slot.job.id)) {
            slot.cancelled = true;
            retireSlot(index, "cancelled");
            return;
        }

        slot.recorder->submitVideoFrame(slot.frame.data(), slot.job.width, slot.job.height,
                                        frameTimestampUs(base + i, slot.job.fps));
        // The pump's only reason to exist. Relaxed is correct: the pump re-reads it
        // under `pumpMutex`, and the mutex provides the edge.
        slot.framesProduced.store(base + i + 1, std::memory_order_relaxed);
        slot.pumpCv.notify_all();
    }

    queue_->reportProgress(slot.job.id, base + frames);
}

void RenderExecutor::pumpLoop(JobSlot* slot) {
    // ── setup, here and not on the GUI thread ──
    // avio_open + avformat_write_header and avformat_open_input are blocking disk
    // I/O, tens of milliseconds each, once per job. In the JobRunner they would be
    // a visible hitch at the front of every job, and the JobRunner runs
    // synchronously on the GUI thread.
    const auto settings = encoderSettingsFor(slot->job);
    if (auto started = slot->recorder->start(settings); !started) {
        slot->setupError = "encoder: " + started.error().full();
        slot->setupFailed.store(true, std::memory_order_release);
        return;
    }
    if (auto opened = slot->decoder->open(slot->job.audioPath); !opened) {
        (void)slot->recorder->stop();
        slot->setupError = "audio: " + opened.error().describe();
        slot->setupFailed.store(true, std::memory_order_release);
        return;
    }
    slot->setupDone.store(true, std::memory_order_release);
    slot->pumpCv.notify_all();

    // ── pump ──
    const u32 sampleRate = slot->decoder->sampleRate();
    const u32 fps = slot->job.fps == 0 ? 1u : slot->job.fps;
    PcmChunk chunk;

    while (slot->stopRequested.load(std::memory_order_acquire) == false) {
        const u64 target = audioFramesForVideoFrames(
                slot->framesProduced.load(std::memory_order_acquire) + 1, sampleRate, fps);
        if (slot->audioFramesReady.load(std::memory_order_acquire) >= target) {
            std::unique_lock lock(slot->pumpMutex);
            slot->pumpCv.wait_for(lock, std::chrono::milliseconds(20), [&] {
                return slot->stopRequested.load(std::memory_order_acquire) ||
                       slot->audioFramesReady.load(std::memory_order_acquire) < target;
            });
            continue;
        }

        if (auto status = slot->decoder->nextChunk(chunk); !status) {
            slot->setupError = "audio decode: " + status.error().describe();
            slot->setupFailed.store(true, std::memory_order_release);
            return;
        } else if (*status == ChunkStatus::EndOfStream) {
            // Clean end. The job will still be asked for every expected frame, so
            // a file whose audio ran out early ends up short and the queue files it
            // as CompletedPartial -- which is the honest outcome.
            break;
        }

        auto push = slot->decoder->pushChunkTo(*slot->audio);
        if (!push) {
            slot->setupError = "audio push: " + push.error().describe();
            slot->setupFailed.store(true, std::memory_order_release);
            return;
        }
        drainUnreadVizQueue(*slot->audio, push->frames);

        // `pushAll` pushes a *prefix* and returns false, and the decoder has already
        // moved on, so a refusal cannot be retried: the cursor advances by exactly
        // what landed and the loss is carried into the receipt. Preventing it is the
        // queue's job (see audioQueueEntriesFor); reporting it is this loop's.
        const u64 accepted = push->framesAcceptedByRecorder();
        const u64 refused = push->frames - accepted;
        slot->audioFramesReady.fetch_add(accepted, std::memory_order_release);
        if (refused > 0) {
            slot->audioFramesDropped.fetch_add(refused, std::memory_order_relaxed);
            LOG_WARN("RenderExecutor: job {} lost {} audio frames to a full recorder "
                     "queue; A/V desync is permanent",
                     slot->job.id, refused);
        }
        slot->pumpCv.notify_all();
    }
}

void RenderExecutor::teardownSlot(const std::size_t index) {
    JobSlot& slot = *slots_[index];
    slot.stopRequested.store(true, std::memory_order_release);
    slot.pump.request_stop();
    slot.pumpCv.notify_all();
    if (slot.pump.joinable()) slot.pump.join();
    if (slot.setupDone.load(std::memory_order_acquire)) {
        if (auto stopped = slot.recorder->stop(); !stopped) {
            slot.note += "encoder stop: " + stopped.error().full() + "; ";
        }
    }
    queue_->reportProgress(slot.job.id, slot.framesProduced.load(std::memory_order_acquire));
}

void RenderExecutor::finishSlot(const std::size_t index) {
    JobSlot& slot = *slots_[index];
    const u64 written = slot.framesProduced.load(std::memory_order_acquire);
    teardownSlot(index);

    const RecordingStats stats = slot.recorder->getCurrentStats();
    RenderReceipt receipt = slot.job.receipt;
    receipt.outcome = RenderState::Completed;
    receipt.framesExpected = slot.job.expectedFrames;
    // `stats.framesWritten` is what the encoder accepted, which is >= what we
    // produced; the honest numerator for "how far did this render get" is the
    // smaller one, so the queue's Completed-vs-CompletedPartial check compares
    // against a number the executor can actually vouch for.
    receipt.framesWritten = std::min<u64>(written, stats.framesWritten);
    receipt.bytesWritten = stats.bytesWritten;
    receipt.elapsedMs = static_cast<u64>(slot.clock.elapsed());
    receipt.finishedUtc = utcNowIso().toStdString();

    const u64 dropped = slot.audioFramesDropped.load(std::memory_order_relaxed);
    if (dropped > 0) {
        receipt.note += std::to_string(dropped) +
                        " audio frames were refused by a full recorder queue, so this "
                        "file has a permanent A/V desync; ";
    }
    if (!slot.note.empty()) receipt.note += slot.note;

    const std::string id = slot.job.id;
    (void)queue_->finish(id, std::move(receipt));
    slots_.erase(slots_.begin() + static_cast<std::ptrdiff_t>(index));
}

void RenderExecutor::retireSlot(const std::size_t index, std::string reason) {
    if (index >= slots_.size()) return;
    JobSlot& slot = *slots_[index];
    if (slot.setupFailed.load(std::memory_order_acquire) && !slot.setupError.empty()) {
        reason = slot.setupError;
    }
    teardownSlot(index);

    const RenderFailureKind kind =
            slot.cancelled ? RenderFailureKind::Cancelled : RenderFailureKind::Permanent;
    const std::string id = slot.job.id;
    (void)queue_->fail(id, RenderFailure{kind, 0, std::move(reason)});
    slots_.erase(slots_.begin() + static_cast<std::ptrdiff_t>(index));
}

} // namespace vc