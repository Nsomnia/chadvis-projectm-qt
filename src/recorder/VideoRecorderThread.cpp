// Version: 1.1.0
// Last Edited: 2026-03-29 12:00:00
// Description: Video recorder thread implementation with lock-free queue

#include "VideoRecorderThread.hpp"
#include "audio/AudioQueue.hpp"
#include "core/Logger.hpp"

namespace vc {

VideoRecorderThread::VideoRecorderThread(VideoRecorder& parent,
                                         const EncoderSettings& settings)
    : parent_(parent), settings_(settings) {}

VideoRecorderThread::~VideoRecorderThread() {
    stop();
}

Result<void> VideoRecorderThread::start(std::string_view assSubtitle) {
    frameGrabber_.start();
    // Synchronous, on the caller's thread, and the borrowed view dies with this
    // statement. Everything the encoding thread will later read about the
    // subtitle track is ffmpeg_'s own copy, written here and published by the
    // JThread construction further down.
    if (auto res = ffmpeg_.init(settings_, assSubtitle); !res) {
        LOG_ERROR("Failed to initialize FFmpeg: {}", res.error().message);
        // Returned *and* emitted. The return is what lets VideoRecorder::start
        // refuse to enter Recording -- the emit alone left the caller with a
        // success it had to disbelieve, which is how an unwritable output
        // directory produced a live timer, no file and no error.
        parent_.error.emitSignal(res.error().message);
        return res;
    }
    
    actualOutputPath_ = ffmpeg_.getOutputPath();
    
    // Reset stats atomically
    {
        std::lock_guard<std::mutex> lock(statsMutex_);
        stats_ = RecordingStats{};
        stats_.currentFile = actualOutputPath_;
    }
    
    // The capture is `this` and nothing else. A string_view must never appear
    // here: this lambda outlives the caller's buffer by construction, and the
    // document has already been copied into ffmpeg_ anyway.
    thread_ = JThread([this](StopToken st) { threadLoop(st); });
    LOG_INFO("Recording thread started: {}", settings_.outputPath.string());
    return Result<void>::ok();
}

void VideoRecorderThread::stop() {
    shouldStop_ = true;
    frameGrabber_.stop();
    thread_.request_stop();
    if (thread_.joinable())
        thread_.join();

    // Final flush
    u64 bytes = 0;
    ffmpeg_.flush(bytes);

    // Read the encoder's verdict *before* cleanup(), which resets the counters so
    // the instance can be reused. These two facts are what decide whether
    // VideoRecorder::stop reports a finished recording, a damaged one, or a
    // finished one with a degraded audio track. Snapshotted once, and used for
    // both the stats and the log below, so there is a single answer to "what did
    // this recording cost" rather than two reads of a counter that is about to
    // be zeroed.
    writeFailed_ = ffmpeg_.writeFailed();
    audioResample_ = ffmpeg_.audioResampleReport();

    // Update final stats
    {
        std::lock_guard<std::mutex> lock(statsMutex_);
        stats_.bytesWritten += bytes;
        // Final value of a counter that updateStats() has been publishing live
        // every second for the whole recording. It used to be assigned *only*
        // here, which made it unreadable for the duration: a bridge polling it
        // saw a zero that could not be told apart from "nothing dropped yet",
        // and then a jump to N at teardown.
        stats_.audioFramesDropped = audioResample_.droppedFrames;
    }

    ffmpeg_.cleanup();
    if (writeFailed_) {
        LOG_ERROR("Recording stopped with an incomplete file: the muxer refused "
                  "at least one packet");
    } else if (audioResample_.droppedFrames > 0) {
        // Not LOG_ERROR: the file is complete and decodes. This is a degraded
        // track, and saying so in the log is the point -- the old code said it
        // once per frame and never said how many.
        LOG_WARN("Recording stopped with degraded audio: {} frames were left "
                 "out of the audio track", audioResample_.droppedFrames);
    } else {
        LOG_INFO("Recording thread stopped");
    }
}

void VideoRecorderThread::pushVideoFrame(GrabbedFrame frame) {
    frameGrabber_.pushFrame(std::move(frame));
}

RecordingStats VideoRecorderThread::getStats() const {
    std::lock_guard<std::mutex> lock(statsMutex_);
    return stats_;
}

void VideoRecorderThread::threadLoop(StopToken stopToken) {
    LOG_DEBUG("Encoding thread started");
    auto startTime = std::chrono::steady_clock::now();
    auto lastStatsUpdate = startTime;
    u64 lastFramesWritten = 0;
    
    while (!stopToken.stop_requested()) {
        GrabbedFrame frame;
        bool hasVideo = frameGrabber_.getNextFrame(frame, 10);

        // A damaging write failure is terminal for this recording, and the one
        // place that decides it is here rather than inside the encoder: the
        // encoder has no authority over the loop, and stopping *producing* is
        // different from trying to undo what was written. The file cannot be
        // repaired -- the accepted packets are interleaved with holes, so it
        // opens and plays to the break and then stops, which reads as a good
        // recording -- so the response is to stop lengthening it and say so
        // exactly once. Report the reason, not the generic encoder message.
        if (ffmpeg_.reportWriteFailure()) {
            parent_.error.emitSignal(
                "Recording failed while writing to disk -- the file is "
                "incomplete (the most common cause is a full volume)");
            break;
        }

        u64 bytesWritten = 0;
        bool hadError = false;

        if (hasVideo) {
            if (ffmpeg_.encodeVideo(frame, bytesWritten)) {
                std::lock_guard<std::mutex> lock(statsMutex_);
                ++stats_.framesWritten;
                stats_.bytesWritten += bytesWritten;
            } else {
                hadError = true;
                LOG_WARN("Failed to encode video frame");
        }
    }

    // Pop audio from lock-free queue (no mutex).  Load the pointer once so an
    // attach-before/after-worker transition is race-free for this iteration.
    if (AudioQueue* queue = audioQueue_.load(std::memory_order_acquire)) {
        static constexpr usize AUDIO_BATCH_SIZE = 4096;
        alignas(64) float audioBatch[AUDIO_BATCH_SIZE * 2];
        u32 popped = queue->popRecBatch(audioBatch, AUDIO_BATCH_SIZE);
        if (popped > 0) {
            std::vector<f32> audioBuffer(audioBatch, audioBatch + popped * 2);
            // Only the encoder itself is an error here. A batch whose every frame
            // was refused by the resampler produced no audio and nothing wrong
            // with the encoder, and the drop path already reports it -- once, in
            // words that say the file is fine -- at stop(). Reporting it here as
            // well told the user the recording had broken up to ~100 times a
            // second, via RecordingBridge, and then contradicted it at the end.
            const auto outcome =
                ffmpeg_.encodeAudio(audioBuffer, 2, bytesWritten);
            if (outcome == VideoRecorderFFmpeg::AudioEncodeOutcome::Failed) {
                hadError = true;
                LOG_WARN("Failed to encode audio samples");
            } else {
                std::lock_guard<std::mutex> statsLock(statsMutex_);
                stats_.bytesWritten += bytesWritten;
            }
        }
    }

    // Update dropped frames count
        {
            std::lock_guard<std::mutex> lock(statsMutex_);
            stats_.framesDropped = frameGrabber_.droppedFrames();
        }

        // Stop condition: requested stop AND queues empty
        if (shouldStop_ && !hasVideo && !frameGrabber_.hasFrames()) {
            break;
        }

        // Stats update every second
        auto now = std::chrono::steady_clock::now();
        if (now - lastStatsUpdate >= std::chrono::seconds(1)) {
            updateStats(startTime, lastFramesWritten, lastStatsUpdate);
            lastStatsUpdate = now;
        }
        
        // Emit error signal if there were encoding issues
        if (hadError) {
            parent_.error.emitSignal("Encoding error occurred - check logs");
        }
    }
    
    // A failure that landed on the very last iteration, after the top-of-loop
    // check, still has to be reported -- otherwise the loop exits silently and
    // stop() is the only thing that learns of it, by which point the user has
    // already watched the counter climb.
    if (ffmpeg_.reportWriteFailure()) {
        parent_.error.emitSignal(
            "Recording failed while writing to disk -- the file is "
            "incomplete (the most common cause is a full volume)");
    }

    // Final stats update
    updateStats(startTime, lastFramesWritten, lastStatsUpdate, true);
    
    LOG_DEBUG("Encoding thread finishing");
}

void VideoRecorderThread::updateStats(TimePoint startTime,
                                      u64& lastFramesWritten,
                                      TimePoint& lastUpdate,
                                      bool isFinal) {
    auto now = std::chrono::steady_clock::now();
    
    std::lock_guard<std::mutex> lock(statsMutex_);
    
    // Read here rather than only at stop(), for the same reason framesDropped is
    // read on this thread: the whole point of a counter is that a reader can watch
    // it move. Every caller of updateStats is threadLoop -- the once-a-second pass
    // and the final one -- so this runs on the encoding thread, the only writer of
    // the counters it reads, and the atomic load is the one that needs the care.
    // framesDropped is published right below, so a reader sees both or neither.
    stats_.audioFramesDropped = ffmpeg_.audioResampleReport().droppedFrames;
    
    // Calculate elapsed time
    stats_.elapsed = std::chrono::duration_cast<Duration>(now - startTime);
    
    // Calculate FPS over the last interval
    auto intervalSecs = std::chrono::duration<f64>(now - lastUpdate).count();
    if (intervalSecs > 0) {
        u64 framesDelta = stats_.framesWritten - lastFramesWritten;
        stats_.avgFps = static_cast<f64>(framesDelta) / intervalSecs;
        
        // Also calculate overall encoding FPS
        auto totalSecs = std::chrono::duration<f64>(stats_.elapsed).count();
        if (totalSecs > 0) {
            stats_.encodingFps = static_cast<f64>(stats_.framesWritten) / totalSecs;
        }
    }
    
    lastFramesWritten = stats_.framesWritten;
    
    // Emit stats update signal (thread-safe via Qt queued connection in UI)
    if (!isFinal || stats_.framesWritten > 0) {
        parent_.statsUpdated.emitSignal(stats_);
    }
}

} // namespace vc
