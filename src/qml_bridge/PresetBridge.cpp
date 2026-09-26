#include "PresetBridge.hpp"
#include "visualizer/PresetManager.hpp"
#include "visualizer/PresetData.hpp"
#include "visualizer/RatingManager.hpp"

namespace qml_bridge {

vc::PresetManager* PresetBridge::s_manager = nullptr;

PresetBridge::PresetBridge(QObject* parent)
    : QObject(parent)
{
    attachManager();
}

void PresetBridge::attachManager()
{
    if (!s_manager || signalsAttached_)
        return;
    signalsAttached_ = true;

    // A manager scan publishes its result by queuing onto this bridge, so the
    // hand-off always lands on the GUI thread QML reads from.
    s_manager->setPublishContext(this);

    // The manager's vc::Signal connections are made HERE rather than from
    // registerBridges(): QML instantiates this singleton lazily, so the
    // registration call happens before there is a bridge to connect to, and
    // with nothing connected a finished scan never reaches QML — the list would
    // stay empty, because main.qml binds the list once at load time, long
    // before the async startup scan publishes. This is also why the wiring
    // lives in a guard: connecting twice would emit presetsChanged twice per
    // publication and double the QVariantList rebuilds.
    auto* self = this;
    s_manager->presetChanged.connect([self](const vc::PresetInfo* p) {
        self->onPresetChanged(p);
    });
    s_manager->listChanged.connect([self]() {
        self->onListChanged();
    });
}

void PresetBridge::setPresetManager(vc::PresetManager* manager)
{
    s_manager = manager;
    // The singleton may already exist if something touched it before the
    // manager was registered; otherwise its constructor attaches on creation.
    if (auto* bridge = instance(); bridge)
        bridge->attachManager();
}

QVariantList PresetBridge::presets() const
{
    if (!s_manager) return {};

    // One shared handle for the whole loop: it pins the generation the pointers
    // below point into, so a rescan publishing a new generation mid-build can
    // never dangle them (and no lock is held while the QVariantMaps are built).
    const vc::PresetManager::Snapshot generation = s_manager->allPresets();

    QVariantList result;
    result.reserve(static_cast<qsizetype>(generation->size()));
    for (const auto& preset : *generation) {
        result.append(presetToVariant(preset));
    }
    return result;
}

QVariantList PresetBridge::activePresets() const
{
    if (!s_manager) return {};

    const vc::PresetManager::PresetView active = s_manager->activePresets();
    return toVariantList(active);
}

QVariantList PresetBridge::favoritePresets() const
{
    if (!s_manager) return {};

    const vc::PresetManager::PresetView favorites = s_manager->favoritePresets();
    return toVariantList(favorites);
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

    // Pin the generation for the duration of the lookup; the index means nothing
    // without it once a rescan publishes a new list.
    const vc::PresetManager::Snapshot presets = s_manager->allPresets();
    if (static_cast<std::size_t>(index) < presets->size()) {
        vc::RatingManager::instance().setRating(
            (*presets)[index].name, static_cast<int>(rating));
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

    // One view, whatever the filter: whichever manager query answers, the
    // result pins the generation its pointers borrow from.
    vc::PresetManager::PresetView filtered;

    if (!searchQuery_.isEmpty()) {
        filtered = s_manager->search(searchQuery_.toStdString());
    } else if (selectedCategory_ == QLatin1String("__favorites__")) {
        filtered = s_manager->favoritePresets();
    } else if (!selectedCategory_.isEmpty()) {
        filtered = s_manager->byCategory(selectedCategory_.toStdString());
    } else {
        filtered = s_manager->activePresets();
    }

    return toVariantList(filtered);
}

QVariantList PresetBridge::toVariantList(const vc::PresetManager::PresetView& view) const
{
    QVariantList result;
    result.reserve(static_cast<qsizetype>(view.items.size()));
    for (const auto* preset : view.items) {
        result.append(presetToVariant(*preset));
    }
    return result;
}

void PresetBridge::rescan()
{
    if (!s_manager) return;

    // Off-thread. The directory walk is thousands of stat() calls on a real
    // library, far too slow for the GUI thread that invoked us, so it runs on
    // the manager's scan worker and the finished list is published back here as
    // one listChanged. A request that arrives while a scan is in flight is
    // coalesced into a single follow-up, so holding down the rescan button
    // cannot queue a pile of walks or multiply the QVariantList rebuilds that
    // presetsChanged triggers.
    s_manager->rescanAsync();
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
