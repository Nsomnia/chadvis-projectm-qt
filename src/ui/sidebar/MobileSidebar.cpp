#include "MobileSidebar.hpp"
#include "AccordionSection.hpp"
#include <QGesture>
#include <QPainter>
#include <QStyleOption>
#include <QToolButton>
#include <QLabel>

namespace chadvis {

MobileSidebar::MobileSidebar(QWidget* parent)
    : QWidget(parent) {
    setupUI();
    applyTheme("dark");
}

MobileSidebar::~MobileSidebar() = default;

void MobileSidebar::setupUI() {
    setObjectName("MobileSidebar");
    setAttribute(Qt::WA_AcceptTouchEvents);
    grabGesture(Qt::SwipeGesture);
    
    mainLayout_ = new QVBoxLayout(this);
    mainLayout_->setContentsMargins(8, 8, 8, 8);
    mainLayout_->setSpacing(8);
    
    createHeader();
    mainLayout_->addWidget(headerWidget_);
    
    scrollArea_ = new QScrollArea(this);
    scrollArea_->setObjectName("sectionScrollArea");
    scrollArea_->setWidgetResizable(true);
    scrollArea_->setFrameShape(QFrame::NoFrame);
    scrollArea_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    
    container_ = new QWidget(scrollArea_);
    container_->setObjectName("sectionContainer");
    sectionLayout_ = new QVBoxLayout(container_);
    sectionLayout_->setContentsMargins(0, 0, 0, 0);
    sectionLayout_->setSpacing(4);
    sectionLayout_->addStretch();
    
    scrollArea_->setWidget(container_);
    mainLayout_->addWidget(scrollArea_, 1);
    
    widthAnimation_ = new QPropertyAnimation(this, "minimumWidth");
    widthAnimation_->setDuration(250);
    widthAnimation_->setEasingCurve(QEasingCurve::OutCubic);
    
    setFixedWidth(expandedWidth_);
}

void MobileSidebar::createHeader() {
    headerWidget_ = new QWidget(this);
    headerWidget_->setObjectName("sidebarHeader");
    headerWidget_->setFixedHeight(56);
    
    auto* headerLayout = new QHBoxLayout(headerWidget_);
    headerLayout_->setContentsMargins(8, 8, 8, 8);
    headerLayout_->setSpacing(12);
    
    auto* toggleBtn = new QToolButton(headerWidget_);
    toggleBtn->setObjectName("toggleButton");
    toggleBtn->setIcon(QIcon::fromTheme("application-menu"));
    toggleBtn->setIconSize(QSize(24, 24));
    toggleBtn->setFixedSize(40, 40);
    toggleBtn->setAutoRaise(true);
    toggleBtn->setToolTip("Toggle sidebar");
    connect(toggleBtn, &QToolButton::clicked, this, &MobileSidebar::toggleCollapse);
    headerLayout->addWidget(toggleBtn);
    
    auto* titleLbl = new QLabel("ChadVis", headerWidget_);
    titleLbl->setObjectName("sidebarTitle");
    titleLbl->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    headerLayout->addWidget(titleLbl);
    
    auto* collapseBtn = new QToolButton(headerWidget_);
    collapseBtn->setObjectName("collapseButton");
    collapseBtn->setIcon(QIcon::fromTheme("go-previous"));
    collapseBtn->setIconSize(QSize(20, 20));
    collapseBtn->setFixedSize(32, 32);
    collapseBtn->setAutoRaise(true);
    collapseBtn->setToolTip("Collapse sidebar");
    connect(collapseBtn, &QToolButton::clicked, this, &MobileSidebar::toggleCollapse);
    headerLayout->addWidget(collapseBtn);
}

void MobileSidebar::addSection(const QString& id, 
                               const QString& title, 
                               const QString& iconName, 
                               QWidget* widget) {
    if (hasSection(id)) {
        return;
    }
    
    SidebarSection section;
    section.id = id;
    section.title = title;
    section.iconName = iconName;
    section.widget = widget;
    
    createSectionWidget(section);
    sections_.push_back(section);
    
    if (sections_.size() == 1) {
        setCurrentSection(id);
    }
}

void MobileSidebar::createSectionWidget(SidebarSection& section) {
    auto* accordion = new AccordionSection(section.title, this);
    accordion->setIcon(section.iconName);
    accordion->setContentWidget(section.widget);
    accordion->setExclusiveMode(true);
    
    connect(accordion, &AccordionSection::toggled,
            this, &MobileSidebar::onSectionToggled);
    
    section.section = accordion;
    sectionLayout_->insertWidget(sectionLayout_->count() - 1, accordion);
}

void MobileSidebar::removeSection(const QString& id) {
    auto it = std::find_if(sections_.begin(), sections_.end(),
        [&id](const SidebarSection& s) { return s.id == id; });
    
    if (it != sections_.end()) {
        if (it->section) {
            it->section->deleteLater();
        }
        sections_.erase(it);
    }
}

void MobileSidebar::expandSection(const QString& id) {
    auto* section = const_cast<SidebarSection*>(std::find_if(
        sections_.begin(), sections_.end(),
        [&id](const SidebarSection& s) { return s.id == id; }
    ).base() - (sections_.size() > 0 ? 1 : 0));
    
    if (section && section->section) {
        section->section->setExpanded(true);
        currentSectionId_ = id;
        emit sectionExpanded(id);
        emit sectionChanged(id);
    }
}

void MobileSidebar::collapseSection(const QString& id) {
    for (auto& section : sections_) {
        if (section.id == id && section.section) {
            section.section->setExpanded(false);
            emit sectionCollapsed(id);
            break;
        }
    }
}

void MobileSidebar::collapseAll() {
    for (auto& section : sections_) {
        if (section.section) {
            section.section->setExpanded(false);
        }
    }
}

void MobileSidebar::setCurrentSection(const QString& id) {
    if (!hasSection(id)) return;
    
    for (auto& section : sections_) {
        if (section.section) {
            section.section->setExpanded(section.id == id);
        }
    }
    currentSectionId_ = id;
    emit sectionChanged(id);
}

void MobileSidebar::setCompactMode(bool compact) {
    if (compactMode_ == compact) return;
    
    compactMode_ = compact;
    
    for (auto& section : sections_) {
        if (section.section) {
            section.section->setTitle(compact ? "" : section.title);
        }
    }
    
    if (compact) {
        animateWidth(collapsedWidth_);
    } else {
        animateWidth(expandedWidth_);
    }
    
    emit compactModeChanged(compact);
}

void MobileSidebar::toggleCollapse() {
    setCompactMode(!compactMode_);
}

void MobileSidebar::toggleSection(const QString& id) {
    for (auto& section : sections_) {
        if (section.id == id && section.section) {
            section.section->toggle();
            break;
        }
    }
}

bool MobileSidebar::hasSection(const QString& id) const {
    return std::any_of(sections_.begin(), sections_.end(),
        [&id](const SidebarSection& s) { return s.id == id; });
}

void MobileSidebar::onSectionToggled() {
    auto* sender = qobject_cast<AccordionSection*>(QObject::sender());
    if (!sender) return;
    
    for (auto& section : sections_) {
        if (section.section == sender) {
            if (sender->isExpanded()) {
                for (auto& other : sections_) {
                    if (other.section && other.section != sender && other.section->exclusiveMode()) {
                        other.section->setExpanded(false);
                    }
                }
                currentSectionId_ = section.id;
                emit sectionExpanded(section.id);
                emit sectionChanged(section.id);
            } else {
                emit sectionCollapsed(section.id);
            }
            break;
        }
    }
}

void MobileSidebar::animateWidth(int targetWidth) {
    if (widthAnimation_->state() == QAbstractAnimation::Running) {
        widthAnimation_->stop();
    }
    
    widthAnimation_->setStartValue(width());
    widthAnimation_->setEndValue(targetWidth);
    widthAnimation_->start();
}

bool MobileSidebar::event(QEvent* event) {
    if (event->type() == QEvent::Gesture) {
        return gestureEvent(static_cast<QGestureEvent*>(event));
    }
    return QWidget::event(event);
}

bool MobileSidebar::gestureEvent(QGestureEvent* event) {
    if (QGesture* swipe = event->gesture(Qt::SwipeGesture)) {
        swipeGesture(static_cast<QSwipeGesture*>(swipe));
    }
    return true;
}

void MobileSidebar::swipeGesture(QSwipeGesture* gesture) {
    if (gesture->state() == Qt::GestureFinished) {
        if (gesture->horizontalDirection() == QSwipeGesture::Left) {
            setCompactMode(true);
        } else if (gesture->horizontalDirection() == QSwipeGesture::Right) {
            setCompactMode(false);
        }
    }
}

void MobileSidebar::applyTheme(const QString& themeName) {
    currentTheme_ = themeName;
    setStyleSheet(getStyleSheet(themeName));
}

QString MobileSidebar::getStyleSheet(const QString& themeName) {
    if (themeName == "dark" || themeName.isEmpty()) {
        return R"(
            #MobileSidebar {
                background-color: #1e1e1e;
                border-right: 1px solid #3d3d3d;
            }
            #sidebarHeader {
                background-color: #2d2d2d;
                border-radius: 8px;
            }
            #sidebarTitle {
                color: #e0e0e0;
                font-size: 18px;
                font-weight: 600;
            }
            #toggleButton, #collapseButton {
                background: transparent;
                border: none;
                color: #b0b0b0;
                border-radius: 8px;
            }
            #toggleButton:hover, #collapseButton:hover {
                background-color: #3d3d3d;
                color: #ffffff;
            }
            #sectionScrollArea {
                background-color: transparent;
                border: none;
            }
            #sectionContainer {
                background-color: transparent;
            }
            QScrollBar:vertical {
                background-color: #2d2d2d;
                width: 8px;
                border-radius: 4px;
            }
            QScrollBar::handle:vertical {
                background-color: #5d5d5d;
                border-radius: 4px;
                min-height: 20px;
            }
            QScrollBar::handle:vertical:hover {
                background-color: #7d7d7d;
            }
        )";
    }
    
    if (themeName == "nord") {
        return R"(
            #MobileSidebar {
                background-color: #2e3440;
                border-right: 1px solid #4c566a;
            }
            #sidebarHeader {
                background-color: #3b4252;
                border-radius: 8px;
            }
            #sidebarTitle {
                color: #eceff4;
                font-size: 18px;
                font-weight: 600;
            }
            #toggleButton, #collapseButton {
                background: transparent;
                border: none;
                color: #d8dee9;
                border-radius: 8px;
            }
            #toggleButton:hover, #collapseButton:hover {
                background-color: #434c5e;
                color: #eceff4;
            }
            QScrollBar:vertical {
                background-color: #3b4252;
                width: 8px;
                border-radius: 4px;
            }
            QScrollBar::handle:vertical {
                background-color: #5e81ac;
                border-radius: 4px;
            }
        )";
    }
    
    return QString();
}

} // namespace chadvis
