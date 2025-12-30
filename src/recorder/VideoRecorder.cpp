#include "VideoRecorder.hpp"
#include "core/Config.hpp"
#include "core/Logger.hpp"
#include "util/FileUtils.hpp"

#include <chrono>

namespace vc {

VideoRecorder::VideoRecorder() = default;

VideoRecorder::~VideoRecorder() {
    if (isRecording()) {
        stop();
    }
}

Result<void> VideoRecorder::start(const EncoderSettings& settings) {
    if (state_ != RecordingState::Stopped) {
        return Result<void>::err("Recording already in progress");
    }
    
    // Validate settings
    if (auto result = settings.validate(); !result) {
        return result;
    }
    
    settings_ = settings;
    
    // Ensure output directory exists
    file::ensureDir(settings_.outputPath.parent_path());
    
    // Initialize FFmpeg
    state_ = RecordingState::Starting;
    stateChanged.emitSignal(state_);
    
    if (auto result = initFFmpeg(); !result) {
        cleanupFFmpeg();
        state_ = RecordingState::Error;
        stateChanged.emitSignal(state_);
        return result;
    }
    
    // Reset stats
    stats_ = RecordingStats{};
    stats_.currentFile = settings_.outputPath.string();
    
    // Start encoding thread
    shouldStop_ = false;
    frameGrabber_.setSize(settings_.video.width, settings_.video.height);
    frameGrabber_.start();
    
    startTime_ = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    
    encodingThread_ = std::thread(&VideoRecorder::encodingThread, this);
    
    state_ = RecordingState::Recording;
    stateChanged.emitSignal(state_);
    
    LOG_INFO("Recording started: {}", settings_.outputPath.string());
    return Result<void>::ok();
}

Result<void> VideoRecorder::start(const fs::path& outputPath) {
    auto settings = EncoderSettings::fromConfig();
    settings.outputPath = outputPath;
    return start(settings);
}

Result<void> VideoRecorder::stop() {
    if (state_ != RecordingState::Recording) {
        return Result<void>::ok();
    }
    
    state_ = RecordingState::Stopping;
    stateChanged.emitSignal(state_);
    
    // Signal thread to stop
    shouldStop_ = true;
    frameGrabber_.stop();
    
    // Wait for encoding thread
    if (encodingThread_.joinable()) {
        encodingThread_.join();
    }
    
    // Flush encoders and finalize file
    {
        std::lock_guard lock(ffmpegMutex_);
        flushEncoders();
        
        // Write trailer
        if (formatCtx_) {
            av_write_trailer(formatCtx_.get());
        }
    }
    
    cleanupFFmpeg();
    
    state_ = RecordingState::Stopped;
    stateChanged.emitSignal(state_);
    
    LOG_INFO("Recording stopped. Frames: {}, Dropped: {}", 
             stats_.framesWritten, stats_.framesDropped);
    
    return Result<void>::ok();
}

void VideoRecorder::submitVideoFrame(const u8* data, u32 width, u32 height, i64 timestamp) {
    if (state_ != RecordingState::Recording) return;
    
    GrabbedFrame frame;
    frame.width = width;
    frame.height = height;
    frame.timestamp = timestamp;
    frame.data.assign(data, data + width * height * 4);
    
    processVideoFrame(frame);
}

void VideoRecorder::submitAudioSamples(const f32* data, u32 samples, u32 channels, u32 sampleRate) {
    if (state_ != RecordingState::Recording) return;
    if (!audioStream_) return;
    
    std::lock_guard lock(audioMutex_);
    audioSampleRate_ = sampleRate;
    audioChannels_ = channels;
    
    usize size = samples * channels;
    audioBuffer_.insert(audioBuffer_.end(), data, data + size);
}

void VideoRecorder::encodingThread() {
    LOG_DEBUG("Encoding thread started");
    
    auto lastStatsUpdate = std::chrono::steady_clock::now();
    
    while (!shouldStop_) {
        GrabbedFrame frame;
        if (frameGrabber_.getNextFrame(frame, 16)) {
            processVideoFrame(frame);
        }
        
        processAudioBuffer();
        
        auto now = std::chrono::steady_clock::now();
        if (now - lastStatsUpdate >= std::chrono::seconds(1)) {
            stats_.elapsed = Duration(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - std::chrono::steady_clock::time_point(
                        std::chrono::microseconds(startTime_))).count());
            
            if (stats_.elapsed.count() > 0) {
                stats_.avgFps = static_cast<f64>(stats_.framesWritten) * 1000.0 / stats_.elapsed.count();
            }
            
            stats_.framesDropped = frameGrabber_.droppedFrames();
            statsUpdated.emitSignal(stats_);
            lastStatsUpdate = now;
        }
    }
    
