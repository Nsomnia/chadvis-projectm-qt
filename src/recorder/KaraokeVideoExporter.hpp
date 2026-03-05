/**
 * @file KaraokeVideoExporter.hpp
 * @brief Export karaoke music videos with visualizer and lyrics
 */

#pragma once

#include "lyrics/LyricsData.hpp"
#include "recorder/VideoRecorderCore.hpp"
#include <QObject>
#include <QString>
#include <QSize>
#include <QColor>
#include <memory>
#include <atomic>

namespace vc {

class VisualizerWindow;
class OverlayEngine;
class AudioEngine;
class VideoRecorder;

/**
 * @brief Karaoke video exporter combining visualizer, lyrics, and audio
 *
 * Creates professional karaoke/music videos by:
 * - Recording ProjectM visualizer frames
 * - Rendering synchronized lyrics overlays
 * - Encoding with FFmpeg (H.264 + AAC)
 *
 * Supports:
 * - Multiple resolutions (720p, 1080p, 4K)
 * - Hardware acceleration (NVENC, VAAPI)
 * - Custom lyrics styling
 * - Static/animated text overlays
 */
class KaraokeVideoExporter : public QObject {
    Q_OBJECT

public:
    struct Settings {
        QString outputPath;
        QSize resolution{1920, 1080};
        int bitrate{8000000};
        int fps{30};
        QString codec{"libx264"};
        QString preset{"medium"};
        bool hardwareAccel{false};

        QString fontFamily{"Arial"};
        int fontSize{48};
        QColor activeColor{0, 188, 212};
        QColor inactiveColor{128, 128, 128};
        QColor shadowColor{0, 0, 0};
        int verticalPosition{80};

        bool showTitle{true};
        bool showArtist{true};
        bool animateTitle{true};
    };

    explicit KaraokeVideoExporter(QObject* parent = nullptr);
    ~KaraokeVideoExporter() override;

    void setVisualizerWindow(VisualizerWindow* window);
    void setOverlayEngine(OverlayEngine* engine);
    void setAudioEngine(AudioEngine* engine);
    void setLyrics(const LyricsData& lyrics);
    void setAudioSource(const QString& path);
    void setPreset(const QString& presetName);

    bool startExport(const Settings& settings);
    void cancelExport();

    bool isExporting() const { return exporting_.load(); }
    float progress() const { return progress_.load(); }

signals:
    void progressChanged(float progress);
    void frameRendered(int frame);
    void encodingComplete(const QString& outputPath);
    void encodingFailed(const QString& error);

private slots:
    void onFrameCaptured(std::vector<u8> data, u32 width, u32 height, i64 timestamp);
    void onRecordingStateChanged(RecordingState state);

private:
    void setupRecording();
    void renderLyrics(QImage& frame, double timeSeconds);
    void finalize();

    VisualizerWindow* visualizerWindow_{nullptr};
    OverlayEngine* overlayEngine_{nullptr};
    AudioEngine* audioEngine_{nullptr};
    std::unique_ptr<VideoRecorder> recorder_;

    LyricsData lyrics_;
    QString audioPath_;
    QString presetName_;
    Settings currentSettings_;

    std::atomic<bool> exporting_{false};
    std::atomic<bool> cancelled_{false};
    std::atomic<float> progress_{0.0f};

    double durationMs_{0.0};
    int totalFrames_{0};
    int currentFrame_{0};
};

} // namespace vc
