#pragma once

/**
 * @file MobileSidebar.hpp
 * @brief Modern mobile-app style sidebar with accordion navigation
 *
 * Provides a collapsible sidebar with accordion-style sections for
 * navigation. Designed for both desktop and mobile use with:
 * - Collapsible sections (accordion)
 * - Compact mode (icon-only)
 * - Smooth animations
 * - Touch-friendly interface
 *
 * @section Usage
 * @code
 * auto* sidebar = new MobileSidebar(this);
 * sidebar->addSection("library", "Library", "media-playlist", playlistView);
 * sidebar->addSection("visualizer", "Visualizer", "video-display", presetBrowser);
 * sidebar->setCompactMode(true); // Icon-only
 * @endcode
 */

#include <QWidget>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QPropertyAnimation>
#include <QGestureEvent>
#include <memory>
#include <vector>

namespace chadvis {

class AccordionSection;

struct SidebarSection {
    QString id;
    QString title;
    QString iconName;
    QWidget* widget{nullptr};
    AccordionSection* section{nullptr};
};

class MobileSidebar : public QWidget {
    Q_OBJECT
    Q_PROPERTY(int expandedWidth READ expandedWidth WRITE setExpandedWidth)
    Q_PROPERTY(int collapsedWidth READ collapsedWidth WRITE setCollapsedWidth)
    Q_PROPERTY(bool compactMode READ isCompactMode WRITE setCompactMode)

public:
    explicit MobileSidebar(QWidget* parent = nullptr);
    ~MobileSidebar() override;

    void addSection(const QString& id, 
                   const QString& title, 
                   const QString& iconName, 
                   QWidget* widget);
    
    void removeSection(const QString& id);
    void expandSection(const QString& id);
    void collapseAll();
    void collapseSection(const QString& id);
    
    QString currentSection() const { return currentSectionId_; }
    void setCurrentSection(const QString& id);
    
    void setCompactMode(bool compact);
    bool isCompactMode() const { return compactMode_; }
    
    void setExpandedWidth(int width);
    int expandedWidth() const { return expandedWidth_; }
    
    void setCollapsedWidth(int width);
    int collapsedWidth() const { return collapsedWidth_; }
    
    void toggleCollapse();
    void toggleSection(const QString& id);
    
    void applyTheme(const QString& themeName);
    
    int sectionCount() const { return static_cast<int>(sections_.size()); }
    bool hasSection(const QString& id) const;

signals:
    void sectionChanged(const QString& sectionId);
    void sectionExpanded(const QString& sectionId);
    void sectionCollapsed(const QString& sectionId);
    void compactModeChanged(bool compact);
    void widthChanged(int width);

protected:
    bool event(QEvent* event) override;
    bool gestureEvent(QGestureEvent* event);
    void swipeGesture(QSwipeGesture* gesture);

private slots:
    void onSectionToggled();

private:
    void setupUI();
    void createHeader();
    void createSectionWidget(SidebarSection& section);
    void updateLayout();
    void animateWidth(int targetWidth);
    QString getStyleSheet(const QString& themeName);

    QWidget* container_{nullptr};
    QVBoxLayout* mainLayout_{nullptr};
    QVBoxLayout* sectionLayout_{nullptr};
    QScrollArea* scrollArea_{nullptr};
    QWidget* headerWidget_{nullptr};
    
    std::vector<SidebarSection> sections_;
    QString currentSectionId_;
    
    bool compactMode_{false};
    bool expanded_{true};
    int expandedWidth_{280};
    int collapsedWidth_{72};
    
    QPropertyAnimation* widthAnimation_{nullptr};
    QString currentTheme_;
};

} // namespace chadvis