    LOG_DEBUG("Encoding thread stopped");
}

void VideoRecorder::processVideoFrame(const GrabbedFrame& frame) {
    std::lock_guard lock(ffmpegMutex_);
    
    if (!videoCodecCtx_ || !videoFrame_) return;
    
    const u8* srcData[1] = { frame.data.data() };
    int srcLinesize[1] = { static_cast<int>(frame.width * 4) };
    
    sws_scale(swsCtx_.get(), srcData, srcLinesize, 0, frame.height,
              videoFrame_->data, videoFrame_->linesize);
    
    videoFrame_->pts = videoFrameCount_++;
    
    if (encodeVideoFrame(videoFrame_.get())) {
        ++stats_.framesWritten;
    }
}

void VideoRecorder::processAudioBuffer() {
    std::lock_guard lock(audioMutex_);
    
    if (!audioCodecCtx_ || !audioFrame_ || audioBuffer_.empty()) return;
    
    int frameSize = audioCodecCtx_->frame_size;
    int channels = audioChannels_;
    
    while (audioBuffer_.size() >= static_cast<usize>(frameSize * channels)) {
        std::vector<f32> samples(audioBuffer_.begin(), 
                                  audioBuffer_.begin() + frameSize * channels);
        audioBuffer_.erase(audioBuffer_.begin(), 
                           audioBuffer_.begin() + frameSize * channels);
        
        const u8* srcData[1] = { reinterpret_cast<const u8*>(samples.data()) };
        
        int ret = swr_convert(swrCtx_.get(), audioFrame_->data, frameSize,
                              srcData, frameSize);
        if (ret < 0) {
            LOG_WARN("Audio resample error: {}", ffmpegError(ret));
            continue;
        }
        
        audioFrame_->pts = audioFrameCount_;
        audioFrameCount_ += frameSize;
        
        {
            std::lock_guard lockFFmpeg(ffmpegMutex_);
            encodeAudioFrame(audioFrame_.get());
        }
    }
}

Result<void> VideoRecorder::initFFmpeg() {
    int ret;
    
    AVFormatContext* ctx = nullptr;
    ret = avformat_alloc_output_context2(&ctx, nullptr, nullptr, 
                                          settings_.outputPath.c_str());
    formatCtx_.reset(ctx);
    
    if (ret < 0 || !formatCtx_) {
        return Result<void>::err("Failed to create output context: " + ffmpegError(ret));
    }
    
    if (auto result = initVideoStream(); !result) {
        return result;
    }
    
    if (auto result = initAudioStream(); !result) {
        return result;
    }
    
    if (!(formatCtx_->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&formatCtx_->pb, settings_.outputPath.c_str(), AVIO_FLAG_WRITE);
        if (ret < 0) {
            return Result<void>::err("Failed to open output file: " + ffmpegError(ret));
        }
    }
    
    AVDictionary* opts = nullptr;
    ret = avformat_write_header(formatCtx_.get(), &opts);
    av_dict_free(&opts);
    
    if (ret < 0) {
        return Result<void>::err("Failed to write header: " + ffmpegError(ret));
    }
    
    packet_.reset(av_packet_alloc());
    if (!packet_) {
        return Result<void>::err("Failed to allocate packet");
    }
    
    LOG_DEBUG("FFmpeg initialized successfully");
    return Result<void>::ok();
}

