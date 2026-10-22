#include "audio/VisualizerAudioTap.hpp"

#include "audio/AudioQueue.hpp"
#include "core/Logger.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace vc {

namespace {
constexpr u32 kOutputRate = 48000;
constexpr u32 kChunkFrames = 4096;
} // namespace

VisualizerAudioTap::VisualizerAudioTap(AudioQueue& queue) : queue_(queue) {
    decoder_ = std::make_unique<AudioFileDecoder>(AudioFileDecoder::OutputSpec{
            .sampleRate = kOutputRate,
            .channels = 2,
            .chunkFrames = kChunkFrames,
    });
}

VisualizerAudioTap::~VisualizerAudioTap() = default;

void VisualizerAudioTap::setSource(const fs::path& path) {
    const std::string next = path.string();
    if (next == source_) return;
    clearSource();
    source_ = next;
}

void VisualizerAudioTap::clearSource() {
    if (decoder_) decoder_->close();
    source_.clear();
    framesPushed_ = 0;
    ended_ = false;
    failed_ = false;
}

bool VisualizerAudioTap::isActive() const noexcept {
    return !source_.empty() && !ended_ && !failed_;
}

void VisualizerAudioTap::pump(const qint64 positionMs) {
    if (!isActive() || positionMs < 0) return;

    if (!decoder_->isOpen()) {
        if (const auto opened = decoder_->open(fs::path(source_)); !opened) {
            // One line per track, not per tick: a file the decoder refuses will
            // be refused 25 times a second otherwise.
            failed_ = true;
            LOG_WARN("VisualizerAudioTap: cannot decode {} for the visualizer: {}",
                     source_, opened.error().describe());
            return;
        }
        if (!announced_) {
            announced_ = true;
            LOG_INFO("VisualizerAudioTap: the media backend delivers no audio buffers, so {} is "
                     "decoded for projectM",
                     source_);
        }
    }

    const u32 rate = decoder_->sampleRate();
    const qint64 target = positionMs * static_cast<qint64>(rate) / 1000;
    const qint64 lead = static_cast<qint64>(rate) * kLeadMs / 1000;
    const qint64 realign = static_cast<qint64>(rate) * kRealignMs / 1000;
    const qint64 tapPosition = static_cast<qint64>(framesPushed_);

    if (std::llabs(target - tapPosition) > realign) {
        if (const auto sought = decoder_->seekTo(static_cast<f64>(target) / rate); !sought) {
            failed_ = true;
            LOG_WARN("VisualizerAudioTap: cannot seek {} for the visualizer: {}", source_,
                     sought.error().describe());
            return;
        }
        framesPushed_ = static_cast<u64>(std::max<qint64>(target, 0));
    }

    for (int i = 0; i < kMaxChunksPerTick; ++i) {
        if (static_cast<qint64>(framesPushed_) >= target + lead) break;

        PcmChunk chunk;
        const auto next = decoder_->nextChunk(chunk);
        if (!next) {
            failed_ = true;
            LOG_WARN("VisualizerAudioTap: decode of {} stopped: {}", source_,
                     next.error().describe());
            return;
        }
        if (*next == ChunkStatus::EndOfStream) {
            ended_ = true;
            return;
        }
        if (chunk.frameCount() == 0) continue;

        // Refusals are expected exactly when the renderer is not draining (a
        // hidden window). The push is still correct -- the queue keeps the audio
        // it had room for -- and the next tick re-aligns on the player's clock,
        // so this is not an error worth logging 25 times a second.
        static_cast<void>(queue_.pushAll(chunk.samples.data(), chunk.frameCount(), chunk.channels,
                                         chunk.sampleRate));
        framesPushed_ += chunk.frameCount();
    }
}

} // namespace vc
