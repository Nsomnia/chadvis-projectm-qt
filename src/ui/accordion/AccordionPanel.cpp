/**
 * @file AccordionPanel.cpp
 */

#include "AccordionPanel.hpp"

#include <QHBoxLayout>
#include <QPainter>
#include <QStyleOption>

namespace vc::ui {

AccordionPanel::AccordionPanel(const QString& title, QWidget* parent)
    : QWidget(parent)
    , title_(title)
{
    setupUI();
}

AccordionPanel::~AccordionPanel() = default;

void AccordionPanel::setupUI() {
    mainLayout_ = new QVBoxLayout(this);
    mainLayout_->setContentsMargins(0, 0, 0, 0);
    mainLayout_->setSpacing(0);
    
    // Header
    headerWidget_ = new QWidget(this);
    headerWidget_->setFixedHeight(headerHeight_);
    headerWidget_->setCursor(Qt::PointingHandCursor);
    headerWidget_->installEventFilter(this);
    
    auto* headerLayout = new QHBoxLayout(headerWidget_);
    headerLayout_->setContentsMargins(12, 0, 12, 0);
    headerLayout_->setSpacing(8);
    
    // Arrow
    arrowButton_ = new QToolButton(this);
    arrowButton_->setFixedSize(20, 20);
    arrowButton_->setStyleSheet(R"(
        QToolButton {
            border: none;
            color: #00bcd4;
            font-size: 16px;
            font-weight: bold;
        }
    )");
    arrowButton_->setText("▶");
    
    // Title
    titleLabel_ = new QLabel(title_, this);
    titleLabel_->setStyleSheet(R"(
        QLabel {
            color: #ffffff;
            font-size: 14px;
            font-weight: 500;
        }
    )");
    
    headerLayout_->addWidget(arrowButton_);
    headerLayout_->addWidget(titleLabel_);
    headerLayout_->addStretch();
    
    // Content container (collapsible)
    contentContainer_ = new QWidget(this);
    contentContainer_->setMaximumHeight(0);
    
    contentLayout_ = new QVBoxLayout(contentContainer_);
    contentLayout_->setContentsMargins(8, 8, 8, 8);
    contentLayout_->setSpacing(4);
    
    mainLayout_->addWidget(headerWidget_);
    mainLayout_->addWidget(contentContainer_);
    
    // Styling
    setStyleSheet(R"(
        AccordionPanel {
            background: #1e1e1e;
            border: 1px solid #333;
            border-radius: 6px;
        }
        AccordionPanel:hover {
            border-color: #00bcd4;
        }
    )");
    
    // Animation group
    animationGroup_ = new QParallelAnimationGroup(this);
    
    heightAnimation_ = new QPropertyAnimation(contentContainer_, "maximumHeight", this);
    heightAnimation_->setDuration(animationDuration_);
    heightAnimation_->setEasingCurve(QEasingCurve::OutCubic);
    animationGroup_->addAnimation(heightAnimation_);
    
    arrowAnimation_ = new QPropertyAnimation(this, "rotation", this);
    arrowAnimation_->setDuration(animationDuration_);
    arrowAnimation_->setEasingCurve(QEasingCurve::OutCubic);
}

void AccordionPanel::setContentWidget(QWidget* content) {
    if (contentWidget_) {
        contentLayout_->removeWidget(contentWidget_);
        contentWidget_->deleteLater();
    }
    
    contentWidget_ = content;
    contentWidget_->setParent(contentContainer_);
    contentLayout_->addWidget(contentWidget_);
    
    // Update content height
    if (contentWidget_) {
        contentHeight_ = contentWidget_->sizeHint().height() + 16;
    }
}

void AccordionPanel::setTitle(const QString& title) {
    title_ = title;
    if (titleLabel_) {
        titleLabel_->setText(title);
    }
}

void AccordionPanel::setIcon(const QString& iconPath) {
    iconPath_ = iconPath;
    // TODO: Load and display SVG icon
}

void AccordionPanel::setExpanded(bool expand) {
    if (expanded_ == expand) return;
    
    expanded_ = expand;
    animateExpansion(expand);
    
    if (expand) {
        emit expanded();
    } else {
        emit collapsed();
    }
    emit toggled(expand);
}

void AccordionPanel::animateExpansion(bool expand) {
    animationGroup_->stop();
    
    if (expand) {
        heightAnimation_->setStartValue(0);
        heightAnimation_->setEndValue(contentHeight_);
        arrowButton_->setText("▼");
    } else {
        heightAnimation_->setStartValue(contentHeight_);
        heightAnimation_->setEndValue(0);
        arrowButton_->setText("▶");
    }
    
    animationGroup_->start();
}

void AccordionPanel::updateArrow() {
    arrowButton_->setText(expanded_ ? "▼" : "▶");
}

bool AccordionPanel::eventFilter(QObject* watched, QEvent* event) {
    if (watched == headerWidget_ && event->type() == QEvent::MouseButtonPress) {
        setExpanded(!expanded_);
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace vc::ui
