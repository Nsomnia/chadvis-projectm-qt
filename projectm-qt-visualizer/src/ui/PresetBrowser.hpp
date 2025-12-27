#pragma once
// PresetBrowser.hpp - Visualizer preset browser
// Window shopping for eye candy

#include "util/Types.hpp"
#include "visualizer/PresetManager.hpp"

#include <QWidget>
#include <QListWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>

namespace vc {

class PresetBrowser : public QWidget {
    Q_OBJECT
    
public:
    explicit PresetBrowser(QWidget* parent = nullptr);
    
    void setPresetManager(PresetManager* manager);
    
signals:
    void presetSelected(const QString& path);
    
public slots:
    void refresh();
    void scrollToCurrent();
    
private slots:
    void onSearchTextChanged(const QString& text);
    void onCategoryChanged(int index);
    void onPresetDoubleClicked(QListWidgetItem* item);
    void onFavoriteClicked();
    void onBlacklistClicked();
    
private:
    void setupUI();
    void populateList(const std::vector<const PresetInfo*>& presets);
    void updateCategories();
    
    PresetManager* presetManager_{nullptr};
    
    QLineEdit* searchEdit_{nullptr};
    QComboBox* categoryCombo_{nullptr};
    QListWidget* presetList_{nullptr};
    QPushButton* favoriteButton_{nullptr};
    QPushButton* blacklistButton_{nullptr};
    QPushButton* randomButton_{nullptr};
    
    std::string currentCategory_;
    std::string searchQuery_;
};

} // namespace vc
