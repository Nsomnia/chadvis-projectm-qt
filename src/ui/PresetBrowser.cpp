#include "PresetBrowser.hpp"
#include "core/Logger.hpp"
#include "visualizer/RatingManager.hpp"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QVBoxLayout>

namespace vc {

PresetBrowser::PresetBrowser(QWidget* parent) : QWidget(parent) {
    setupUI();
}

void PresetBrowser::setPresetManager(PresetManager* manager) {
    presetManager_ = manager;
    if (manager) {
        manager->listChanged.connect([this] {
            QMetaObject::invokeMethod(this, &PresetBrowser::refresh);
        });
        manager->presetChanged.connect([this](const PresetInfo*) {
            QMetaObject::invokeMethod(this, &PresetBrowser::scrollToCurrent);
        });
        refresh();
    }
}

void PresetBrowser::setupUI() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    searchEdit_ = new QLineEdit();
    searchEdit_->setPlaceholderText("Search presets...");
    searchEdit_->setClearButtonEnabled(true);
    connect(searchEdit_,
            &QLineEdit::textChanged,
            this,
            &PresetBrowser::onSearchTextChanged);
    layout->addWidget(searchEdit_);

    auto* filterLayout = new QHBoxLayout();
    categoryCombo_ = new QComboBox();
    categoryCombo_->addItem("All Categories", "");
    categoryCombo_->addItem("★ Favorites", "__favorites__");
    connect(categoryCombo_,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            &PresetBrowser::onCategoryChanged);
    filterLayout->addWidget(categoryCombo_, 1);

    randomButton_ = new QPushButton("🎲");
    randomButton_->setFixedSize(28, 28);
    randomButton_->setToolTip("Random preset");
    connect(randomButton_, &QPushButton::clicked, this, [this] {
        if (presetManager_)
            presetManager_->selectRandom();
    });
    filterLayout->addWidget(randomButton_);
    layout->addLayout(filterLayout);

    presetList_ = new QListWidget();
    presetList_->setAlternatingRowColors(true);
    connect(presetList_,
            &QListWidget::itemDoubleClicked,
            this,
            &PresetBrowser::onPresetDoubleClicked);
    connect(presetList_,
            &QListWidget::currentRowChanged,
            this,
            &PresetBrowser::onCurrentRowChanged);
    layout->addWidget(presetList_, 1);

    // Rating Bar
    auto* ratingLayout = new QHBoxLayout();
    ratingLayout->setSpacing(2);
    ratingLayout->addWidget(new QLabel("Rating:"));
    for (int i = 1; i <= 5; ++i) {
        auto* star = new QPushButton("☆");
        star->setFixedSize(24, 24);
        star->setFlat(true);
        star->setProperty("stars", i);
        connect(star,
                &QPushButton::clicked,
                this,
                &PresetBrowser::onStarClicked);
        ratingButtons_.push_back(star);
        ratingLayout->addWidget(star);
    }
    ratingLayout->addStretch();
    layout->addLayout(ratingLayout);

    auto* buttonLayout = new QHBoxLayout();
    favoriteButton_ = new QPushButton("★ Favorite");
    favoriteButton_->setCheckable(true);
    connect(favoriteButton_,
            &QPushButton::clicked,
            this,
            &PresetBrowser::onFavoriteClicked);
    buttonLayout->addWidget(favoriteButton_);

    blacklistButton_ = new QPushButton("⊘ Blacklist");
    blacklistButton_->setCheckable(true);
    connect(blacklistButton_,
            &QPushButton::clicked,
            this,
            &PresetBrowser::onBlacklistClicked);
    buttonLayout->addWidget(blacklistButton_);
    layout->addLayout(buttonLayout);
}

void PresetBrowser::onStarClicked() {
    auto* star = qobject_cast<QPushButton*>(sender());
    if (!star || !presetManager_)
        return;
    auto* item = presetList_->currentItem();
    if (!item)
        return;

    int stars = star->property("stars").toInt();
    std::string path = item->data(Qt::UserRole).toString().toStdString();
    std::string name = item->text()
                               .remove("★ ")
                               .remove(QRegularExpression("\\[.*\\] "))
                               .toStdString();

    RatingManager::instance().setRating(name, stars);
    RatingManager::instance().save();
    updateRatingDisplay(stars);

    // Update text in list
    QString ratingStr =
            QString("[%1]").arg(QString(stars, '*').leftJustified(5, '.'));
    item->setText(
            (presetManager_->current() && presetManager_->current()->favorite
                     ? "★ "
                     : "") +
            ratingStr + " " + QString::fromStdString(name));
}

void PresetBrowser::updateRatingDisplay(int stars) {
    for (int i = 0; i < 5; ++i) {
        ratingButtons_[i]->setText(i < stars ? "★" : "☆");
        ratingButtons_[i]->setStyleSheet(i < stars ? "color: #FFD700;"
                                                   : "color: #888888;");
    }
}

