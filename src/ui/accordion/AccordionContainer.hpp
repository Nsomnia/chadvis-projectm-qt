/**
 * @file AccordionContainer.hpp
 * @brief Modern accordion container for collapsible panels
 */

#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QList>
#include <QMap>
#include <memory>

namespace vc::ui {

class AccordionPanel;

class AccordionContainer : public QWidget {
    Q_OBJECT

public:
    explicit AccordionContainer(QWidget* parent = nullptr);
    ~AccordionContainer() override;

	AccordionPanel* addPanel(const QString& id, const QString& title, const QString& icon, QWidget* content = nullptr);
	void addPanel(AccordionPanel* panel);
	void removePanel(AccordionPanel* panel);

	void expandPanel(AccordionPanel* panel);
	void expandPanel(int index);
	void expandPanel(const QString& id);
	void collapsePanel(AccordionPanel* panel);
	void collapseAll();

	AccordionPanel* panel(const QString& id) const;
	AccordionPanel* panelAt(int index) const;
	int panelCount() const { return panels_.size(); }
    
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
	QMap<QString, AccordionPanel*> panelsById_;
	bool singleExpansion_ = true;
	int animationDuration_ = 200;
};

} // namespace vc::ui
