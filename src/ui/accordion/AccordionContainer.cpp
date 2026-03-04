/**
 * @file AccordionContainer.cpp
 */

#include "AccordionContainer.hpp"
#include "AccordionPanel.hpp"

namespace vc::ui {

AccordionContainer::AccordionContainer(QWidget* parent)
    : QWidget(parent)
    , layout_(new QVBoxLayout(this))
{
    layout_->setContentsMargins(0, 0, 0, 0);
    layout_->setSpacing(2);
    layout_->addStretch();
    
    setStyleSheet(R"(
        AccordionContainer {
            background: transparent;
        }
    )");
}

AccordionContainer::~AccordionContainer() = default;

void AccordionContainer::addPanel(AccordionPanel* panel) {
    panels_.append(panel);
    layout_->insertWidget(layout_->count() - 1, panel);
    connect(panel, &AccordionPanel::expanded, this, &AccordionContainer::onPanelExpanded);
}

void AccordionContainer::removePanel(AccordionPanel* panel) {
    panels_.removeOne(panel);
    layout_->removeWidget(panel);
    disconnect(panel, nullptr, this, nullptr);
}

void AccordionContainer::expandPanel(AccordionPanel* panel) {
    panel->setExpanded(true);
}

void AccordionContainer::collapsePanel(AccordionPanel* panel) {
    panel->setExpanded(false);
}

void AccordionContainer::collapseAll() {
    for (auto* p : panels_) {
        p->setExpanded(false);
    }
}

void AccordionContainer::onPanelExpanded() {
    if (!singleExpansion_) return;
    
    auto* senderPanel = qobject_cast<AccordionPanel*>(sender());
    if (!senderPanel) return;
    
    for (auto* p : panels_) {
        if (p != senderPanel && p->isExpanded()) {
            p->setExpanded(false);
        }
    }
    
    emit panelExpanded(senderPanel);
}

} // namespace vc::ui
