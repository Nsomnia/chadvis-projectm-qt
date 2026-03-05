/**
 * @file BottomToolsPanel.cpp
 */

#include "BottomToolsPanel.hpp"
#include "IconManager.hpp"

#include "ui/PlaylistView.hpp"
#include "ui/PresetBrowser.hpp"
#include "ui/RecordingControls.hpp"
#include "ui/OverlayEditor.hpp"
#include "ui/KaraokeWidget.hpp"
#include "ui/SunoBrowser.hpp"
#include "ui/LyricsPanel.hpp"

#include <QVBoxLayout>
#include <QLabel>

namespace vc::ui {

BottomToolsPanel::BottomToolsPanel(QWidget* parent)
    : QFrame(parent)
{
    setupUI();
    setupStyling();
}

BottomToolsPanel::~BottomToolsPanel() = default;

void BottomToolsPanel::setupUI() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(2);

    container_ = new AccordionContainer(this);
    container_->setSingleExpansion(true);
    layout->addWidget(container_);

    createPanels();
}

void BottomToolsPanel::createPanels() {
    // Create placeholder wrappers - actual widgets set later
    playlistWrapper_ = new QWidget;
    auto* plLayout = new QVBoxLayout(playlistWrapper_);
    plLayout->setContentsMargins(0, 0, 0, 0);
    plLayout->addWidget(new QLabel("Playlist"));

    presetWrapper_ = new QWidget;
    auto* prLayout = new QVBoxLayout(presetWrapper_);
    prLayout->setContentsMargins(0, 0, 0, 0);
    prLayout->addWidget(new QLabel("Presets"));

    recordingWrapper_ = new QWidget;
    auto* recLayout = new QVBoxLayout(recordingWrapper_);
    recLayout->setContentsMargins(0, 0, 0, 0);
    recLayout->addWidget(new QLabel("Recording"));

    overlayWrapper_ = new QWidget;
    auto* ovLayout = new QVBoxLayout(overlayWrapper_);
    ovLayout->setContentsMargins(0, 0, 0, 0);
    ovLayout->addWidget(new QLabel("Overlays"));

    lyricsWrapper_ = new QWidget;
    auto* lyLayout = new QVBoxLayout(lyricsWrapper_);
    lyLayout->setContentsMargins(0, 0, 0, 0);
    lyLayout->addWidget(new QLabel("Lyrics"));

    sunoWrapper_ = new QWidget;
    auto* suLayout = new QVBoxLayout(sunoWrapper_);
    suLayout->setContentsMargins(0, 0, 0, 0);
    suLayout->addWidget(new QLabel("Suno Library"));

    // Add panels to accordion
    container_->addPanel("playlist", "Playlist", "playlist", playlistWrapper_);
    container_->addPanel("presets", "Presets", "preset", presetWrapper_);
    container_->addPanel("recording", "Recording", "record", recordingWrapper_);
    container_->addPanel("overlays", "Overlays", "overlay", overlayWrapper_);
    container_->addPanel("lyrics", "Lyrics", "lyrics", lyricsWrapper_);
    container_->addPanel("suno", "Suno Library", "suno", sunoWrapper_);

    // Expand first panel by default
    container_->expandPanel(0);
}

void BottomToolsPanel::setupStyling() {
    setStyleSheet(R"(
        BottomToolsPanel {
            background: rgba(30, 30, 40, 0.95);
            border-top: 1px solid rgba(0, 188, 212, 0.3);
            border-radius: 8px;
        }
    )");
}

	void BottomToolsPanel::setPlaylistView(vc::PlaylistView* view) {
    if (playlistView_ && playlistWrapper_->layout()) {
        playlistWrapper_->layout()->removeWidget(playlistView_);
    }
    playlistView_ = view;
    if (view && playlistWrapper_->layout()) {
        // Remove placeholder
        QLayoutItem* item;
        while ((item = playlistWrapper_->layout()->takeAt(0)) != nullptr) {
            if (item->widget()) item->widget()->deleteLater();
            delete item;
        }
        view->setParent(playlistWrapper_);
        playlistWrapper_->layout()->addWidget(view);
    }
}

void BottomToolsPanel::setPresetBrowser(vc::PresetBrowser* browser) {
	if (presetBrowser_ && presetWrapper_->layout()) {
		presetWrapper_->layout()->removeWidget(presetBrowser_);
	}
	presetBrowser_ = browser;
	if (browser && presetWrapper_->layout()) {
		QLayoutItem* item;
		while ((item = presetWrapper_->layout()->takeAt(0)) != nullptr) {
			if (item->widget()) item->widget()->deleteLater();
			delete item;
		}
		browser->setParent(presetWrapper_);
		presetWrapper_->layout()->addWidget(browser);
	}
}

void BottomToolsPanel::setRecordingControls(vc::RecordingControls* controls) {
	if (recordingControls_ && recordingWrapper_->layout()) {
		recordingWrapper_->layout()->removeWidget(recordingControls_);
	}
	recordingControls_ = controls;
	if (controls && recordingWrapper_->layout()) {
		QLayoutItem* item;
		while ((item = recordingWrapper_->layout()->takeAt(0)) != nullptr) {
			if (item->widget()) item->widget()->deleteLater();
			delete item;
		}
		controls->setParent(recordingWrapper_);
		recordingWrapper_->layout()->addWidget(controls);
	}
}

void BottomToolsPanel::setOverlayEditor(vc::OverlayEditor* editor) {
	overlayEditor_ = editor;
}

void BottomToolsPanel::setKaraokeWidget(vc::KaraokeWidget* widget) {
	karaokeWidget_ = widget;
}

void BottomToolsPanel::setSunoBrowser(vc::suno::SunoBrowser* browser) {
	sunoBrowser_ = browser;
}

void BottomToolsPanel::setLyricsPanel(vc::LyricsPanel* panel) {
	lyricsPanel_ = panel;
}

void BottomToolsPanel::expandPanel(const QString& id) {
    container_->expandPanel(id);
}

void BottomToolsPanel::collapseAll() {
    container_->collapseAll();
}

void BottomToolsPanel::applyTheme(const QString& theme) {
    // Theme support
}

} // namespace vc::ui
