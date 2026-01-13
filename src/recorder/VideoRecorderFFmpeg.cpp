#include "VideoRecorderFFmpeg.hpp"
#include <libavcodec/version.h>
#include "core/Logger.hpp"

#if LIBAVCODEC_VERSION_MAJOR >= 60
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

namespace vc {

VideoRecorderFFmpeg::VideoRecorderFFmpeg() = default;

VideoRecorderFFmpeg::~VideoRecorderFFmpeg() {
    cleanup();
}

Result<void> VideoRecorderFFmpeg::init(const EncoderSettings& settings) {
    int ret;

    AVFormatContext* ctx = nullptr;
    ret = avformat_alloc_output_context2(
            &ctx, nullptr, nullptr, settings.outputPath.c_str());
    formatCtx_.reset(ctx);

    if (ret < 0 || !formatCtx_) {
        return Result<void>::err("Failed to create output context: " +
                                 ffmpegError(ret));
    }

    if (auto result = initVideoStream(settings); !result) {
        return result;
    }

    if (auto result = initAudioStream(settings); !result) {
        return result;
    }

    if (!(formatCtx_->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(
                &formatCtx_->pb, settings.outputPath.c_str(), AVIO_FLAG_WRITE);
        if (ret < 0) {
            return Result<void>::err("Failed to open output file: " +
                                     ffmpegError(ret));
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

void VideoRecorderFFmpeg::cleanup() {
    std::lock_guard lock(mutex_);

    packet_.reset();
    videoFrame_.reset();
    audioFrame_.reset();
    swsCtx_.reset();
    swrCtx_.reset();
    videoCodecCtx_.reset();
    audioCodecCtx_.reset();

    if (formatCtx_ && formatCtx_->pb) {
        av_write_trailer(formatCtx_.get());
    }
    formatCtx_.reset();

    videoStream_ = nullptr;
    audioStream_ = nullptr;
    videoFrameCount_ = 0;
    audioFrameCount_ = 0;
}

bool VideoRecorderFFmpeg::encodeVideo(const GrabbedFrame& frame,
                                      u64& bytesWritten) {
    if (frame.data.empty())
        return false;

    std::lock_guard lock(mutex_);
    if (!videoCodecCtx_ || !videoFrame_)
        return false;

    const u8* srcData[1] = {frame.data.data()};
    int srcLinesize[1] = {static_cast<int>(frame.width * 4)};

    // Re-create SwsContext if needed (resizing handling could go here)
    if (!swsCtx_) {
        swsCtx_.reset(sws_getContext(frame.width,
                                     frame.height,
                                     AV_PIX_FMT_RGBA,
                                     videoCodecCtx_->width,
                                     videoCodecCtx_->height,
                                     videoCodecCtx_->pix_fmt,
                                     SWS_BILINEAR,
                                     nullptr,
                                     nullptr,
                                     nullptr));
    }

    sws_scale(swsCtx_.get(),
              srcData,
              srcLinesize,
              0,
              frame.height,
              videoFrame_->data,
              videoFrame_->linesize);

    videoFrame_->pts = videoFrameCount_++;

    return encodeVideoFrame(videoFrame_.get(), bytesWritten);
}

bool VideoRecorderFFmpeg::encodeAudio(std::vector<f32>& buffer,
                                      u32 channels,
                                      u64& bytesWritten) {
    std::lock_guard lock(mutex_);
    if (!audioCodecCtx_ || !audioFrame_ || buffer.empty())
        return false;

    int frameSize = audioCodecCtx_->frame_size;
    if (frameSize <= 0)
        return false;

    bool encodedAny = false;

    while (buffer.size() >= static_cast<usize>(frameSize * channels)) {
        std::vector<f32> samples(buffer.begin(),
                                 buffer.begin() + frameSize * channels);
        buffer.erase(buffer.begin(), buffer.begin() + frameSize * channels);

        const u8* srcData[1] = {reinterpret_cast<const u8*>(samples.data())};

        int ret = swr_convert(swrCtx_.get(),
                              audioFrame_->data,
                              frameSize,
                              srcData,
                              frameSize);
        if (ret < 0) {
            LOG_WARN("Audio resample error: {}", ffmpegError(ret));
            continue;
        }

        audioFrame_->pts = audioFrameCount_;
        audioFrameCount_ += frameSize;

        if (encodeAudioFrame(audioFrame_.get(), bytesWritten)) {
            encodedAny = true;
        }
    }
    return encodedAny;
}

void VideoRecorderFFmpeg::flush(u64& bytesWritten) {
    std::lock_guard lock(mutex_);

    if (videoCodecCtx_) {
        avcodec_send_frame(videoCodecCtx_.get(), nullptr);
        while (true) {
            int ret =
                    avcodec_receive_packet(videoCodecCtx_.get(), packet_.get());
            if (ret < 0)
                break;
            writePacket(packet_.get(), videoStream_, bytesWritten);
        }
    }

    if (audioCodecCtx_) {
        avcodec_send_frame(audioCodecCtx_.get(), nullptr);
        while (true) {
            int ret =
                    avcodec_receive_packet(audioCodecCtx_.get(), packet_.get());
            if (ret < 0)
                break;
            writePacket(packet_.get(), audioStream_, bytesWritten);
        }
    }
}

Result<void> VideoRecorderFFmpeg::initVideoStream(
        const EncoderSettings& settings) {
    const AVCodec* codec =
            avcodec_find_encoder_by_name(settings.video.codecName().c_str());
    if (!codec) {
        return Result<void>::err("Video codec not found: " +
                                 settings.video.codecName());
    }

    videoStream_ = avformat_new_stream(formatCtx_.get(), nullptr);
    if (!videoStream_)
        return Result<void>::err("Failed to create video stream");

    videoCodecCtx_.reset(avcodec_alloc_context3(codec));
    if (!videoCodecCtx_)
        return Result<void>::err("Failed to allocate video codec context");

    videoCodecCtx_->width = settings.video.width;
    videoCodecCtx_->height = settings.video.height;
    videoCodecCtx_->time_base =
            AVRational{1, static_cast<int>(settings.video.fps)};
    videoCodecCtx_->framerate =
            AVRational{static_cast<int>(settings.video.fps), 1};
    videoCodecCtx_->pix_fmt = AV_PIX_FMT_YUV420P;
    videoCodecCtx_->gop_size = settings.video.gopSize > 0
                                       ? settings.video.gopSize
                                       : settings.video.fps * 2;
    videoCodecCtx_->max_b_frames = settings.video.bFrames;

    if (formatCtx_->oformat->flags & AVFMT_GLOBALHEADER) {
        videoCodecCtx_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    AVDictionary* opts = nullptr;
    if (settings.video.codec == VideoCodec::H264 ||
        settings.video.codec == VideoCodec::H265) {
        av_dict_set(&opts, "preset", settings.video.presetName().c_str(), 0);
        av_dict_set(
                &opts, "crf", std::to_string(settings.video.crf).c_str(), 0);
        av_dict_set(&opts, "tune", "zerolatency", 0);
    }

    int ret = avcodec_open2(videoCodecCtx_.get(), codec, &opts);
    av_dict_free(&opts);

    if (ret < 0) {
        return Result<void>::err("Failed to open video codec: " +
                                 ffmpegError(ret));
    }

    avcodec_parameters_from_context(videoStream_->codecpar,
                                    videoCodecCtx_.get());
    videoStream_->time_base = videoCodecCtx_->time_base;

    videoFrame_.reset(av_frame_alloc());
    videoFrame_->format = videoCodecCtx_->pix_fmt;
    videoFrame_->width = videoCodecCtx_->width;
    videoFrame_->height = videoCodecCtx_->height;
    av_frame_get_buffer(videoFrame_.get(), 0);

    return Result<void>::ok();
}

Result<void> VideoRecorderFFmpeg::initAudioStream(
        const EncoderSettings& settings) {
    const AVCodec* codec =
            avcodec_find_encoder_by_name(settings.audio.codecName().c_str());
    if (!codec) {
        LOG_WARN("Audio codec not found, skipping audio");
        return Result<void>::ok();
    }

    audioStream_ = avformat_new_stream(formatCtx_.get(), nullptr);
    if (!audioStream_)
        return Result<void>::err("Failed to create audio stream");

    audioCodecCtx_.reset(avcodec_alloc_context3(codec));

    audioCodecCtx_->sample_rate = settings.audio.sampleRate;
    audioCodecCtx_->bit_rate = settings.audio.bitrate * 1000;

    AVChannelLayout layout;
    av_channel_layout_default(&layout, settings.audio.channels);
    av_channel_layout_copy(&audioCodecCtx_->ch_layout, &layout);

    audioCodecCtx_->sample_fmt =
            codec->sample_fmts ? codec->sample_fmts[0] : AV_SAMPLE_FMT_FLTP;
    audioCodecCtx_->time_base =
            AVRational{1, static_cast<int>(settings.audio.sampleRate)};

    if (formatCtx_->oformat->flags & AVFMT_GLOBALHEADER) {
        audioCodecCtx_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    if (avcodec_open2(audioCodecCtx_.get(), codec, nullptr) < 0) {
        return Result<void>::err("Failed to open audio codec");
    }

    avcodec_parameters_from_context(audioStream_->codecpar,
                                    audioCodecCtx_.get());
    audioStream_->time_base = audioCodecCtx_->time_base;

    audioFrame_.reset(av_frame_alloc());
    audioFrame_->format = audioCodecCtx_->sample_fmt;
    av_channel_layout_copy(&audioFrame_->ch_layout, &audioCodecCtx_->ch_layout);
    audioFrame_->sample_rate = audioCodecCtx_->sample_rate;
    audioFrame_->nb_samples = audioCodecCtx_->frame_size;

    if (audioFrame_->nb_samples > 0) {
        av_frame_get_buffer(audioFrame_.get(), 0);
    }

    SwrContext* s = nullptr;
    swr_alloc_set_opts2(&s,
                        &audioCodecCtx_->ch_layout,
                        audioCodecCtx_->sample_fmt,
                        audioCodecCtx_->sample_rate,
                        &layout,
                        AV_SAMPLE_FMT_FLT,
                        settings.audio.sampleRate,
                        0,
                        nullptr);
    swrCtx_.reset(s);
    swr_init(swrCtx_.get());

    return Result<void>::ok();
}

bool VideoRecorderFFmpeg::encodeVideoFrame(AVFrame* frame, u64& bytesWritten) {
    int ret = avcodec_send_frame(videoCodecCtx_.get(), frame);
    if (ret < 0)
        return false;

    while (ret >= 0) {
        ret = avcodec_receive_packet(videoCodecCtx_.get(), packet_.get());
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            break;
        if (ret < 0)
            return false;

        writePacket(packet_.get(), videoStream_, bytesWritten);
    }
    return true;
}

bool VideoRecorderFFmpeg::encodeAudioFrame(AVFrame* frame, u64& bytesWritten) {
    int ret = avcodec_send_frame(audioCodecCtx_.get(), frame);
    if (ret < 0)
        return false;

    while (ret >= 0) {
        ret = avcodec_receive_packet(audioCodecCtx_.get(), packet_.get());
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            break;
        if (ret < 0)
            return false;

        writePacket(packet_.get(), audioStream_, bytesWritten);
    }
    return true;
}

bool VideoRecorderFFmpeg::writePacket(AVPacket* packet,
                                      AVStream* stream,
                                      u64& bytesWritten) {
    av_packet_rescale_ts(packet,
                         stream == videoStream_ ? videoCodecCtx_->time_base
                                                : audioCodecCtx_->time_base,
                         stream->time_base);
    packet->stream_index = stream->index;

    if (av_interleaved_write_frame(formatCtx_.get(), packet) >= 0) {
        bytesWritten += packet->size;
        return true;
    }
    return false;
}

} // namespace vc

#if LIBAVCODEC_VERSION_MAJOR >= 60
#pragma GCC diagnostic pop
#endif
