/**
 * @file AccordionContainer.hpp
 * @brief Modern accordion container for collapsible panels
 */

#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QList>
#include <memory>

class AccordionPanel;

namespace vc::ui {

class AccordionContainer : public QWidget {
    Q_OBJECT

public:
    explicit AccordionContainer(QWidget* parent = nullptr);
    ~AccordionContainer() override;

    void addPanel(AccordionPanel* panel);
    void removePanel(AccordionPanel* panel);
    
    void expandPanel(AccordionPanel* panel);
    void collapsePanel(AccordionPanel* panel);
    void collapseAll();
    
    void setSingleExpansion(bool single) { singleExpansion_ = single; }
    bool singleExpansion() const { return singleExpansion_; }
    
    void setAnimationDuration(int ms) { animationDuration_ = ms; }
    int animationDuration() const { return animationDuration_; }

signals:
    void panelExpanded(AccordionPanel* panel);
    void panelCollapsed(AccordionPanel* panel);

private slots:
    void onPanelExpanded();

private:
    QVBoxLayout* layout_;
    QList<AccordionPanel*> panels_;
    bool singleExpansion_ = true;
    int animationDuration_ = 200;
};

} // namespace vc::ui
