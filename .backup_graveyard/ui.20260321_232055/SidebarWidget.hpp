/**
 * @file SidebarWidget.hpp
 * @brief Modern sidebar navigation widget
 *
 * Replaces the tab-based dock widget with a modern sidebar navigation system.
 * Features icon-based navigation with collapsible sections and theme support.
 *
 * @version 1.0.0
 */

#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QStackedWidget>
#include <QLabel>
#include <QScrollArea>
#include <QPropertyAnimation>
#include <QIcon>
#include <memory>

// Forward declarations
class PlaylistView;
class PresetBrowser;
class RecordingControls;
class OverlayEditor;
class KaraokeWidget;
class SunoBrowser;
class QToolButton;

namespace chadvis {

/**
 * @brief Navigation item for sidebar
 */
struct NavItem {
    QString id;
    QString label;
    QString iconName;
    QWidget* widget;
    QToolButton* button;
};

/**
 * @brief Modern sidebar navigation widget
 * 
 * Provides a vertical navigation sidebar that replaces the tab-based interface.
 * Features:
 * - Icon-based navigation with optional text labels
 * - Collapsible/expandable modes
 * - Smooth animations
 * - Theme-aware styling
 * - Panel switching via QStackedWidget
 */
class SidebarWidget : public QWidget {
    Q_OBJECT
    Q_PROPERTY(int expandedWidth READ expandedWidth WRITE setExpandedWidth)
    Q_PROPERTY(int collapsedWidth READ collapsedWidth WRITE setCollapsedWidth)

public:
    explicit SidebarWidget(QWidget* parent = nullptr);
    ~SidebarWidget() override;

    /**
     * @brief Add a navigation panel
     * @param id Unique identifier for the panel
     * @param label Display label
     * @param iconName Icon theme name (e.g., "media-playlist")
     * @param widget The panel widget to display
     */
    void addPanel(const QString& id, const QString& label, const QString& iconName, QWidget* widget);

    /**
     * @brief Switch to a specific panel by ID
     */
    void switchToPanel(const QString& id);

    /**
     * @brief Get current panel ID
     */
    QString currentPanel() const;

    /**
     * @brief Set sidebar expanded or collapsed
     */
    void setExpanded(bool expanded);

    /**
     * @brief Check if sidebar is expanded
     */
    bool isExpanded() const { return isExpanded_; }

    /**
     * @brief Toggle expanded/collapsed state
     */
    void toggleExpanded();

    /**
     * @brief Set the width when expanded
     */
    void setExpandedWidth(int width);
    int expandedWidth() const { return expandedWidth_; }

    /**
     * @brief Set the width when collapsed
     */
    void setCollapsedWidth(int width);
    int collapsedWidth() const { return collapsedWidth_; }

    /**
     * @brief Get the content area widget (stacked widget)
     */
    QStackedWidget* contentArea() const { return contentArea_; }

    /**
     * @brief Apply theme styling
     */
    void applyTheme(const QString& themeName);

signals:
    /**
     * @brief Emitted when panel changes
     */
    void panelChanged(const QString& panelId);

    /**
     * @brief Emitted when expand/collapse state changes
     */
    void expansionChanged(bool expanded);

public slots:
    /**
     * @brief Update styling based on current theme
     */
    void updateStyling();

private slots:
    void onNavButtonClicked();
    void animateExpansion();

private:
    void setupUI();
    void createNavButton(NavItem& item);
    void updateButtonStyles();
    QString getStyleSheet(const QString& themeName);

    // Layouts
    QHBoxLayout* mainLayout_;
    QVBoxLayout* navLayout_;
    QVBoxLayout* navButtonsLayout_;
    
    // Widgets
    QWidget* navContainer_;
    QScrollArea* navScroll_;
    QPushButton* toggleButton_;
    QLabel* titleLabel_;
    QStackedWidget* contentArea_;
    
    // State
    QList<NavItem> navItems_;
    QString currentPanelId_;
    bool isExpanded_ = true;
    int expandedWidth_ = 220;
    int collapsedWidth_ = 60;
    
    // Animation
    QPropertyAnimation* widthAnimation_;
    QString currentTheme_;
};

} // namespace chadvis
