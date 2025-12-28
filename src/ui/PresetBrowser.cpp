#include "PresetBrowser.hpp"
#include "core/Logger.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>

namespace vc {

PresetBrowser::PresetBrowser(QWidget* parent)
    : QWidget(parent)
{
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
    
    // Search bar
    searchEdit_ = new QLineEdit();
    searchEdit_->setPlaceholderText("Search presets...");
    searchEdit_->setClearButtonEnabled(true);
    connect(searchEdit_, &QLineEdit::textChanged, this, &PresetBrowser::onSearchTextChanged);
    layout->addWidget(searchEdit_);
    
    // Category filter
    auto* filterLayout = new QHBoxLayout();
    
    categoryCombo_ = new QComboBox();
    categoryCombo_->addItem("All Categories", "");
    categoryCombo_->addItem("★ Favorites", "__favorites__");
    connect(categoryCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PresetBrowser::onCategoryChanged);
    filterLayout->addWidget(categoryCombo_, 1);
    
    randomButton_ = new QPushButton("🎲");
    randomButton_->setFixedSize(28, 28);
    randomButton_->setToolTip("Random preset");
    connect(randomButton_, &QPushButton::clicked, this, [this] {
        if (presetManager_) {
            presetManager_->selectRandom();
        }
    });
    filterLayout->addWidget(randomButton_);
    
    layout->addLayout(filterLayout);
    
    // Preset list
    presetList_ = new QListWidget();
    presetList_->setAlternatingRowColors(true);
    connect(presetList_, &QListWidget::itemDoubleClicked, 
            this, &PresetBrowser::onPresetDoubleClicked);
    layout->addWidget(presetList_, 1);
    
    // Action buttons
    auto* buttonLayout = new QHBoxLayout();
    
    favoriteButton_ = new QPushButton("★ Favorite");
    favoriteButton_->setCheckable(true);
    connect(favoriteButton_, &QPushButton::clicked, this, &PresetBrowser::onFavoriteClicked);
    buttonLayout->addWidget(favoriteButton_);
    
    blacklistButton_ = new QPushButton("⊘ Blacklist");
    blacklistButton_->setCheckable(true);
    connect(blacklistButton_, &QPushButton::clicked, this, &PresetBrowser::onBlacklistClicked);
    buttonLayout->addWidget(blacklistButton_);
    
    layout->addLayout(buttonLayout);
}

void PresetBrowser::refresh() {
    if (!presetManager_) {
        presetList_->clear();
        return;
    }
    
    // Update categories first (with signals blocked)
    updateCategories();
    
    std::vector<const PresetInfo*> presets;
    
    if (currentCategory_ == "__favorites__") {
        presets = presetManager_->favoritePresets();
    } else if (!searchQuery_.empty()) {
        presets = presetManager_->search(searchQuery_);
    } else if (!currentCategory_.empty()) {
        presets = presetManager_->byCategory(currentCategory_);
    } else {
        presets = presetManager_->activePresets();
    }
    
    populateList(presets);
}

void PresetBrowser::scrollToCurrent() {
    if (!presetManager_) return;
    
    const auto* current = presetManager_->current();
    if (!current) return;
    
    for (int i = 0; i < presetList_->count(); ++i) {
        auto* item = presetList_->item(i);
        if (item->data(Qt::UserRole).toString().toStdString() == current->path.string()) {
            presetList_->scrollToItem(item);
            presetList_->setCurrentItem(item);
            break;
        }
    }
}

void PresetBrowser::onSearchTextChanged(const QString& text) {
    searchQuery_ = text.toStdString();
    
    // Don't call full refresh, just update the list
    if (!presetManager_) return;
    
    std::vector<const PresetInfo*> presets;
    if (!searchQuery_.empty()) {
        presets = presetManager_->search(searchQuery_);
    } else if (currentCategory_ == "__favorites__") {
        presets = presetManager_->favoritePresets();
    } else if (!currentCategory_.empty()) {
        presets = presetManager_->byCategory(currentCategory_);
    } else {
        presets = presetManager_->activePresets();
    }
    
    populateList(presets);
}

void PresetBrowser::onCategoryChanged(int index) {
    if (index < 0) return;  // Ignore invalid index during clear
    
    currentCategory_ = categoryCombo_->itemData(index).toString().toStdString();
    
    // Don't call full refresh, just update the list
    if (!presetManager_) return;
    
    std::vector<const PresetInfo*> presets;
    if (currentCategory_ == "__favorites__") {
        presets = presetManager_->favoritePresets();
    } else if (!searchQuery_.empty()) {
        presets = presetManager_->search(searchQuery_);
    } else if (!currentCategory_.empty()) {
        presets = presetManager_->byCategory(currentCategory_);
    } else {
        presets = presetManager_->activePresets();
    }
    
    populateList(presets);
}

void PresetBrowser::onPresetDoubleClicked(QListWidgetItem* item) {
    if (!presetManager_) {
        LOG_WARN("PresetBrowser: presetManager_ is null");
        return;
    }
    
    QString path = item->data(Qt::UserRole).toString();
    LOG_DEBUG("PresetBrowser: Double-clicked preset: {}", path.toStdString());
    presetManager_->selectByPath(fs::path(path.toStdString()));
    emit presetSelected(path);
    LOG_DEBUG("PresetBrowser: Emitted presetSelected signal for: {}", path.toStdString());
}

void PresetBrowser::onFavoriteClicked() {
    if (!presetManager_) return;
    
    auto* item = presetList_->currentItem();
    if (!item) return;
    
    QString path = item->data(Qt::UserRole).toString();
    for (usize i = 0; i < presetManager_->allPresets().size(); ++i) {
        if (presetManager_->allPresets()[i].path.string() == path.toStdString()) {
            presetManager_->toggleFavorite(i);
            break;
        }
    }
}

void PresetBrowser::onBlacklistClicked() {
    if (!presetManager_) return;
    
    auto* item = presetList_->currentItem();
    if (!item) return;
    
    QString path = item->data(Qt::UserRole).toString();
    for (usize i = 0; i < presetManager_->allPresets().size(); ++i) {
        if (presetManager_->allPresets()[i].path.string() == path.toStdString()) {
            presetManager_->toggleBlacklisted(i);
            break;
        }
    }
}

void PresetBrowser::populateList(const std::vector<const PresetInfo*>& presets) {
    presetList_->clear();
    
    for (const auto* preset : presets) {
        QString name = QString::fromStdString(preset->name);
        auto* item = new QListWidgetItem(name, presetList_);
        item->setData(Qt::UserRole, QString::fromStdString(preset->path.string()));
        
        if (preset->favorite) {
            item->setText("★ " + name);
            item->setForeground(QColor(255, 215, 0));
        }
        
        QString tooltip = QString::fromStdString(preset->path.filename().string());
        if (!preset->author.empty()) {
            tooltip += "\nAuthor: " + QString::fromStdString(preset->author);
        }
        tooltip += "\nCategory: " + QString::fromStdString(preset->category);
        tooltip += QString("\nPlays: %1").arg(preset->playCount);
        item->setToolTip(tooltip);
    }
}

void PresetBrowser::updateCategories() {
    if (!presetManager_) return;
    
    // Block signals to prevent recursive calls
    categoryCombo_->blockSignals(true);
    
    // Remember current selection
    QString current = categoryCombo_->currentData().toString();
    
    categoryCombo_->clear();
    categoryCombo_->addItem("All Categories", "");
    categoryCombo_->addItem("★ Favorites", "__favorites__");
    
    for (const auto& cat : presetManager_->categories()) {
        categoryCombo_->addItem(QString::fromStdString(cat), QString::fromStdString(cat));
    }
    
    // Restore selection
    int idx = categoryCombo_->findData(current);
    if (idx >= 0) {
        categoryCombo_->setCurrentIndex(idx);
    }
    
    // Re-enable signals
    categoryCombo_->blockSignals(false);
}

} // namespace vc