Result<void> VideoRecorder::initVideoStream() {
    const AVCodec* codec = avcodec_find_encoder_by_name(settings_.video.codecName().c_str());
    if (!codec) {
        return Result<void>::err("Video codec not found: " + settings_.video.codecName());
    }
    
    videoStream_ = avformat_new_stream(formatCtx_.get(), nullptr);
    if (!videoStream_) {
        return Result<void>::err("Failed to create video stream");
    }
    
    videoCodecCtx_.reset(avcodec_alloc_context3(codec));
    if (!videoCodecCtx_) {
        return Result<void>::err("Failed to allocate video codec context");
    }
    
    videoCodecCtx_->width = settings_.video.width;
    videoCodecCtx_->height = settings_.video.height;
    videoCodecCtx_->time_base = AVRational{1, static_cast<int>(settings_.video.fps)};
    videoCodecCtx_->framerate = AVRational{static_cast<int>(settings_.video.fps), 1};
    videoCodecCtx_->pix_fmt = AV_PIX_FMT_YUV420P;
    videoCodecCtx_->gop_size = settings_.video.gopSize > 0 ? 
                               settings_.video.gopSize : settings_.video.fps * 2;
    videoCodecCtx_->max_b_frames = settings_.video.bFrames;
    
    if (formatCtx_->oformat->flags & AVFMT_GLOBALHEADER) {
        videoCodecCtx_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }
    
    AVDictionary* opts = nullptr;
    if (settings_.video.codec == VideoCodec::H264 || 
        settings_.video.codec == VideoCodec::H265) {
        av_dict_set(&opts, "preset", settings_.video.presetName().c_str(), 0);
        av_dict_set(&opts, "crf", std::to_string(settings_.video.crf).c_str(), 0);
        av_dict_set(&opts, "tune", "zerolatency", 0);
    }
    
    int ret = avcodec_open2(videoCodecCtx_.get(), codec, &opts);
    av_dict_free(&opts);
    
    if (ret < 0) {
        return Result<void>::err("Failed to open video codec: " + ffmpegError(ret));
    }
    
    ret = avcodec_parameters_from_context(videoStream_->codecpar, videoCodecCtx_.get());
    if (ret < 0) {
        return Result<void>::err("Failed to copy video codec params");
    }
    
    videoStream_->time_base = videoCodecCtx_->time_base;
    
    videoFrame_.reset(av_frame_alloc());
    if (!videoFrame_) {
        return Result<void>::err("Failed to allocate video frame");
    }
    
    videoFrame_->format = videoCodecCtx_->pix_fmt;
    videoFrame_->width = videoCodecCtx_->width;
    videoFrame_->height = videoCodecCtx_->height;
    
    ret = av_frame_get_buffer(videoFrame_.get(), 0);
    if (ret < 0) {
        return Result<void>::err("Failed to allocate video frame buffer");
    }
    
    swsCtx_.reset(sws_getContext(
        settings_.video.width, settings_.video.height, AV_PIX_FMT_RGBA,
        settings_.video.width, settings_.video.height, AV_PIX_FMT_YUV420P,
        SWS_BILINEAR, nullptr, nullptr, nullptr));
    
    if (!swsCtx_) {
        return Result<void>::err("Failed to create swscale context");
    }
    
    LOG_DEBUG("Video stream initialized: {}x{} @ {} fps, codec: {}",
              settings_.video.width, settings_.video.height,
              settings_.video.fps, settings_.video.codecName());
    
    return Result<void>::ok();
}

Result<void> VideoRecorder::initAudioStream() {
    const AVCodec* codec = avcodec_find_encoder_by_name(settings_.audio.codecName().c_str());
    if (!codec) {
        LOG_WARN("Audio codec not found: {}, skipping audio", settings_.audio.codecName());
        return Result<void>::ok();
    }
    
    audioStream_ = avformat_new_stream(formatCtx_.get(), nullptr);
    if (!audioStream_) {
        return Result<void>::err("Failed to create audio stream");
    }
    
    audioCodecCtx_.reset(avcodec_alloc_context3(codec));
    if (!audioCodecCtx_) {
        return Result<void>::err("Failed to allocate audio codec context");
    }
    
    audioCodecCtx_->sample_rate = settings_.audio.sampleRate;
    audioCodecCtx_->bit_rate = settings_.audio.bitrate * 1000;
    
    AVChannelLayout layout;
    av_channel_layout_default(&layout, settings_.audio.channels);
    av_channel_layout_copy(&audioCodecCtx_->ch_layout, &layout);
    
    audioCodecCtx_->sample_fmt = codec->sample_fmts ? codec->sample_fmts[0] : AV_SAMPLE_FMT_FLTP;
    audioCodecCtx_->time_base = AVRational{1, static_cast<int>(settings_.audio.sampleRate)};
    
    if (formatCtx_->oformat->flags & AVFMT_GLOBALHEADER) {
        audioCodecCtx_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }
    
    int ret = avcodec_open2(audioCodecCtx_.get(), codec, nullptr);
    if (ret < 0) {
        return Result<void>::err("Failed to open audio codec: " + ffmpegError(ret));
    }
    
    ret = avcodec_parameters_from_context(audioStream_->codecpar, audioCodecCtx_.get());
    if (ret < 0) {
        return Result<void>::err("Failed to copy audio codec params");
    }
    
    audioStream_->time_base = audioCodecCtx_->time_base;
    
    audioFrame_.reset(av_frame_alloc());
    if (!audioFrame_) {
        return Result<void>::err("Failed to allocate audio frame");
    }
    
    audioFrame_->format = audioCodecCtx_->sample_fmt;
    av_channel_layout_copy(&audioFrame_->ch_layout, &audioCodecCtx_->ch_layout);
    audioFrame_->sample_rate = audioCodecCtx_->sample_rate;
    audioFrame_->nb_samples = audioCodecCtx_->frame_size;
    
    if (audioFrame_->nb_samples > 0) {
        ret = av_frame_get_buffer(audioFrame_.get(), 0);
        if (ret < 0) {
            return Result<void>::err("Failed to allocate audio frame buffer");
        }
    }
    
    SwrContext* s = nullptr;
    ret = swr_alloc_set_opts2(&s,
        &audioCodecCtx_->ch_layout, audioCodecCtx_->sample_fmt, audioCodecCtx_->sample_rate,
        &layout, AV_SAMPLE_FMT_FLT, settings_.audio.sampleRate,
        0, nullptr);
    swrCtx_.reset(s);
    
    if (ret < 0 || !swrCtx_) {
        return Result<void>::err("Failed to create swresample context");
    }
    
    ret = swr_init(swrCtx_.get());
    if (ret < 0) {
        return Result<void>::err("Failed to init swresample: " + ffmpegError(ret));
    }
    
    LOG_DEBUG("Audio stream initialized: {} Hz, {} ch, codec: {}",
              settings_.audio.sampleRate, settings_.audio.channels,
              settings_.audio.codecName());
    
    return Result<void>::ok();
}

