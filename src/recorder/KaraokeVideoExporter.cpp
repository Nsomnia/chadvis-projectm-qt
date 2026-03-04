/**
 * @file KaraokeVideoExporter.cpp
 * @brief Implementation of karaoke video exporter
 */

#include "KaraokeVideoExporter.hpp"
#include "VideoRecorderCore.hpp"
#include "ui/VisualizerPanel.hpp"
#include "overlay/OverlayEngine.hpp"
#include "audio/AudioEngine.hpp"
#include "core/Logger.hpp"

#include <QImage>
#include <QPainter>
#include <QTimer>

namespace vc {

KaraokeVideoExporter::KaraokeVideoExporter(QObject* parent)
    : QObject(parent)
{
}

KaraokeVideoExporter::~KaraokeVideoExporter() {
    cancelExport();
}

void KaraokeVideoExporter::setVisualizerPanel(VisualizerPanel* panel) {
    visualizerPanel_ = panel;
}

void KaraokeVideoExporter::setOverlayEngine(OverlayEngine* engine) {
    overlayEngine_ = engine;
}

void KaraokeVideoExporter::setAudioEngine(AudioEngine* engine) {
    audioEngine_ = engine;
}

void KaraokeVideoExporter::setLyrics(const lyrics::LyricsData& lyrics) {
    lyrics_ = lyrics;
}

void KaraokeVideoExporter::setAudioSource(const QString& path) {
    audioPath_ = path;
}

void KaraokeVideoExporter::setPreset(const QString& presetName) {
    presetName_ = presetName;
}

bool KaraokeVideoExporter::startExport(const Settings& settings) {
    if (exporting_.load()) {
        LOG_ERROR("Export already in progress");
        return false;
    }

    if (!visualizerPanel_) {
        LOG_ERROR("No visualizer panel set");
        return false;
    }

    if (audioPath_.isEmpty()) {
        LOG_ERROR("No audio source set");
        return false;
    }

    currentSettings_ = settings;
    exporting_.store(true);
    cancelled_.store(false);
    progress_.store(0.0f);
    currentFrame_ = 0;

    LOG_INFO("Starting karaoke video export: {}x{} @ {} fps",
             settings.resolution.width(),
             settings.resolution.height(),
             settings.fps);

    // Get audio duration
    if (audioEngine_) {
        duration_ = audioEngine_->duration();
    }

    if (duration_ <= 0) {
        LOG_ERROR("Invalid audio duration");
        exporting_.store(false);
        return false;
    }

    totalFrames_ = static_cast<int>(duration_ * settings.fps);
    LOG_INFO("Total frames: {}", totalFrames_);

    setupRecording();

    return true;
}

void KaraokeVideoExporter::cancelExport() {
    if (!exporting_.load()) return;

    LOG_INFO("Cancelling export...");
    cancelled_.store(true);

    if (recorder_) {
        recorder_->stop();
    }

    exporting_.store(false);
    emit encodingFailed("Export cancelled");
}

void KaraokeVideoExporter::setupRecording() {
    // Create recorder
    recorder_ = std::make_unique<recorder::VideoRecorderCore>();

    // Configure encoder
    recorder::EncoderConfig config;
    config.width = currentSettings_.resolution.width();
    config.height = currentSettings_.resolution.height();
    config.fps = currentSettings_.fps;
    config.bitrate = currentSettings_.bitrate;
    config.codec = currentSettings_.codec.toStdString();
    config.preset = currentSettings_.preset.toStdString();
    config.outputPath = currentSettings_.outputPath.toStdString();

    if (!recorder_->initialize(config)) {
        LOG_ERROR("Failed to initialize recorder");
        exporting_.store(false);
        emit encodingFailed("Failed to initialize recorder");
        return;
    }

    // Connect progress signal
    connect(recorder_.get(), &recorder::VideoRecorderCore::progress,
            this, &KaraokeVideoExporter::onEncodingProgress);

    // Start recording
    if (!recorder_->start()) {
        LOG_ERROR("Failed to start recorder");
        exporting_.store(false);
        emit encodingFailed("Failed to start recorder");
        return;
    }

    // Start frame rendering
    LOG_INFO("Recording started, rendering frames...");
    
    // Use a timer to render frames
    auto* renderTimer = new QTimer(this);
    connect(renderTimer, &QTimer::timeout, this, &KaraokeVideoExporter::onFrameReady);
    renderTimer->start(1); // As fast as possible
}

void KaraokeVideoExporter::onFrameReady() {
    if (cancelled_.load() || currentFrame_ >= totalFrames_) {
        finalize();
        return;
    }

    // Calculate time for this frame
    const double timeSeconds = static_cast<double>(currentFrame_) / currentSettings_.fps;

    // Render frame
    renderFrame(timeSeconds);

    // Update progress
    const float prog = static_cast<float>(currentFrame_) / totalFrames_;
    progress_.store(prog);
    emit progressChanged(prog);
    emit frameRendered(currentFrame_);

    currentFrame_++;
}

void KaraokeVideoExporter::renderFrame(double timeSeconds) {
    if (!visualizerPanel_ || !recorder_) return;

    // Get frame from visualizer
    QImage frame = visualizerPanel_->grabFramebuffer();
    
    if (frame.isNull()) {
        frame = QImage(currentSettings_.resolution, QImage::Format_RGB32);
        frame.fill(Qt::black);
    }

    // Scale to target resolution
    if (frame.size() != currentSettings_.resolution) {
        frame = frame.scaled(currentSettings_.resolution, 
                             Qt::IgnoreAspectRatio, 
                             Qt::SmoothTransformation);
    }

    // Render lyrics overlay
    if (overlayEngine_ && !lyrics_.empty()) {
        QPainter painter(&frame);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::TextAntialiasing);

        // Find current lyric line
        const auto& lines = lyrics_.lines;
        int activeLineIndex = -1;
        
        for (size_t i = 0; i < lines.size(); ++i) {
            if (timeSeconds >= lines[i].startTime && timeSeconds < lines[i].endTime) {
                activeLineIndex = static_cast<int>(i);
                break;
            }
        }

        // Configure font
        QFont font(currentSettings_.fontFamily, currentSettings_.fontSize);
        font.setBold(true);
        painter.setFont(font);

        const int y = currentSettings_.resolution.height() * currentSettings_.verticalPosition / 100;

        // Draw shadow
        if (currentSettings_.shadowColor.alpha() > 0) {
            painter.setPen(currentSettings_.shadowColor);
            painter.drawText(0, y - currentSettings_.fontSize, 
                            currentSettings_.resolution.width(), 
                            currentSettings_.fontSize * 2,
                            Qt::AlignHCenter | Qt::AlignVCenter,
                            lines[activeLineIndex].text.c_str());
        }

        // Draw active line
        if (activeLineIndex >= 0 && activeLineIndex < static_cast<int>(lines.size())) {
            painter.setPen(currentSettings_.activeColor);
            painter.drawText(0, y, 
                            currentSettings_.resolution.width(), 
                            currentSettings_.fontSize * 2,
                            Qt::AlignHCenter | Qt::AlignVCenter,
                            lines[activeLineIndex].text.c_str());
        }

        // Draw next line (preview)
        if (activeLineIndex >= 0 && activeLineIndex + 1 < static_cast<int>(lines.size())) {
            painter.setPen(currentSettings_.inactiveColor);
            painter.setFont(QFont(currentSettings_.fontFamily, currentSettings_.fontSize * 0.7));
            painter.drawText(0, y + currentSettings_.fontSize * 1.5,
                            currentSettings_.resolution.width(),
                            currentSettings_.fontSize * 2,
                            Qt::AlignHCenter | Qt::AlignVCenter,
                            lines[activeLineIndex + 1].text.c_str());
        }
    }

    // Encode frame
    recorder_->addFrame(frame);
}

void KaraokeVideoExporter::onEncodingProgress(float progress) {
    progress_.store(progress);
    emit progressChanged(progress);
}

void KaraokeVideoExporter::finalize() {
    LOG_INFO("Finalizing video export...");

    if (recorder_) {
        recorder_->stop();
        recorder_.reset();
    }

    exporting_.store(false);
    progress_.store(1.0f);
    emit progressChanged(1.0f);
    emit encodingComplete(currentSettings_.outputPath);

    LOG_INFO("Video export complete: {}", currentSettings_.outputPath.toStdString());
}

} // namespace vc
