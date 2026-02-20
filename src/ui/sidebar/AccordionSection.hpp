#pragma once

/**
 * @file AccordionSection.hpp
 * @brief Collapsible accordion section widget for modern mobile-style navigation
 *
 * Provides a single expandable/collapsible section with animated transitions.
 * Designed for use within MobileSidebar to create accordion-style navigation.
 *
 * @section Features
 * - Smooth expand/collapse animation with easing
 * - Icon + title header with expand indicator
 * - Customizable content widget
 * - Theme-aware styling
 * - Touch-friendly tap targets
 */

#include <QWidget>
#include <QVBoxLayout>
#include <QToolButton>
#include <QLabel>
#include <QPropertyAnimation>
#include <QParallelAnimationGroup>
#include <memory>

class QScrollArea;

namespace chadvis {

class AccordionSection : public QWidget {
    Q_OBJECT
    Q_PROPERTY(bool expanded READ isExpanded WRITE setExpanded NOTIFY expandedChanged)
    Q_PROPERTY(QString title READ title WRITE setTitle)
    Q_PROPERTY(QString icon READ icon WRITE setIcon)

public:
    explicit AccordionSection(QWidget* parent = nullptr);
    explicit AccordionSection(const QString& title, QWidget* parent = nullptr);
    ~AccordionSection() override;

    void setContentWidget(QWidget* content);
    QWidget* contentWidget() const { return contentWidget_; }
    
    void setTitle(const QString& title);
    QString title() const { return title_; }
    
    void setIcon(const QString& iconName);
    QString icon() const { return iconName_; }
    
    void setExpanded(bool expanded);
    bool isExpanded() const { return expanded_; }
    
    void toggle();
    
    void setAnimationDuration(int ms);
    int animationDuration() const { return animationDuration_; }
    
    void setExclusiveMode(bool exclusive);
    bool exclusiveMode() const { return exclusiveMode_; }

signals:
    void expandedChanged(bool expanded);
    void toggled();

protected:
    void paintEvent(QPaintEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void onAnimationFinished();

private:
    void setupUI();
    void createHeader();
    void applyStyle();
    void updateExpandIcon();

    QWidget* headerWidget_{nullptr};
    QWidget* contentContainer_{nullptr};
    QWidget* contentWidget_{nullptr};
    QToolButton* expandButton_{nullptr};
    QLabel* titleLabel_{nullptr};
    QLabel* iconLabel_{nullptr};
    QScrollArea* contentScroll_{nullptr};
    
    QParallelAnimationGroup* animationGroup_{nullptr};
    QPropertyAnimation* contentAnimation_{nullptr};
    
    QString title_;
    QString iconName_;
    bool expanded_{false};
    bool exclusiveMode_{true};
    int animationDuration_{250};
    int contentHeight_{0};
};

} // namespace chadvis
