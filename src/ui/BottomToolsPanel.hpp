/**
 * @file BottomToolsPanel.hpp
 * @brief Modern bottom tools panel with accordion layout
 */

#pragma once

#include "accordion/AccordionContainer.hpp"
#include <QFrame>
#include <memory>

// Forward declarations
class PlaylistView;
class PresetBrowser;
class RecordingControls;
class OverlayEditor;
class KaraokeWidget;
class SunoBrowser;
class LyricsPanel;

namespace vc::ui {

/**
 * @brief Bottom tools panel with collapsible accordion sections
 * 
 * Provides a modern mobile-inspired interface with:
 * - Playlist panel
 * - Presets panel
 * - Recording panel
 * - Overlays panel
 * - Suno library panel
 * - Lyrics panel
 */
class BottomToolsPanel : public QFrame {
    Q_OBJECT

public:
    explicit BottomToolsPanel(QWidget* parent = nullptr);
    ~BottomToolsPanel() override;

    void setPlaylistView(PlaylistView* view);
    void setPresetBrowser(PresetBrowser* browser);
    void setRecordingControls(RecordingControls* controls);
    void setOverlayEditor(OverlayEditor* editor);
    void setKaraokeWidget(KaraokeWidget* widget);
    void setSunoBrowser(SunoBrowser* browser);
    void setLyricsPanel(LyricsPanel* panel);

    PlaylistView* playlistView() const { return playlistView_; }
    PresetBrowser* presetBrowser() const { return presetBrowser_; }
    RecordingControls* recordingControls() const { return recordingControls_; }

    void expandPanel(const QString& id);
    void collapseAll();

signals:
    void panelExpanded(const QString& panelId);

public slots:
    void applyTheme(const QString& theme);

private:
    void setupUI();
    void createPanels();
    void setupStyling();

    AccordionContainer* container_{nullptr};
    
    // External widgets (not owned)
    PlaylistView* playlistView_{nullptr};
    PresetBrowser* presetBrowser_{nullptr};
    RecordingControls* recordingControls_{nullptr};
    OverlayEditor* overlayEditor_{nullptr};
    KaraokeWidget* karaokeWidget_{nullptr};
    SunoBrowser* sunoBrowser_{nullptr};
    LyricsPanel* lyricsPanel_{nullptr};

    // Internal wrapper widgets
    QWidget* playlistWrapper_{nullptr};
    QWidget* presetWrapper_{nullptr};
    QWidget* recordingWrapper_{nullptr};
    QWidget* overlayWrapper_{nullptr};
    QWidget* lyricsWrapper_{nullptr};
    QWidget* sunoWrapper_{nullptr};
};

} // namespace vc::ui
