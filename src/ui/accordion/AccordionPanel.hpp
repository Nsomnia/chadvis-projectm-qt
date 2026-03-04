/**
 * @file AccordionPanel.hpp
 * @brief Collapsible accordion panel with header and content
 */

#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QPropertyAnimation>
#include <QParallelAnimationGroup>

namespace vc::ui {

class AccordionPanel : public QWidget {
    Q_OBJECT
    Q_PROPERTY(bool expanded READ isExpanded WRITE setExpanded)

public:
    explicit AccordionPanel(const QString& title, QWidget* parent = nullptr);
    ~AccordionPanel() override;

    void setContentWidget(QWidget* content);
    QWidget* contentWidget() const { return contentWidget_; }
    
    void setTitle(const QString& title);
    QString title() const { return title_; }
    
    void setIcon(const QString& iconPath);
    
    bool isExpanded() const { return expanded_; }
    void setExpanded(bool expanded);
    
    void setHeaderHeight(int height) { headerHeight_ = height; }
    int headerHeight() const { return headerHeight_; }
    
    void setContentHeight(int height) { contentHeight_ = height; }
    int contentHeight() const { return contentHeight_; }
    
    void setAnimationDuration(int ms) { animationDuration_ = ms; }

signals:
    void expanded();
    void collapsed();
    void toggled(bool expanded);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void setupUI();
    void updateArrow();
    void animateExpansion(bool expand);

    QVBoxLayout* mainLayout_{nullptr};
    QWidget* headerWidget_{nullptr};
    QWidget* contentContainer_{nullptr};
    QVBoxLayout* contentLayout_{nullptr};
    QLabel* titleLabel_{nullptr};
    QToolButton* arrowButton_{nullptr};
    QWidget* contentWidget_{nullptr};
    
    QString title_;
    QString iconPath_;
    bool expanded_{false};
    int headerHeight_{48};
    int contentHeight_{200};
    int animationDuration_{200};
    
    QPropertyAnimation* heightAnimation_{nullptr};
    QPropertyAnimation* arrowAnimation_{nullptr};
    QParallelAnimationGroup* animationGroup_{nullptr};
};

} // namespace vc::ui
