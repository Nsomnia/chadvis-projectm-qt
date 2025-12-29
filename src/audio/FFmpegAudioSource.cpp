#include "FFmpegAudioSource.hpp"
#include "core/Logger.hpp"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
}

namespace vc {

struct FFmpegAudioSource::Private {
    AVFormatContext* formatCtx = nullptr;
    AVCodecContext* codecCtx = nullptr;
    AVFrame* frame = nullptr;
    AVPacket* packet = nullptr;
    SwrContext* swrCtx = nullptr;
    
    int audioStreamIndex = -1;
    projectm_handle projectM = nullptr;
    
    // Audio format conversion
    int sampleRate = 48000;
    int channels = 2;
    
    bool isPlaying = false;
    bool isPaused = false;
};

FFmpegAudioSource::FFmpegAudioSource() 
    : d(std::make_unique<Private>()) {
}

FFmpegAudioSource::~FFmpegAudioSource() {
    stop();
}

bool FFmpegAudioSource::init(projectm_handle pM, int sampleRate) {
    if (!pM) {
        LOG_ERROR("Invalid projectM handle");
        return false;
    }
    
    d->projectM = pM;
    d->sampleRate = sampleRate;
    
    LOG_INFO("FFmpegAudioSource initialized");
    return true;
}

bool FFmpegAudioSource::loadFile(const std::string& path) {
    // Clean up any existing resources
    cleanup();
    
    // Open input file
    if (avformat_open_input(&d->formatCtx, path.c_str(), nullptr, nullptr) < 0) {
        LOG_ERROR("Could not open file: {}", path);
        return false;
    }
    
    // Find stream info
    if (avformat_find_stream_info(d->formatCtx, nullptr) < 0) {
        LOG_ERROR("Could not find stream info");
        cleanup();
        return false;
    }
    
    // Find audio stream
    d->audioStreamIndex = -1;
    for (unsigned int i = 0; i < d->formatCtx->nb_streams; i++) {
        if (d->formatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            d->audioStreamIndex = i;
            break;
        }
    }
    
    if (d->audioStreamIndex == -1) {
        LOG_ERROR("No audio stream found");
        cleanup();
        return false;
    }
    
    // Get codec
    AVCodecParameters* codecParams = d->formatCtx->streams[d->audioStreamIndex]->codecpar;
    const AVCodec* codec = avcodec_find_decoder(codecParams->codec_id);
    if (!codec) {
        LOG_ERROR("Codec not found");
        cleanup();
        return false;
    }
    
    // Open codec context
    d->codecCtx = avcodec_alloc_context3(codec);
    if (!d->codecCtx) {
        LOG_ERROR("Could not allocate codec context");
        cleanup();
        return false;
    }
    
    if (avcodec_parameters_to_context(d->codecCtx, codecParams) < 0) {
        LOG_ERROR("Could not copy codec params");
        cleanup();
        return false;
    }
    
    if (avcodec_open2(d->codecCtx, codec, nullptr) < 0) {
        LOG_ERROR("Could not open codec");
        cleanup();
        return false;
    }
    
    // Allocate frames and packet
    d->frame = av_frame_alloc();
    d->packet = av_packet_alloc();
    
    if (!d->frame || !d->packet) {
        LOG_ERROR("Could not allocate frame/packet");
        cleanup();
        return false;
    }
    
    // Set up resampler for converting to float stereo
    AVChannelLayout outLayout = AV_CHANNEL_LAYOUT_STEREO;
    int ret = swr_alloc_set_opts2(&d->swrCtx,
        &outLayout, AV_SAMPLE_FMT_FLT, d->sampleRate,
        &d->codecCtx->ch_layout, d->codecCtx->sample_fmt, d->codecCtx->sample_rate,
        0, nullptr);
    
    if (ret < 0 || swr_init(d->swrCtx) < 0) {
        LOG_ERROR("Could not initialize resampler");
        cleanup();
        return false;
    }
    
    LOG_INFO("Loaded audio file: {}", path);
    LOG_DEBUG("  Codec: {}, Sample rate: {}, Channels: {}", 
              codec->name, d->codecCtx->sample_rate, d->codecCtx->ch_layout.nb_channels);
    
    return true;
}

void FFmpegAudioSource::play() {
    if (!d->formatCtx || !d->codecCtx) {
        LOG_WARN("No file loaded");
        return;
    }
    
    d->isPlaying = true;
    d->isPaused = false;
    
    // Start decoding in a separate thread
    if (!decodeThread_.joinable()) {
        decodeThread_ = std::thread(&FFmpegAudioSource::decodeLoop, this);
        LOG_INFO("Playback started (decoding thread launched)");
    }
}

void FFmpegAudioSource::pause() {
    d->isPaused = true;
    LOG_INFO("Playback paused");
}

void FFmpegAudioSource::resume() {
    d->isPaused = false;
    LOG_INFO("Playback resumed");
}

void FFmpegAudioSource::stop() {
    d->isPlaying = false;
    d->isPaused = false;
    
    if (decodeThread_.joinable()) {
        decodeThread_.join();
    }
    
    cleanup();
    LOG_INFO("Playback stopped");
}

void FFmpegAudioSource::decodeLoop() {
    LOG_DEBUG("Decode thread started");
    
    while (d->isPlaying) {
        if (d->isPaused) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        
        // Read packet
        if (av_read_frame(d->formatCtx, d->packet) < 0) {
            // End of file
            LOG_INFO("End of file reached");
            d->isPlaying = false;
            break;
        }
        
        // Check if it's the audio stream
        if (d->packet->stream_index != d->audioStreamIndex) {
            av_packet_unref(d->packet);
            continue;
        }
        
        // Send to decoder
        if (avcodec_send_packet(d->codecCtx, d->packet) < 0) {
            LOG_WARN("Error sending packet to decoder");
            av_packet_unref(d->packet);
            continue;
        }
        
        // Receive decoded frames
        while (avcodec_receive_frame(d->codecCtx, d->frame) >= 0) {
            // Allocate output buffer (estimate size)
            int outSamples = d->frame->nb_samples * 2;  // At least as many samples
            std::vector<uint8_t> outputBuffer(outSamples * 2 * sizeof(float));  // Stereo float
            
            // Convert to float stereo
            uint8_t* outputPtr = outputBuffer.data();
            int convertedSamples = swr_convert(d->swrCtx, &outputPtr, outSamples,
                                               (const uint8_t**)d->frame->data, d->frame->nb_samples);
            
            if (convertedSamples < 0) {
                LOG_WARN("swr_convert failed: {}", convertedSamples);
                av_frame_unref(d->frame);
                continue;
            }
            
            if (convertedSamples > 0 && d->projectM) {
                // Feed to projectM
                float* floatData = reinterpret_cast<float*>(outputPtr);
                projectm_pcm_add_float(d->projectM, floatData, convertedSamples, PROJECTM_STEREO);
                
                LOG_DEBUG("Fed {} samples to projectM", convertedSamples);
            }
            
            av_frame_unref(d->frame);
        }
        
        av_packet_unref(d->packet);
        
        // Small delay to prevent CPU spinning
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    
    LOG_DEBUG("Decode thread finished");
}

void FFmpegAudioSource::cleanup() {
    if (d->swrCtx) {
        swr_free(&d->swrCtx);
        d->swrCtx = nullptr;
    }
    
    if (d->packet) {
        av_packet_free(&d->packet);
        d->packet = nullptr;
    }
    
    if (d->frame) {
        av_frame_free(&d->frame);
        d->frame = nullptr;
    }
    
    if (d->codecCtx) {
        avcodec_free_context(&d->codecCtx);
        d->codecCtx = nullptr;
    }
    
    if (d->formatCtx) {
        avformat_close_input(&d->formatCtx);
        d->formatCtx = nullptr;
    }
    
    d->audioStreamIndex = -1;
}

} // namespace vc
