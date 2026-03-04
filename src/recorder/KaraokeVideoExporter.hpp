/**
 * @file KaraokeVideoExporter.hpp
 * @brief Export karaoke music videos with visualizer and lyrics
 */

#pragma once

#include "lyrics/LyricsData.hpp"
#include <QObject>
#include <QString>
#include <QSize>
#include <memory>
#include <functional>
#include <atomic>

namespace vc {

class VisualizerPanel;
class OverlayEngine;
class AudioEngine;

namespace recorder {
class VideoRecorderCore;
}

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
        int bitrate{8000000};        // 8 Mbps
        int fps{30};
        QString codec{"libx264"};
        QString preset{"medium"};
        bool hardwareAccel{false};
        
        // Lyrics styling
        QString fontFamily{"Arial"};
        int fontSize{48};
        QColor activeColor{0, 188, 212};    // Cyan
        QColor inactiveColor{128, 128, 128}; // Grey
        QColor shadowColor{0, 0, 0};
        int verticalPosition{80};            // % from top
        
        // Overlay settings
        bool showTitle{true};
        bool showArtist{true};
        bool animateTitle{true};
    };

    explicit KaraokeVideoExporter(QObject* parent = nullptr);
    ~KaraokeVideoExporter() override;

    void setVisualizerPanel(VisualizerPanel* panel);
    void setOverlayEngine(OverlayEngine* engine);
    void setAudioEngine(AudioEngine* engine);

    /**
     * @brief Set the lyrics data for the video
     */
    void setLyrics(const lyrics::LyricsData& lyrics);

    /**
     * @brief Set the audio source (file path or URL)
     */
    void setAudioSource(const QString& path);

    /**
     * @brief Set the ProjectM preset to use
     */
    void setPreset(const QString& presetName);

    /**
     * @brief Start the export process
     */
    bool startExport(const Settings& settings);

    /**
     * @brief Cancel the ongoing export
     */
    void cancelExport();

    /**
     * @brief Check if export is in progress
     */
    bool isExporting() const { return exporting_.load(); }

    /**
     * @brief Get current progress (0.0 to 1.0)
     */
    float progress() const { return progress_.load(); }

signals:
    void progressChanged(float progress);
    void frameRendered(int frame);
    void encodingComplete(const QString& outputPath);
    void encodingFailed(const QString& error);

private slots:
    void onFrameReady();
    void onEncodingProgress(float progress);

private:
    void setupRecording();
    void renderFrame(double timeSeconds);
    void finalize();

    VisualizerPanel* visualizerPanel_{nullptr};
    OverlayEngine* overlayEngine_{nullptr};
    AudioEngine* audioEngine_{nullptr};
    std::unique_ptr<recorder::VideoRecorderCore> recorder_;

    lyrics::LyricsData lyrics_;
    QString audioPath_;
    QString presetName_;
    Settings currentSettings_;

    std::atomic<bool> exporting_{false};
    std::atomic<bool> cancelled_{false};
    std::atomic<float> progress_{0.0f};

    double duration_{0.0};
    int totalFrames_{0};
    int currentFrame_{0};
};

} // namespace vc
