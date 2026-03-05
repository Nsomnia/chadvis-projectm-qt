/**
 * @file KaraokeVideoExporter.cpp
 * @brief Implementation of karaoke video exporter
 */

#include "KaraokeVideoExporter.hpp"
#include "audio/AudioEngine.hpp"
#include "overlay/OverlayEngine.hpp"
#include "visualizer/VisualizerWindow.hpp"
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

void KaraokeVideoExporter::setVisualizerWindow(VisualizerWindow* window) {
    visualizerWindow_ = window;
}

void KaraokeVideoExporter::setOverlayEngine(OverlayEngine* engine) {
    overlayEngine_ = engine;
}

void KaraokeVideoExporter::setAudioEngine(AudioEngine* engine) {
    audioEngine_ = engine;
}

void KaraokeVideoExporter::setLyrics(const LyricsData& lyrics) {
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

    if (!visualizerWindow_) {
        LOG_ERROR("No visualizer window set");
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

    if (audioEngine_) {
        durationMs_ = static_cast<double>(audioEngine_->duration().count());
    }

    if (durationMs_ <= 0) {
        LOG_ERROR("Invalid audio duration");
        exporting_.store(false);
        return false;
    }

    totalFrames_ = static_cast<int>((durationMs_ / 1000.0) * settings.fps);
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
    recorder_ = std::make_unique<VideoRecorder>();

    EncoderSettings encoderSettings;
    encoderSettings.video.width = static_cast<u32>(currentSettings_.resolution.width());
    encoderSettings.video.height = static_cast<u32>(currentSettings_.resolution.height());
    encoderSettings.video.fps = static_cast<u32>(currentSettings_.fps);
    encoderSettings.video.bitrate = static_cast<u32>(currentSettings_.bitrate);
    encoderSettings.video.crf = 18;
    encoderSettings.outputPath = currentSettings_.outputPath.toStdString();

    auto result = recorder_->start(encoderSettings);
    if (!result) {
        LOG_ERROR("Failed to initialize recorder: {}", result.error().message);
        exporting_.store(false);
        emit encodingFailed("Failed to initialize recorder");
        return;
    }

    recorder_->stateChanged.connect([this](RecordingState state) {
        onRecordingStateChanged(state);
    });

    if (visualizerWindow_) {
        connect(visualizerWindow_, &VisualizerWindow::frameCaptured,
                this, &KaraokeVideoExporter::onFrameCaptured);
        visualizerWindow_->startRecording();
    }

    LOG_INFO("Recording started, capturing frames...");
}

void KaraokeVideoExporter::onFrameCaptured(std::vector<u8> data, u32 width, u32 height, i64 timestamp) {
    if (cancelled_.load() || !recorder_) return;

    if (currentFrame_ >= totalFrames_) {
        finalize();
        return;
    }

    double timeSeconds = static_cast<double>(currentFrame_) / currentSettings_.fps;

    QImage frame(reinterpret_cast<const uchar*>(data.data()),
                 static_cast<int>(width),
                 static_cast<int>(height),
                 static_cast<int>(width * 4),
                 QImage::Format_RGBA8888);

    if (!frame.isNull() && !lyrics_.empty()) {
        renderLyrics(frame, timeSeconds);
    }

    recorder_->submitVideoFrame(std::move(data), width, height, timestamp);

    float prog = static_cast<float>(currentFrame_) / totalFrames_;
    progress_.store(prog);
    emit progressChanged(prog);
    emit frameRendered(currentFrame_);

    currentFrame_++;
}

void KaraokeVideoExporter::renderLyrics(QImage& frame, double timeSeconds) {
    QPainter painter(&frame);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);

    const auto& lines = lyrics_.lines;
    int activeLineIndex = -1;

    for (size_t i = 0; i < lines.size(); ++i) {
        double lineStart = static_cast<double>(lines[i].startTime);
        double lineEnd = static_cast<double>(lines[i].endTime);

        if (timeSeconds >= lineStart && timeSeconds < lineEnd) {
            activeLineIndex = static_cast<int>(i);
            break;
        }
    }

    QFont font(currentSettings_.fontFamily, currentSettings_.fontSize);
    font.setBold(true);
    painter.setFont(font);

    const int y = currentSettings_.resolution.height() * currentSettings_.verticalPosition / 100;

    if (activeLineIndex >= 0 && activeLineIndex < static_cast<int>(lines.size())) {
        if (currentSettings_.shadowColor.alpha() > 0) {
            painter.setPen(currentSettings_.shadowColor);
            painter.drawText(2, y + 2,
                           currentSettings_.resolution.width(),
                           currentSettings_.fontSize * 2,
                           Qt::AlignHCenter | Qt::AlignVCenter,
                           QString::fromStdString(lines[activeLineIndex].text));
        }

        painter.setPen(currentSettings_.activeColor);
        painter.drawText(0, y,
                        currentSettings_.resolution.width(),
                        currentSettings_.fontSize * 2,
                        Qt::AlignHCenter | Qt::AlignVCenter,
                        QString::fromStdString(lines[activeLineIndex].text));

        if (activeLineIndex + 1 < static_cast<int>(lines.size())) {
            painter.setPen(currentSettings_.inactiveColor);
            painter.setFont(QFont(currentSettings_.fontFamily, currentSettings_.fontSize * 0.7));
            painter.drawText(0, y + currentSettings_.fontSize * 2,
                            currentSettings_.resolution.width(),
                            currentSettings_.fontSize * 2,
                            Qt::AlignHCenter | Qt::AlignVCenter,
                            QString::fromStdString(lines[activeLineIndex + 1].text));
        }
    }
}

void KaraokeVideoExporter::onRecordingStateChanged(RecordingState state) {
    if (state == RecordingState::Stopped) {
        finalize();
    } else if (state == RecordingState::Error) {
        emit encodingFailed("Recording error");
    }
}

void KaraokeVideoExporter::finalize() {
    LOG_INFO("Finalizing video export...");

    if (visualizerWindow_) {
        visualizerWindow_->stopRecording();
    }

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
