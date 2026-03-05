/**
 * @file BottomToolsPanel.hpp
 * @brief Modern bottom tools panel with accordion layout
 */

#pragma once

#include "accordion/AccordionContainer.hpp"
#include <QFrame>
#include <memory>

namespace vc {
class PlaylistView;
class PresetBrowser;
class RecordingControls;
class OverlayEditor;
class KaraokeWidget;
class LyricsPanel;
namespace suno {
class SunoBrowser;
}
}

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

	void setPlaylistView(vc::PlaylistView* view);
	void setPresetBrowser(vc::PresetBrowser* browser);
	void setRecordingControls(vc::RecordingControls* controls);
	void setOverlayEditor(vc::OverlayEditor* editor);
	void setKaraokeWidget(vc::KaraokeWidget* widget);
	void setSunoBrowser(vc::suno::SunoBrowser* browser);
	void setLyricsPanel(vc::LyricsPanel* panel);

	vc::PlaylistView* playlistView() const { return playlistView_; }
	vc::PresetBrowser* presetBrowser() const { return presetBrowser_; }
	vc::RecordingControls* recordingControls() const { return recordingControls_; }

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
	vc::PlaylistView* playlistView_{nullptr};
	vc::PresetBrowser* presetBrowser_{nullptr};
	vc::RecordingControls* recordingControls_{nullptr};
	vc::OverlayEditor* overlayEditor_{nullptr};
	vc::KaraokeWidget* karaokeWidget_{nullptr};
	vc::suno::SunoBrowser* sunoBrowser_{nullptr};
	vc::LyricsPanel* lyricsPanel_{nullptr};

    // Internal wrapper widgets
    QWidget* playlistWrapper_{nullptr};
    QWidget* presetWrapper_{nullptr};
    QWidget* recordingWrapper_{nullptr};
    QWidget* overlayWrapper_{nullptr};
    QWidget* lyricsWrapper_{nullptr};
    QWidget* sunoWrapper_{nullptr};
};

} // namespace vc::ui