void PresetBrowser::onCurrentRowChanged(int row) {
    if (row < 0)
        return;
    auto* item = presetList_->item(row);
    if (!item)
        return;
    std::string name = item->text()
                               .remove("★ ")
                               .remove(QRegularExpression("\\[.*\\] "))
                               .toStdString();
    int rating = RatingManager::instance().getRating(name);
    updateRatingDisplay(rating);
}

void PresetBrowser::refresh() {
    if (!presetManager_) {
        presetList_->clear();
        return;
    }
    updateCategories();
    std::vector<const PresetInfo*> presets;
    if (currentCategory_ == "__favorites__")
        presets = presetManager_->favoritePresets();
    else if (!searchQuery_.empty())
        presets = presetManager_->search(searchQuery_);
    else if (!currentCategory_.empty())
        presets = presetManager_->byCategory(currentCategory_);
    else
        presets = presetManager_->activePresets();
    populateList(presets);
}

void PresetBrowser::scrollToCurrent() {
    if (!presetManager_)
        return;
    const auto* current = presetManager_->current();
    if (!current)
        return;
    std::string currentPath = current->path.string();
    for (int i = 0; i < presetList_->count(); ++i) {
        auto* item = presetList_->item(i);
        if (item->data(Qt::UserRole).toString().toStdString() == currentPath) {
            presetList_->scrollToItem(item);
            presetList_->setCurrentItem(item);
            break;
        }
    }
}

void PresetBrowser::onSearchTextChanged(const QString& text) {
    searchQuery_ = text.toStdString();
    refresh();
}

void PresetBrowser::onCategoryChanged(int index) {
    if (index < 0)
        return;
    currentCategory_ = categoryCombo_->itemData(index).toString().toStdString();
    refresh();
}

void PresetBrowser::onPresetDoubleClicked(QListWidgetItem* item) {
    if (!presetManager_)
        return;
    QString path = item->data(Qt::UserRole).toString();
    presetManager_->selectByPath(fs::path(path.toStdString()));
    emit presetSelected(path);
}

void PresetBrowser::onFavoriteClicked() {
    if (!presetManager_)
        return;
    auto* item = presetList_->currentItem();
    if (!item)
        return;
    QString path = item->data(Qt::UserRole).toString();
    for (usize i = 0; i < presetManager_->allPresets().size(); ++i) {
        if (presetManager_->allPresets()[i].path.string() ==
            path.toStdString()) {
            presetManager_->toggleFavorite(i);
            break;
        }
    }
    refresh();
}

void PresetBrowser::onBlacklistClicked() {
    if (!presetManager_)
        return;
    auto* item = presetList_->currentItem();
    if (!item)
        return;
    QString path = item->data(Qt::UserRole).toString();
    for (usize i = 0; i < presetManager_->allPresets().size(); ++i) {
        if (presetManager_->allPresets()[i].path.string() ==
            path.toStdString()) {
            presetManager_->toggleBlacklisted(i);
            break;
        }
    }
    refresh();
}

void PresetBrowser::populateList(
        const std::vector<const PresetInfo*>& presets) {
    presetList_->clear();
    for (const auto* preset : presets) {
        int rating = RatingManager::instance().getRating(preset->name);
        QString ratingStr =
                QString("[%1]").arg(QString(rating, '*').leftJustified(5, '.'));
        QString name = (preset->favorite ? "★ " : "") + ratingStr + " " +
                       QString::fromStdString(preset->name);
        auto* item = new QListWidgetItem(name, presetList_);
        item->setData(Qt::UserRole,
                      QString::fromStdString(preset->path.string()));
        if (preset->favorite)
            item->setForeground(QColor(255, 215, 0));

        QString tooltip =
                QString::fromStdString(preset->path.filename().string());
        if (!preset->author.empty())
            tooltip += "\nAuthor: " + QString::fromStdString(preset->author);
        tooltip += "\nCategory: " + QString::fromStdString(preset->category);
        tooltip += QString("\nPlays: %1").arg(preset->playCount);
        item->setToolTip(tooltip);
    }
}

void PresetBrowser::updateCategories() {
    if (!presetManager_)
        return;
    categoryCombo_->blockSignals(true);
    QString current = categoryCombo_->currentData().toString();
    categoryCombo_->clear();
    categoryCombo_->addItem("All Categories", "");
    categoryCombo_->addItem("★ Favorites", "__favorites__");
    for (const auto& cat : presetManager_->categories())
        categoryCombo_->addItem(QString::fromStdString(cat),
                                QString::fromStdString(cat));
    int idx = categoryCombo_->findData(current);
    if (idx >= 0)
        categoryCombo_->setCurrentIndex(idx);
    categoryCombo_->blockSignals(false);
}

} // namespace vc
