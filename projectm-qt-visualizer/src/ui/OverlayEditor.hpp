#pragma once
// OverlayEditor.hpp - Text overlay editor
// WYSIWYG for your watermarks

#include "util/Types.hpp"
#include "overlay/OverlayEngine.hpp"

#include <QWidget>
#include <QListWidget>
#include <QLineEdit>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QPushButton>
#include <QCheckBox>
#include <QColorDialog>

namespace vc {

class OverlayEditor : public QWidget {
    Q_OBJECT
    
public:
    explicit OverlayEditor(QWidget* parent = nullptr);
    
    void setOverlayEngine(OverlayEngine* engine);
    
signals:
    void overlayChanged();
    
public slots:
    void refresh();
    
private slots:
    void onElementSelected(int row);
    void onAddElement();
    void onRemoveElement();
    void onTextChanged();
    void onPositionChanged();
    void onStyleChanged();
    void onAnimationChanged();
    void onColorButtonClicked();
    void onVisibilityChanged(bool visible);
    
private:
    void setupUI();
    void updateElementList();
    void loadElement(TextElement* element);
    void saveElement();
    void setColorButtonColor(const QColor& color);
    
    OverlayEngine* overlayEngine_{nullptr};
    TextElement* currentElement_{nullptr};
    
    // Element list
    QListWidget* elementList_{nullptr};
    QPushButton* addButton_{nullptr};
    QPushButton* removeButton_{nullptr};
    
    // Text properties
    QLineEdit* textEdit_{nullptr};
    QLineEdit* idEdit_{nullptr};
    QCheckBox* visibleCheck_{nullptr};
    
    // Position
    QDoubleSpinBox* posXSpin_{nullptr};
    QDoubleSpinBox* posYSpin_{nullptr};
    QComboBox* anchorCombo_{nullptr};
    
    // Style
    QComboBox* fontCombo_{nullptr};
    QSpinBox* fontSizeSpin_{nullptr};
    QPushButton* colorButton_{nullptr};
    QDoubleSpinBox* opacitySpin_{nullptr};
    QCheckBox* boldCheck_{nullptr};
    QCheckBox* italicCheck_{nullptr};
    QCheckBox* shadowCheck_{nullptr};
    
    // Animation
    QComboBox* animationCombo_{nullptr};
    QDoubleSpinBox* animSpeedSpin_{nullptr};
    QCheckBox* beatReactiveCheck_{nullptr};
    
    QColor currentColor_{Qt::white};
    bool updating_{false};
};

} // namespace vc