void VideoRecorder::cleanupFFmpeg() {
    std::lock_guard lock(ffmpegMutex_);
    
    packet_.reset();
    videoFrame_.reset();
    audioFrame_.reset();
    swsCtx_.reset();
    swrCtx_.reset();
    videoCodecCtx_.reset();
    audioCodecCtx_.reset();
    formatCtx_.reset();
    
    videoStream_ = nullptr;
    audioStream_ = nullptr;
    videoFrameCount_ = 0;
    audioFrameCount_ = 0;
}

bool VideoRecorder::encodeVideoFrame(AVFrame* frame) {
    int ret = avcodec_send_frame(videoCodecCtx_.get(), frame);
    if (ret < 0) {
        std::string errMsg = "Error sending video frame: " + ffmpegError(ret);
        LOG_WARN("{}", errMsg);
        error.emitSignal(errMsg);
        return false;
    }
    
    while (ret >= 0) {
        ret = avcodec_receive_packet(videoCodecCtx_.get(), packet_.get());
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        }
        if (ret < 0) {
            std::string errMsg = "Error receiving video packet: " + ffmpegError(ret);
            LOG_WARN("{}", errMsg);
            error.emitSignal(errMsg);
            return false;
        }
        
        if (!writePacket(packet_.get(), videoStream_)) {
            return false;
        }
    }
    
    return true;
}

bool VideoRecorder::encodeAudioFrame(AVFrame* frame) {
    int ret = avcodec_send_frame(audioCodecCtx_.get(), frame);
    if (ret < 0) {
        std::string errMsg = "Error sending audio frame: " + ffmpegError(ret);
        LOG_WARN("{}", errMsg);
        error.emitSignal(errMsg);
        return false;
    }
    
    while (ret >= 0) {
        ret = avcodec_receive_packet(audioCodecCtx_.get(), packet_.get());
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        }
        if (ret < 0) {
            std::string errMsg = "Error receiving audio packet: " + ffmpegError(ret);
            LOG_WARN("{}", errMsg);
            error.emitSignal(errMsg);
            return false;
        }
        
        if (!writePacket(packet_.get(), audioStream_)) {
            return false;
        }
    }
    
    return true;
}

bool VideoRecorder::writePacket(AVPacket* packet, AVStream* stream) {
    av_packet_rescale_ts(packet, 
        stream == videoStream_ ? videoCodecCtx_->time_base : audioCodecCtx_->time_base,
        stream->time_base);
    
    packet->stream_index = stream->index;
    
    int ret = av_interleaved_write_frame(formatCtx_.get(), packet);
    if (ret < 0) {
        LOG_WARN("Error writing packet: {}", ffmpegError(ret));
        return false;
    }
    
    stats_.bytesWritten += packet->size;
    return true;
}

void VideoRecorder::flushEncoders() {
    if (videoCodecCtx_) {
        avcodec_send_frame(videoCodecCtx_.get(), nullptr);
        
        int ret;
        while ((ret = avcodec_receive_packet(videoCodecCtx_.get(), packet_.get())) >= 0) {
            writePacket(packet_.get(), videoStream_);
        }
    }
    
    if (audioCodecCtx_) {
        avcodec_send_frame(audioCodecCtx_.get(), nullptr);
        
        int ret;
        while ((ret = avcodec_receive_packet(audioCodecCtx_.get(), packet_.get())) >= 0) {
            writePacket(packet_.get(), audioStream_);
        }
    }
}

} // namespace vc
