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

AccordionPanel* AccordionContainer::addPanel(const QString& id, const QString& title, const QString& /*icon*/, QWidget* content) {
	auto* panel = new AccordionPanel(title, this);
	if (content) {
		panel->setContentWidget(content);
	}
	addPanel(panel);
	panelsById_[id] = panel;
	return panel;
}

void AccordionContainer::addPanel(AccordionPanel* panel) {
	panels_.append(panel);
	layout_->insertWidget(layout_->count() - 1, panel);
	connect(panel, &AccordionPanel::expanded, this, &AccordionContainer::onPanelExpanded);
}

void AccordionContainer::removePanel(AccordionPanel* panel) {
	panels_.removeOne(panel);
	layout_->removeWidget(panel);
	disconnect(panel, nullptr, this, nullptr);
	QString idToRemove;
	for (auto it = panelsById_.begin(); it != panelsById_.end(); ++it) {
		if (it.value() == panel) {
			idToRemove = it.key();
			break;
		}
	}
	if (!idToRemove.isEmpty()) {
		panelsById_.remove(idToRemove);
	}
}

void AccordionContainer::expandPanel(AccordionPanel* panel) {
	panel->setExpanded(true);
}

void AccordionContainer::expandPanel(int index) {
	if (index >= 0 && index < panels_.size()) {
		panels_[index]->setExpanded(true);
	}
}

void AccordionContainer::expandPanel(const QString& id) {
	auto it = panelsById_.find(id);
	if (it != panelsById_.end()) {
		it.value()->setExpanded(true);
	}
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

AccordionPanel* AccordionContainer::panel(const QString& id) const {
	auto it = panelsById_.find(id);
	return it != panelsById_.end() ? it.value() : nullptr;
}

AccordionPanel* AccordionContainer::panelAt(int index) const {
	return (index >= 0 && index < panels_.size()) ? panels_[index] : nullptr;
}

} // namespace vc::ui
