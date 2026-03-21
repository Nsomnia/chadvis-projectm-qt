#include "PresetBridge.hpp"
#include "visualizer/PresetManager.hpp"
#include "visualizer/PresetData.hpp"
#include "visualizer/RatingManager.hpp"
#include <QQmlEngine>

namespace qml_bridge {

vc::PresetManager* PresetBridge::s_manager = nullptr;
PresetBridge* PresetBridge::s_instance = nullptr;

PresetBridge::PresetBridge(QObject* parent)
    : QObject(parent)
{
}

PresetBridge* PresetBridge::create(QQmlEngine* qmlEngine, QJSEngine* jsEngine)
{
    Q_UNUSED(jsEngine)
    if (!s_instance) {
        s_instance = new PresetBridge(qmlEngine);
    }
    return s_instance;
}

void PresetBridge::setPresetManager(vc::PresetManager* manager)
{
    s_manager = manager;
}

void PresetBridge::connectSignals()
{
    if (s_manager && s_instance) {
        s_manager->presetChanged.connect([s = s_instance](const vc::PresetInfo* p) {
            s->onPresetChanged(p);
        });
        s_manager->listChanged.connect([s = s_instance]() {
            s->onListChanged();
        });
    }
}

QVariantList PresetBridge::presets() const
{
    if (!s_manager) return {};

    QVariantList result;
    for (const auto& preset : s_manager->allPresets()) {
        result.append(presetToVariant(preset));
    }
    return result;
}

QVariantList PresetBridge::activePresets() const
{
    if (!s_manager) return {};

    QVariantList result;
    for (const auto* preset : s_manager->activePresets()) {
        result.append(presetToVariant(*preset));
    }
    return result;
}

QVariantList PresetBridge::favoritePresets() const
{
    if (!s_manager) return {};

    QVariantList result;
    for (const auto* preset : s_manager->favoritePresets()) {
        result.append(presetToVariant(*preset));
    }
    return result;
}

QStringList PresetBridge::categories() const
{
    if (!s_manager) return {};

    QStringList result;
    for (const auto& cat : s_manager->categories()) {
        result.append(QString::fromStdString(cat));
    }
    return result;
}

QVariantMap PresetBridge::currentPreset() const
{
    if (!s_manager) return {};

    const auto* current = s_manager->current();
    if (!current) return {};

    return presetToVariant(*current);
}

int PresetBridge::currentIndex() const
{
    return s_manager ? static_cast<int>(s_manager->currentIndex()) : 0;
}

int PresetBridge::presetCount() const
{
    return s_manager ? static_cast<int>(s_manager->count()) : 0;
}

int PresetBridge::activeCount() const
{
    return s_manager ? static_cast<int>(s_manager->activeCount()) : 0;
}

QString PresetBridge::searchQuery() const
{
    return searchQuery_;
}

QString PresetBridge::selectedCategory() const
{
    return selectedCategory_;
}

void PresetBridge::setSearchQuery(const QString& query)
{
    if (searchQuery_ != query) {
        searchQuery_ = query;
        emit searchQueryChanged();
        emit presetsChanged();
    }
}

void PresetBridge::setSelectedCategory(const QString& category)
{
    if (selectedCategory_ != category) {
        selectedCategory_ = category;
        emit selectedCategoryChanged();
        emit presetsChanged();
    }
}

bool PresetBridge::selectByIndex(int index)
{
    if (!s_manager) return false;
    return s_manager->selectByIndex(static_cast<size_t>(index));
}

bool PresetBridge::selectByName(const QString& name)
{
    if (!s_manager) return false;
    return s_manager->selectByName(name.toStdString());
}

bool PresetBridge::selectRandom()
{
    if (!s_manager) return false;
    return s_manager->selectRandom();
}

bool PresetBridge::selectNext()
{
    if (!s_manager) return false;
    return s_manager->selectNext();
}

bool PresetBridge::selectPrevious()
{
    if (!s_manager) return false;
    return s_manager->selectPrevious();
}

void PresetBridge::toggleFavorite(int index)
{
    if (s_manager && index >= 0) {
        s_manager->toggleFavorite(static_cast<size_t>(index));
    }
}

void PresetBridge::toggleBlacklist(int index)
{
    if (s_manager && index >= 0) {
        s_manager->toggleBlacklisted(static_cast<size_t>(index));
    }
}

void PresetBridge::setRating(int index, int rating)
{
    if (!s_manager || index < 0 || rating < 1 || rating > 5) return;

    const auto& presets = s_manager->allPresets();
    if (static_cast<size_t>(index) < presets.size()) {
        vc::RatingManager::instance().setRating(
            presets[index].name, static_cast<int>(rating));
        emit presetsChanged();
    }
}

int PresetBridge::getRating(const QString& presetName) const
{
    return vc::RatingManager::instance().getRating(presetName.toStdString());
}

QVariantList PresetBridge::filteredPresets() const
{
    if (!s_manager) return {};

    std::vector<const vc::PresetInfo*> filtered;

    if (!searchQuery_.isEmpty()) {
        filtered = s_manager->search(searchQuery_.toStdString());
    } else if (selectedCategory_ == QLatin1String("__favorites__")) {
        for (const auto* p : s_manager->favoritePresets()) {
            filtered.push_back(p);
        }
    } else if (!selectedCategory_.isEmpty()) {
        filtered = s_manager->byCategory(selectedCategory_.toStdString());
    } else {
        for (const auto* p : s_manager->activePresets()) {
            filtered.push_back(p);
        }
    }

    QVariantList result;
    for (const auto* preset : filtered) {
        result.append(presetToVariant(*preset));
    }
    return result;
}

void PresetBridge::rescan()
{
    if (s_manager) {
        s_manager->rescan();
    }
}

void PresetBridge::onPresetChanged(const vc::PresetInfo* preset)
{
    Q_UNUSED(preset)
    emit currentPresetChanged();
}

void PresetBridge::onListChanged()
{
    emit presetsChanged();
}

QVariantMap PresetBridge::presetToVariant(const vc::PresetInfo& info) const
{
    QVariantMap map;
    map[QStringLiteral("name")] = QString::fromStdString(info.name);
    map[QStringLiteral("path")] = QString::fromStdString(info.path.string());
    map[QStringLiteral("author")] = QString::fromStdString(info.author);
    map[QStringLiteral("category")] = QString::fromStdString(info.category);
    map[QStringLiteral("favorite")] = info.favorite;
    map[QStringLiteral("blacklisted")] = info.blacklisted;
    map[QStringLiteral("playCount")] = static_cast<int>(info.playCount);
    map[QStringLiteral("rating")] = vc::RatingManager::instance().getRating(info.name);
    return map;
}

} // namespace qml_bridge
