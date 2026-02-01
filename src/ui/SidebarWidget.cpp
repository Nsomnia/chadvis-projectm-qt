#include "SidebarWidget.hpp"
#include <QToolButton>
#include <QScrollArea>
#include <QPropertyAnimation>
#include <QStyle>

namespace chadvis {

SidebarWidget::SidebarWidget(QWidget* parent)
    : QWidget(parent) {
    setupUI();
    updateStyling();
}

SidebarWidget::~SidebarWidget() = default;

void SidebarWidget::setupUI() {
    setObjectName("SidebarWidget");
    
    mainLayout_ = new QHBoxLayout(this);
    mainLayout_->setContentsMargins(0, 0, 0, 0);
    mainLayout_->setSpacing(0);
    
    // Navigation container
    navContainer_ = new QWidget(this);
    navContainer_->setObjectName("navContainer");
    navLayout_ = new QVBoxLayout(navContainer_);
    navLayout_->setContentsMargins(4, 8, 4, 8);
    navLayout_->setSpacing(4);
    
    // Toggle button at top
    toggleButton_ = new QPushButton(this);
    toggleButton_->setObjectName("toggleButton");
    toggleButton_->setFixedSize(48, 48);
    toggleButton_->setCursor(Qt::PointingHandCursor);
    toggleButton_->setToolTip("Collapse/Expand");
    connect(toggleButton_, &QPushButton::clicked, this, &SidebarWidget::toggleExpanded);
    navLayout_->addWidget(toggleButton_, 0, Qt::AlignHCenter);
    
    // Separator
    navLayout_->addSpacing(8);
    
    // Navigation buttons area
    navScroll_ = new QScrollArea(this);
    navScroll_->setWidgetResizable(true);
    navScroll_->setFrameShape(QFrame::NoFrame);
    navScroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    navScroll_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    
    auto* scrollWidget = new QWidget();
    navButtonsLayout_ = new QVBoxLayout(scrollWidget);
    navButtonsLayout_->setContentsMargins(0, 0, 0, 0);
    navButtonsLayout_->setSpacing(4);
    navButtonsLayout_->addStretch();
    
    navScroll_->setWidget(scrollWidget);
    navLayout_->addWidget(navScroll_, 1);
    
    // Content area (stacked widget)
    contentArea_ = new QStackedWidget(this);
    contentArea_->setObjectName("contentArea");
    
    mainLayout_->addWidget(navContainer_);
    mainLayout_->addWidget(contentArea_, 1);
    
    setLayout(mainLayout_);
    setFixedWidth(expandedWidth_);
    
    // Animation setup
    widthAnimation_ = new QPropertyAnimation(this, "minimumWidth");
    widthAnimation_->setDuration(200);
    widthAnimation_->setEasingCurve(QEasingCurve::OutCubic);
    connect(widthAnimation_, &QPropertyAnimation::finished, this, &SidebarWidget::animateExpansion);
}

void SidebarWidget::addPanel(const QString& id, const QString& label, 
                              const QString& iconName, QWidget* widget) {
    NavItem item;
    item.id = id;
    item.label = label;
    item.iconName = iconName;
    item.widget = widget;
    
    createNavButton(item);
    navItems_.append(item);
    
    contentArea_->addWidget(widget);
    
    if (navItems_.size() == 1) {
        switchToPanel(id);
    }
}

void SidebarWidget::createNavButton(NavItem& item) {
    auto* button = new QToolButton(navContainer_);
    button->setObjectName("navButton_" + item.id);
    button->setCheckable(true);
    button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    button->setText(item.label);
    button->setIcon(QIcon::fromTheme(item.iconName, QIcon::fromTheme("application-x-executable")));
    button->setIconSize(QSize(24, 24));
    button->setFixedSize(52, 52);
    button->setCursor(Qt::PointingHandCursor);
    button->setToolTip(item.label);
    button->setProperty("panelId", item.id);
    
    connect(button, &QToolButton::clicked, this, &SidebarWidget::onNavButtonClicked);
    
    item.button = button;
    
    navButtonsLayout_->insertWidget(navButtonsLayout_->count() - 1, button);
}

void SidebarWidget::switchToPanel(const QString& id) {
    for (int i = 0; i < navItems_.size(); ++i) {
        if (navItems_[i].id == id) {
            contentArea_->setCurrentIndex(i);
            navItems_[i].button->setChecked(true);
            currentPanelId_ = id;
            emit panelChanged(id);
            return;
        }
    }
}

QString SidebarWidget::currentPanel() const {
    return currentPanelId_;
}

void SidebarWidget::onNavButtonClicked() {
    auto* button = qobject_cast<QToolButton*>(sender());
    if (!button) return;
    
    QString panelId = button->property("panelId").toString();
    switchToPanel(panelId);
}

void SidebarWidget::setExpanded(bool expanded) {
    if (isExpanded_ == expanded) return;
    
    isExpanded_ = expanded;
    
    if (widthAnimation_->state() == QPropertyAnimation::Running) {
        widthAnimation_->stop();
    }
    
    widthAnimation_->setStartValue(width());
    widthAnimation_->setEndValue(expanded ? expandedWidth_ : collapsedWidth_);
    widthAnimation_->start();
    
    toggleButton_->setIcon(QIcon::fromTheme(
        expanded ? "go-previous" : "go-next",
        style()->standardIcon(expanded ? QStyle::SP_ArrowLeft : QStyle::SP_ArrowRight)
    ));
    
    for (const auto& item : navItems_) {
        item.button->setToolButtonStyle(
            expanded ? Qt::ToolButtonTextUnderIcon : Qt::ToolButtonIconOnly
        );
        item.button->setText(expanded ? item.label : "");
    }
    
    emit expansionChanged(expanded);
}

void SidebarWidget::toggleExpanded() {
    setExpanded(!isExpanded_);
}

void SidebarWidget::setExpandedWidth(int width) {
    expandedWidth_ = width;
    if (isExpanded_) {
        setFixedWidth(width);
    }
}

void SidebarWidget::setCollapsedWidth(int width) {
    collapsedWidth_ = width;
    if (!isExpanded_) {
        setFixedWidth(width);
    }
}

void SidebarWidget::animateExpansion() {
    setFixedWidth(widthAnimation_->endValue().toInt());
}

void SidebarWidget::applyTheme(const QString& themeName) {
    currentTheme_ = themeName;
    updateStyling();
}

void SidebarWidget::updateStyling() {
    const QString styleSheet = getStyleSheet(currentTheme_);
    setStyleSheet(styleSheet);
    
    toggleButton_->setIcon(QIcon::fromTheme(
        isExpanded_ ? "go-previous" : "go-next",
        style()->standardIcon(isExpanded_ ? QStyle::SP_ArrowLeft : QStyle::SP_ArrowRight)
    ));
}

QString SidebarWidget::getStyleSheet(const QString& themeName) {
    if (themeName == "dark" || themeName.isEmpty()) {
        return R"(
            #SidebarWidget {
                background-color: #2d2d2d;
                border-right: 1px solid #3d3d3d;
            }
            #navContainer {
                background-color: #2d2d2d;
            }
            QToolButton {
                background-color: transparent;
                border: 2px solid transparent;
                border-radius: 8px;
                color: #b0b0b0;
                font-size: 10px;
                font-weight: 500;
            }
            QToolButton:hover {
                background-color: #3d3d3d;
                color: #ffffff;
            }
            QToolButton:checked {
                background-color: #0d47a1;
                border-color: #1976d2;
                color: #ffffff;
            }
            #toggleButton {
                background-color: transparent;
                border: 2px solid transparent;
                border-radius: 8px;
                color: #b0b0b0;
            }
            #toggleButton:hover {
                background-color: #3d3d3d;
                color: #ffffff;
            }
            QScrollArea {
                border: none;
                background-color: transparent;
            }
        )";
    } else if (themeName == "nord") {
        return R"(
            #SidebarWidget {
                background-color: #2e3440;
                border-right: 1px solid #4c566a;
            }
            #navContainer {
                background-color: #2e3440;
            }
            QToolButton {
                background-color: transparent;
                border: 2px solid transparent;
                border-radius: 8px;
                color: #d8dee9;
                font-size: 10px;
                font-weight: 500;
            }
            QToolButton:hover {
                background-color: #434c5e;
                color: #eceff4;
            }
            QToolButton:checked {
                background-color: #5e81ac;
                border-color: #81a1c1;
                color: #eceff4;
            }
            #toggleButton {
                background-color: transparent;
                border: 2px solid transparent;
                border-radius: 8px;
                color: #d8dee9;
            }
            #toggleButton:hover {
                background-color: #434c5e;
                color: #eceff4;
            }
            QScrollArea {
                border: none;
                background-color: transparent;
            }
        )";
    } else if (themeName == "gruvbox") {
        return R"(
            #SidebarWidget {
                background-color: #282828;
                border-right: 1px solid #3c3836;
            }
            #navContainer {
                background-color: #282828;
            }
            QToolButton {
                background-color: transparent;
                border: 2px solid transparent;
                border-radius: 8px;
                color: #a89984;
                font-size: 10px;
                font-weight: 500;
            }
            QToolButton:hover {
                background-color: #3c3836;
                color: #ebdbb2;
            }
            QToolButton:checked {
                background-color: #458588;
                border-color: #83a598;
                color: #ebdbb2;
            }
            #toggleButton {
                background-color: transparent;
                border: 2px solid transparent;
                border-radius: 8px;
                color: #a89984;
            }
            #toggleButton:hover {
                background-color: #3c3836;
                color: #ebdbb2;
            }
            QScrollArea {
                border: none;
                background-color: transparent;
            }
        )";
    }
    
    return QString();
}

} // namespace chadvis
