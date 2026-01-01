#pragma once
#include <QComboBox>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QWidget>
#include <vector>
#include "util/Types.hpp"
#include "visualizer/PresetManager.hpp"

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
    void onStarClicked();
    void onCurrentRowChanged(int row);

private:
    void setupUI();
    void populateList(const std::vector<const PresetInfo*>& presets);
    void updateCategories();
    void updateRatingDisplay(int stars);

    PresetManager* presetManager_{nullptr};
    QLineEdit* searchEdit_{nullptr};
    QComboBox* categoryCombo_{nullptr};
    QListWidget* presetList_{nullptr};
    QPushButton* randomButton_{nullptr};
    QPushButton* favoriteButton_{nullptr};
    QPushButton* blacklistButton_{nullptr};

    std::vector<QPushButton*> ratingButtons_;
    std::string searchQuery_;
    std::string currentCategory_;
};

} // namespace vc
