#include "AccordionSection.hpp"
#include <QScrollArea>
#include <QPainter>
#include <QStyleOption>
#include <QMouseEvent>

namespace chadvis {

AccordionSection::AccordionSection(QWidget* parent)
    : QWidget(parent) {
    setupUI();
}

AccordionSection::AccordionSection(const QString& title, QWidget* parent)
    : QWidget(parent), title_(title) {
    setupUI();
    setTitle(title);
}

AccordionSection::~AccordionSection() = default;

void AccordionSection::setupUI() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    
    createHeader();
    mainLayout->addWidget(headerWidget_);
    
    contentContainer_ = new QWidget(this);
    contentContainer_->setObjectName("accordionContent");
    contentContainer_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    
    auto* contentLayout = new QVBoxLayout(contentContainer_);
    contentLayout->setContentsMargins(8, 0, 8, 8);
    contentLayout->setSpacing(0);
    
    contentScroll_ = new QScrollArea(this);
    contentScroll_->setWidgetResizable(true);
    contentScroll_->setFrameShape(QFrame::NoFrame);
    contentScroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    contentScroll_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    contentScroll_->setWidget(contentContainer_);
    contentScroll_->setMaximumHeight(0);
    
    mainLayout->addWidget(contentScroll_);
    
    animationGroup_ = new QParallelAnimationGroup(this);
    
    contentAnimation_ = new QPropertyAnimation(contentScroll_, "maximumHeight");
    contentAnimation_->setDuration(animationDuration_);
    contentAnimation_->setEasingCurve(QEasingCurve::OutCubic);
    animationGroup_->addAnimation(contentAnimation_);
    
    connect(animationGroup_, &QParallelAnimationGroup::finished,
            this, &AccordionSection::onAnimationFinished);
    
    applyStyle();
}

void AccordionSection::createHeader() {
    headerWidget_ = new QWidget(this);
    headerWidget_->setObjectName("accordionHeader");
    headerWidget_->setFixedHeight(48);
    headerWidget_->setCursor(Qt::PointingHandCursor);
    
    auto* headerLayout = new QHBoxLayout(headerWidget_);
    headerLayout->setContentsMargins(12, 8, 12, 8);
    headerLayout->setSpacing(12);
    
    iconLabel_ = new QLabel(headerWidget_);
    iconLabel_->setFixedSize(24, 24);
    iconLabel_->setAlignment(Qt::AlignCenter);
    headerLayout->addWidget(iconLabel_);
    
    titleLabel_ = new QLabel(headerWidget_);
    titleLabel_->setObjectName("accordionTitle");
    titleLabel_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    headerLayout->addWidget(titleLabel_);
    
    expandButton_ = new QToolButton(headerWidget_);
    expandButton_->setObjectName("expandButton");
    expandButton_->setFixedSize(24, 24);
    expandButton_->setAutoRaise(true);
    updateExpandIcon();
    headerLayout->addWidget(expandButton_);
    
    headerWidget_->installEventFilter(this);
}

void AccordionSection::setContentWidget(QWidget* content) {
    if (contentWidget_) {
        contentWidget_->deleteLater();
    }
    
    contentWidget_ = content;
    if (contentWidget_) {
        auto* layout = contentContainer_->layout();
        layout->addWidget(contentWidget_);
        
        contentWidget_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        contentHeight_ = contentWidget_->sizeHint().height() + 16;
    }
}

void AccordionSection::setTitle(const QString& title) {
    title_ = title;
    if (titleLabel_) {
        titleLabel_->setText(title);
    }
}

void AccordionSection::setIcon(const QString& iconName) {
    iconName_ = iconName;
    if (iconLabel_) {
        QIcon icon = QIcon::fromTheme(iconName);
        if (!icon.isNull()) {
            iconLabel_->setPixmap(icon.pixmap(24, 24));
        }
    }
}

void AccordionSection::setExpanded(bool expanded) {
    if (expanded_ == expanded) return;
    
    expanded_ = expanded;
    
    if (animationGroup_->state() == QAbstractAnimation::Running) {
        animationGroup_->stop();
    }
    
    int startHeight = contentScroll_->maximumHeight();
    int endHeight = expanded ? contentHeight_ : 0;
    
    contentAnimation_->setStartValue(startHeight);
    contentAnimation_->setEndValue(endHeight);
    
    updateExpandIcon();
    animationGroup_->start();
    
    emit expandedChanged(expanded);
}

void AccordionSection::toggle() {
    setExpanded(!expanded_);
    emit toggled();
}

void AccordionSection::setAnimationDuration(int ms) {
    animationDuration_ = ms;
    if (contentAnimation_) {
        contentAnimation_->setDuration(ms);
    }
}

void AccordionSection::setExclusiveMode(bool exclusive) {
    exclusiveMode_ = exclusive;
}

void AccordionSection::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event)
    QStyleOption opt;
    opt.initFrom(this);
    QPainter p(this);
    style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);
}

bool AccordionSection::eventFilter(QObject* watched, QEvent* event) {
    if (watched == headerWidget_ && event->type() == QEvent::MouseButtonRelease) {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() == Qt::LeftButton) {
            toggle();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void AccordionSection::onAnimationFinished() {
    if (expanded_) {
        contentScroll_->setMaximumHeight(QWIDGETSIZE_MAX);
    }
}

void AccordionSection::applyStyle() {
    setStyleSheet(R"(
        #accordionHeader {
            background-color: #3a3a3a;
            border-radius: 8px;
        }
        #accordionHeader:hover {
            background-color: #4a4a4a;
        }
        #accordionTitle {
            color: #e0e0e0;
            font-size: 14px;
            font-weight: 500;
        }
        #expandButton {
            background: transparent;
            border: none;
            color: #b0b0b0;
        }
        #expandButton:hover {
            color: #ffffff;
        }
        #accordionContent {
            background-color: transparent;
        }
        QScrollArea {
            background-color: transparent;
            border: none;
        }
    )");
}

void AccordionSection::updateExpandIcon() {
    if (expandButton_) {
        QString arrowIcon = expanded_ ? "go-down" : "go-next";
        QIcon icon = QIcon::fromTheme(arrowIcon);
        if (icon.isNull()) {
            expandButton_->setArrowType(expanded_ ? Qt::DownArrow : Qt::RightArrow);
        } else {
            expandButton_->setIcon(icon);
        }
    }
}

} // namespace chadvis
