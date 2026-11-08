#include "PresetBridge.hpp"
#include <system_error>
#include "core/Config.hpp"
#include "core/Logger.hpp"
#include "visualizer/PresetData.hpp"
#include "visualizer/PresetManager.hpp"
#include "visualizer/RatingManager.hpp"

namespace qml_bridge {

vc::PresetManager* PresetBridge::s_manager = nullptr;

std::size_t migrateNameKeyedRatings(const vc::PresetManager::PresetList& presets,
                                    vc::RatingManager& ratings) {
    std::size_t migrated = 0;
    for (const auto& preset : presets) {
        // A name key with no path twin is a rating from before T0058; a path
        // that already holds stars was written after, so it always wins.
        const int stars = ratings.getRating(preset.name);
        const std::string path = preset.path.string();
        if (stars > 0 && ratings.getRating(path) == 0) {
            ratings.setRating(path, stars);
            ++migrated;
        }
    }
    return migrated;
}

PresetBridge::PresetBridge(QObject* parent) : QObject(parent) { attachManager(); }

void PresetBridge::attachManager() {
    if (!s_manager || signalsAttached_) return;
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
    s_manager->presetChanged.connect([self](const vc::PresetInfo* p) { self->onPresetChanged(p); });
    s_manager->listChanged.connect([self]() { self->onListChanged(); });
    // A failed scan never publishes a generation, so without this the panel
    // would show its scanning placeholder forever for a scan that already ended.
    s_manager->scanFailed.connect([self](const std::string& /*message*/) { self->onScanFailed(); });

    // A manager that was already scanned before the bridge existed (tests, a
    // synchronous startup scan) has its list waiting here; otherwise the first
    // listChanged publication runs the migration.
    migrateRatingsOnFirstList();
}

void PresetBridge::migrateRatingsOnFirstList() {
    if (ratingsMigrated_ || !s_manager) return;

    const vc::PresetManager::Snapshot presets = s_manager->allPresets();
    if (presets->empty()) return; // wait for the first real generation
    ratingsMigrated_ = true;

    if (migrateNameKeyedRatings(*presets, vc::RatingManager::instance()) == 0) return;
    // Persist once so the file is rewritten with path keys. The old name keys
    // otherwise stay as unreachable entries -- harmless, and still the only copy
    // of a star whose name is shared by two banks.
    if (auto saved = vc::RatingManager::instance().save(); !saved) {
        LOG_WARN("PresetBridge: could not persist migrated ratings: {}", saved.error().message);
    }
}

void PresetBridge::setPresetManager(vc::PresetManager* manager) {
    s_manager = manager;
    // The singleton may already exist if something touched it before the
    // manager was registered; otherwise its constructor attaches on creation.
    if (auto* bridge = instance(); bridge) bridge->attachManager();
}

QVariantList PresetBridge::presets() const {
    if (!s_manager) return {};

    // One shared handle for the whole loop: it pins the generation the pointers
    // below point into, so a rescan publishing a new generation mid-build can
    // never dangle them (and no lock is held while the QVariantMaps are built).
    const vc::PresetManager::Snapshot generation = s_manager->allPresets();

    QVariantList result;
    result.reserve(static_cast<qsizetype>(generation->size()));
    int fullListIndex = 0;
    for (const auto& preset : *generation) {
        result.append(presetToVariant(preset, fullListIndex++));
    }
    return result;
}

QStringList PresetBridge::categories() const {
    if (!s_manager) return {};

    QStringList result;
    for (const auto& cat : s_manager->categories()) {
        result.append(QString::fromStdString(cat));
    }
    return result;
}

QVariantMap PresetBridge::currentPreset() const {
    if (!s_manager) return {};

    const auto* current = s_manager->current();
    if (!current) return {};

    return presetToVariant(*current, static_cast<int>(s_manager->currentIndex()));
}

int PresetBridge::currentIndex() const {
    return s_manager ? static_cast<int>(s_manager->currentIndex()) : 0;
}

int PresetBridge::presetCount() const {
    return s_manager ? static_cast<int>(s_manager->count()) : 0;
}

bool PresetBridge::scanning() const {
    // No manager, or no scan ever requested: nothing can be in flight.
    return s_manager ? s_manager->scanInFlight() : false;
}

QString PresetBridge::searchQuery() const { return searchQuery_; }

QString PresetBridge::selectedCategory() const { return selectedCategory_; }

void PresetBridge::setSearchQuery(const QString& query) {
    if (searchQuery_ != query) {
        searchQuery_ = query;
        emit searchQueryChanged();
        emit presetsChanged();
    }
}

void PresetBridge::setSelectedCategory(const QString& category) {
    if (selectedCategory_ != category) {
        selectedCategory_ = category;
        emit selectedCategoryChanged();
        emit presetsChanged();
    }
}

bool PresetBridge::selectByIndex(int index) {
    if (!s_manager) return false;
    return s_manager->selectByIndex(static_cast<size_t>(index));
}

bool PresetBridge::selectRandom() {
    if (!s_manager) return false;
    return s_manager->selectRandom();
}

void PresetBridge::toggleFavorite(int index) {
    if (s_manager && index >= 0) {
        s_manager->toggleFavorite(static_cast<size_t>(index));
    }
}

void PresetBridge::toggleBlacklist(int index) {
    if (s_manager && index >= 0) {
        s_manager->toggleBlacklisted(static_cast<size_t>(index));
    }
}

void PresetBridge::setRating(int index, int rating) {
    if (!s_manager || index < 0 || rating < 1 || rating > 5) return;

    // Pin the generation for the duration of the lookup; the index means nothing
    // without it once a rescan publishes a new list.
    const vc::PresetManager::Snapshot presets = s_manager->allPresets();
    if (static_cast<std::size_t>(index) < presets->size()) {
        vc::RatingManager::instance().setRating((*presets)[index].path.string(),
                                                static_cast<int>(rating));
        emit presetsChanged();
    }
}

QVariantList PresetBridge::filteredPresets() const {
    if (!s_manager) return {};

    // One view, whatever the filter: whichever manager query answers, the
    // result pins the generation its pointers borrow from.
    vc::PresetManager::PresetView filtered;

    if (!searchQuery_.isEmpty()) {
        filtered = s_manager->search(searchQuery_.toStdString());
    } else if (selectedCategory_ == QLatin1String("__favorites__")) {
        filtered = s_manager->favoritePresets();
    } else if (selectedCategory_ == QLatin1String("__blacklisted__")) {
        // The combo has offered this entry all along; without the branch the
        // sentinel fell through to byCategory() and the list came back empty.
        filtered = s_manager->blacklistedPresets();
    } else if (!selectedCategory_.isEmpty()) {
        filtered = s_manager->byCategory(selectedCategory_.toStdString());
    } else {
        filtered = s_manager->activePresets();
    }

    return toVariantList(filtered);
}

QVariantList PresetBridge::toVariantList(const vc::PresetManager::PresetView& view) const {
    QVariantList result;
    result.reserve(static_cast<qsizetype>(view.items.size()));
    // Every item of a PresetView borrows from view.generation's own storage —
    // that is the documented PresetView contract — so the full-list index is
    // pointer arithmetic within one array, the same pointer-to-index idiom
    // PresetManager::selectRandom() already uses. It is deliberately NOT the
    // visible row position: search and category filters reorder and drop rows,
    // while every QML-facing index slot addresses the full generation.
    const auto* const base = view.generation->data();
    for (const auto* preset : view.items) {
        result.append(presetToVariant(*preset, static_cast<int>(preset - base)));
    }
    return result;
}

void PresetBridge::rescan() {
    if (!s_manager) return;

    // Off-thread. The directory walk is thousands of stat() calls on a real
    // library, far too slow for the GUI thread that invoked us, so it runs on
    // the manager's scan worker and the finished list is published back here as
    // one listChanged. A request that arrives while a scan is in flight is
    // coalesced into a single follow-up, so holding down the rescan button
    // cannot queue a pile of walks or multiply the QVariantList rebuilds that
    // presetsChanged triggers.
    s_manager->rescanAsync();
    // A queued scan is in flight now, before the finished list lands: fold the
    // flag so the panel shows the scanning placeholder, not the empty one.
    emit scanningChanged();
}

QString PresetBridge::folder() const {
    return QString::fromStdString(vc::Config::instance().visualizer().presetPath.string());
}

void PresetBridge::setFolder(const QString& path) {
    if (!s_manager || path.isEmpty()) return;

    const vc::fs::path dir(path.toStdString());
    std::error_code ec;
    if (!vc::fs::is_directory(dir, ec)) {
        // The only producer is a FolderDialog, which can only hand back a real
        // directory, so this is a programmatic caller or a stale dialog value.
        // Refusing keeps the previous library instead of blanking it.
        LOG_WARN("PresetBridge: refused preset folder '{}' (not a directory)", path.toStdString());
        return;
    }

    if (dir == vc::fs::path(folder().toStdString())) {
        // Re-picking the current folder means "scan it again", which is what the
        // Rescan button does -- no config write, no auto-save churn.
        s_manager->rescanAsync();
        emit scanningChanged();
        return;
    }

    vc::Config::instance().visualizerMut().presetPath = dir;
    if (auto saved = vc::Config::instance().save(vc::Config::instance().configPath()); !saved) {
        LOG_ERROR("PresetBridge: could not persist the preset folder: {}",
                      saved.error().message);
    }
    emit folderChanged();
    s_manager->scanAsync(dir, true);
    emit scanningChanged();
}

void PresetBridge::onPresetChanged(const vc::PresetInfo* preset) {
    Q_UNUSED(preset)
    emit currentPresetChanged();
}

void PresetBridge::onListChanged() {
    migrateRatingsOnFirstList();
    // The in-flight scan landed (or a follow-up is already queued, in which
    // case the live flag still reads true): fold the flag, then the list.
    emit scanningChanged();
    emit presetsChanged();
}

void PresetBridge::onScanFailed() {
    // The scan ended without a list. The previous generation is untouched, so
    // only the flag folds -- the panel's empty block must come back.
    emit scanningChanged();
}
QVariantMap PresetBridge::presetToVariant(const vc::PresetInfo& info, int fullListIndex) const {
    QVariantMap map;
    map[QStringLiteral("index")] = fullListIndex;
    map[QStringLiteral("name")] = QString::fromStdString(info.name);
    map[QStringLiteral("path")] = QString::fromStdString(info.path.string());
    map[QStringLiteral("author")] = QString::fromStdString(info.author);
    map[QStringLiteral("category")] = QString::fromStdString(info.category);
    map[QStringLiteral("favorite")] = info.favorite;
    map[QStringLiteral("blacklisted")] = info.blacklisted;
    map[QStringLiteral("playCount")] = static_cast<int>(info.playCount);
    map[QStringLiteral("rating")] = vc::RatingManager::instance().getRating(info.path.string());
    return map;
}

} // namespace qml_bridge
