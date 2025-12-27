#include "OverlayEditor.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QScrollArea>
#include <QFontDatabase>
#include <QLabel>

namespace vc {

OverlayEditor::OverlayEditor(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
}

void OverlayEditor::setOverlayEngine(OverlayEngine* engine) {
    overlayEngine_ = engine;
    refresh();
}

void OverlayEditor::setupUI() {
    auto* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    
    // Left: Element list
    auto* listGroup = new QGroupBox("Elements");
    auto* listLayout = new QVBoxLayout(listGroup);
    
    elementList_ = new QListWidget();
    connect(elementList_, &QListWidget::currentRowChanged,
            this, &OverlayEditor::onElementSelected);
    listLayout->addWidget(elementList_);
    
    auto* listButtons = new QHBoxLayout();
    addButton_ = new QPushButton("+");
    addButton_->setFixedWidth(30);
    connect(addButton_, &QPushButton::clicked, this, &OverlayEditor::onAddElement);
    listButtons->addWidget(addButton_);
    
    removeButton_ = new QPushButton("-");
    removeButton_->setFixedWidth(30);
    connect(removeButton_, &QPushButton::clicked, this, &OverlayEditor::onRemoveElement);
    listButtons->addWidget(removeButton_);
    listButtons->addStretch();
    listLayout->addLayout(listButtons);
    
    listGroup->setMaximumWidth(200);
    mainLayout->addWidget(listGroup);
    
    // Right: Properties
    auto* scrollArea = new QScrollArea();
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    
    auto* propsWidget = new QWidget();
    auto* propsLayout = new QVBoxLayout(propsWidget);
    
    // Basic properties
    auto* basicGroup = new QGroupBox("Basic");
    auto* basicLayout = new QFormLayout(basicGroup);
    
    idEdit_ = new QLineEdit();
    idEdit_->setReadOnly(true);
    basicLayout->addRow("ID:", idEdit_);
    
    textEdit_ = new QLineEdit();
    connect(textEdit_, &QLineEdit::textChanged, this, &OverlayEditor::onTextChanged);
    basicLayout->addRow("Text:", textEdit_);
    
    visibleCheck_ = new QCheckBox("Visible");
    visibleCheck_->setChecked(true);
    connect(visibleCheck_, &QCheckBox::toggled, this, &OverlayEditor::onVisibilityChanged);
    basicLayout->addRow("", visibleCheck_);
    
    propsLayout->addWidget(basicGroup);
    
    // Position
    auto* posGroup = new QGroupBox("Position");
    auto* posLayout = new QFormLayout(posGroup);
    
    auto* posXYLayout = new QHBoxLayout();
    posXSpin_ = new QDoubleSpinBox();
    posXSpin_->setRange(0, 1);
    posXSpin_->setSingleStep(0.01);
    posXSpin_->setDecimals(3);
    connect(posXSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &OverlayEditor::onPositionChanged);
    posXYLayout->addWidget(new QLabel("X:"));
    posXYLayout->addWidget(posXSpin_);
    
    posYSpin_ = new QDoubleSpinBox();
    posYSpin_->setRange(0, 1);
    posYSpin_->setSingleStep(0.01);
    posYSpin_->setDecimals(3);
    connect(posYSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &OverlayEditor::onPositionChanged);
    posXYLayout->addWidget(new QLabel("Y:"));
    posXYLayout->addWidget(posYSpin_);
    posLayout->addRow("Position:", posXYLayout);
    
    anchorCombo_ = new QComboBox();
    anchorCombo_->addItems({"Top Left", "Top Center", "Top Right",
                           "Center Left", "Center", "Center Right",
                           "Bottom Left", "Bottom Center", "Bottom Right"});
    connect(anchorCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &OverlayEditor::onPositionChanged);
    posLayout->addRow("Anchor:", anchorCombo_);
    
    propsLayout->addWidget(posGroup);
    
    // Style
    auto* styleGroup = new QGroupBox("Style");
    auto* styleLayout = new QFormLayout(styleGroup);
    
    fontCombo_ = new QComboBox();
    fontCombo_->addItems(QFontDatabase::families());
    fontCombo_->setCurrentText("Liberation Sans");
    connect(fontCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &OverlayEditor::onStyleChanged);
    styleLayout->addRow("Font:", fontCombo_);
    
    fontSizeSpin_ = new QSpinBox();
    fontSizeSpin_->setRange(8, 200);
    fontSizeSpin_->setValue(32);
    connect(fontSizeSpin_, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &OverlayEditor::onStyleChanged);
    styleLayout->addRow("Size:", fontSizeSpin_);
    
    auto* colorLayout = new QHBoxLayout();
    colorButton_ = new QPushButton();
    colorButton_->setFixedSize(60, 24);
    setColorButtonColor(Qt::white);
    connect(colorButton_, &QPushButton::clicked, this, &OverlayEditor::onColorButtonClicked);
    colorLayout->addWidget(colorButton_);
    colorLayout->addStretch();
    styleLayout->addRow("Color:", colorLayout);
    
    opacitySpin_ = new QDoubleSpinBox();
    opacitySpin_->setRange(0, 1);
    opacitySpin_->setSingleStep(0.1);
    opacitySpin_->setValue(1.0);
    connect(opacitySpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &OverlayEditor::onStyleChanged);
    styleLayout->addRow("Opacity:", opacitySpin_);
    
    auto* styleFlags = new QHBoxLayout();
    boldCheck_ = new QCheckBox("Bold");
    connect(boldCheck_, &QCheckBox::toggled, this, &OverlayEditor::onStyleChanged);
    styleFlags->addWidget(boldCheck_);
    
    italicCheck_ = new QCheckBox("Italic");
    connect(italicCheck_, &QCheckBox::toggled, this, &OverlayEditor::onStyleChanged);
    styleFlags->addWidget(italicCheck_);
    
    shadowCheck_ = new QCheckBox("Shadow");
    shadowCheck_->setChecked(true);
    connect(shadowCheck_, &QCheckBox::toggled, this, &OverlayEditor::onStyleChanged);
    styleFlags->addWidget(shadowCheck_);
    styleLayout->addRow("", styleFlags);
    
    propsLayout->addWidget(styleGroup);
    
    // Animation
    auto* animGroup = new QGroupBox("Animation");
    auto* animLayout = new QFormLayout(animGroup);
    
    animationCombo_ = new QComboBox();
    animationCombo_->addItems({"None", "Fade Pulse", "Scroll", "Bounce",
                               "Typewriter", "Wave", "Shake", "Scale", "Rainbow"});
    connect(animationCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &OverlayEditor::onAnimationChanged);
    animLayout->addRow("Type:", animationCombo_);
    
    animSpeedSpin_ = new QDoubleSpinBox();
    animSpeedSpin_->setRange(0.1, 10);
    animSpeedSpin_->setSingleStep(0.1);
    animSpeedSpin_->setValue(1.0);
    connect(animSpeedSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &OverlayEditor::onAnimationChanged);
    animLayout->addRow("Speed:", animSpeedSpin_);
    
    beatReactiveCheck_ = new QCheckBox("Beat Reactive");
    connect(beatReactiveCheck_, &QCheckBox::toggled, this, &OverlayEditor::onAnimationChanged);
    animLayout->addRow("", beatReactiveCheck_);
    
    propsLayout->addWidget(animGroup);
    propsLayout->addStretch();
    
    scrollArea->setWidget(propsWidget);
    mainLayout->addWidget(scrollArea, 1);
}

void OverlayEditor::refresh() {
    updateElementList();
}

void OverlayEditor::onElementSelected(int row) {
    if (!overlayEngine_ || row < 0) {
        currentElement_ = nullptr;
        return;
    }
    
    currentElement_ = overlayEngine_->config().elementAt(static_cast<usize>(row));
    if (currentElement_) {
        loadElement(currentElement_);
    }
}

void OverlayEditor::onAddElement() {
    if (!overlayEngine_) return;
    
    auto* elem = overlayEngine_->config().addElement();
    elem->setText("New Text");
    
    updateElementList();
    elementList_->setCurrentRow(elementList_->count() - 1);
    
    emit overlayChanged();
}

void OverlayEditor::onRemoveElement() {
    if (!overlayEngine_ || !currentElement_) return;
    
    overlayEngine_->config().removeElement(currentElement_->id());
    currentElement_ = nullptr;
    
    updateElementList();
    emit overlayChanged();
}

void OverlayEditor::onTextChanged() {
    if (updating_ || !currentElement_) return;
    currentElement_->setText(textEdit_->text());
    emit overlayChanged();
}

void OverlayEditor::onPositionChanged() {
    if (updating_ || !currentElement_) return;
    
    currentElement_->setPosition(
        static_cast<f32>(posXSpin_->value()),
        static_cast<f32>(posYSpin_->value())
    );
    
    static const TextAnchor anchors[] = {
        TextAnchor::TopLeft, TextAnchor::TopCenter, TextAnchor::TopRight,
        TextAnchor::CenterLeft, TextAnchor::Center, TextAnchor::CenterRight,
        TextAnchor::BottomLeft, TextAnchor::BottomCenter, TextAnchor::BottomRight
    };
    currentElement_->setAnchor(anchors[anchorCombo_->currentIndex()]);
    
    emit overlayChanged();
}

void OverlayEditor::onStyleChanged() {
    if (updating_ || !currentElement_) return;
    
    auto& style = currentElement_->style();
    style.fontFamily = fontCombo_->currentText();
    style.fontSize = static_cast<u32>(fontSizeSpin_->value());
    style.color = currentColor_;
    style.opacity = static_cast<f32>(opacitySpin_->value());
    style.bold = boldCheck_->isChecked();
    style.italic = italicCheck_->isChecked();
    style.shadow = shadowCheck_->isChecked();
    
    emit overlayChanged();
}

void OverlayEditor::onAnimationChanged() {
    if (updating_ || !currentElement_) return;
    
    static const AnimationType types[] = {
        AnimationType::None, AnimationType::FadePulse, AnimationType::Scroll,
        AnimationType::Bounce, AnimationType::TypeWriter, AnimationType::Wave,
        AnimationType::Shake, AnimationType::Scale, AnimationType::Rainbow
    };
    
    auto& anim = currentElement_->animation();
    anim.type = types[animationCombo_->currentIndex()];
    anim.speed = static_cast<f32>(animSpeedSpin_->value());
    anim.beatReactive = beatReactiveCheck_->isChecked();
    
    emit overlayChanged();
}

void OverlayEditor::onColorButtonClicked() {
    QColor color = QColorDialog::getColor(currentColor_, this, "Select Text Color",
                                           QColorDialog::ShowAlphaChannel);
    if (color.isValid()) {
        currentColor_ = color;
        setColorButtonColor(color);
        onStyleChanged();
    }
}

void OverlayEditor::onVisibilityChanged(bool visible) {
    if (updating_ || !currentElement_) return;
    currentElement_->setVisible(visible);
    emit overlayChanged();
}

void OverlayEditor::updateElementList() {
    elementList_->clear();
    
    if (!overlayEngine_) return;
    
    for (const auto& elem : overlayEngine_->config()) {
        QString text = QString::fromStdString(elem->id()) + ": " + elem->text();
        if (text.length() > 30) {
            text = text.left(27) + "...";
        }
        elementList_->addItem(text);
    }
}

void OverlayEditor::loadElement(TextElement* element) {
    updating_ = true;
    
    idEdit_->setText(QString::fromStdString(element->id()));
    textEdit_->setText(element->text());
    visibleCheck_->setChecked(element->visible());
    
    posXSpin_->setValue(element->position().x);
    posYSpin_->setValue(element->position().y);
    
    int anchorIdx = static_cast<int>(element->anchor());
    anchorCombo_->setCurrentIndex(anchorIdx);
    
    const auto& style = element->style();
    fontCombo_->setCurrentText(style.fontFamily);
    fontSizeSpin_->setValue(style.fontSize);
    currentColor_ = style.color;
    setColorButtonColor(style.color);
    opacitySpin_->setValue(style.opacity);
    boldCheck_->setChecked(style.bold);
    italicCheck_->setChecked(style.italic);
    shadowCheck_->setChecked(style.shadow);
    
    const auto& anim = element->animation();
    animationCombo_->setCurrentIndex(static_cast<int>(anim.type));
    animSpeedSpin_->setValue(anim.speed);
    beatReactiveCheck_->setChecked(anim.beatReactive);
    
    updating_ = false;
}

void OverlayEditor::setColorButtonColor(const QColor& color) {
    colorButton_->setStyleSheet(QString("background-color: %1; border: 1px solid #555;")
                                 .arg(color.name()));
}

} // namespace vc
