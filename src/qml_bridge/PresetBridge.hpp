#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqml.h>
#include "visualizer/PresetData.hpp"
// The full PresetManager definition, not a forward declaration: the private read
// helpers below name PresetManager::PresetView, the handle that keeps a query
// result's pointers valid while the QML-facing QVariantMaps are built from them.
#include "QmlSingletonBridge.hpp"
#include "visualizer/PresetManager.hpp"

namespace vc { class RatingManager; }

namespace qml_bridge {

/// One-shot upgrade for ratings written before T0058: those were keyed on the
/// preset NAME, so each star is moved onto its preset's path key. A path that
/// already carries a rating is never overwritten, and the old name keys are left
/// in place -- they are unreachable, not wrong, and deleting them would drop the
/// only copy of a star when the name repeats across two banks.
/// Returns how many ratings were moved; the caller persists when it is non-zero.
std::size_t migrateNameKeyedRatings(const vc::PresetManager::PresetList& presets,
                                    vc::RatingManager& ratings);

class PresetBridge : public QObject,
                     public QmlSingletonBridge<PresetBridge, SingletonPolicy::CachedQmlParented> {
    // The CRTP mixin constructs this singleton via its private
    // constructor; grant only the exact instantiation access.
    friend class QmlSingletonBridge<PresetBridge, SingletonPolicy::CachedQmlParented>;
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QVariantList presets READ presets NOTIFY presetsChanged)
    Q_PROPERTY(QStringList categories READ categories NOTIFY presetsChanged)
    Q_PROPERTY(QVariantMap currentPreset READ currentPreset NOTIFY currentPresetChanged)
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY currentPresetChanged)
    Q_PROPERTY(int presetCount READ presetCount NOTIFY presetsChanged)
    // True while a preset scan is in flight on the manager's worker. The
    // backing state is PresetManager::scanInFlight(); the bridge folds it to
    // false on listChanged (the scan landed) and scanFailed (it did not).
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    Q_PROPERTY(QString searchQuery READ searchQuery WRITE setSearchQuery NOTIFY searchQueryChanged)
    Q_PROPERTY(QString selectedCategory READ selectedCategory WRITE setSelectedCategory NOTIFY
                       selectedCategoryChanged)
    // The directory the current generation was scanned from -- the effective
    // default when the user has never chosen, so the Settings row can show what
    // is actually being read rather than an empty string.
    Q_PROPERTY(QString folder READ folder WRITE setFolder NOTIFY folderChanged)

public:
    explicit PresetBridge(QObject* parent = nullptr);
    ~PresetBridge() override = default;

    static void setPresetManager(vc::PresetManager* manager);

    QVariantList presets() const;
    QStringList categories() const;
    QVariantMap currentPreset() const;
    int currentIndex() const;
    int presetCount() const;
    bool scanning() const;
    QString searchQuery() const;
    QString selectedCategory() const;
    QString folder() const;

    void setSearchQuery(const QString& query);
    void setSelectedCategory(const QString& category);
    /// Point the library at another directory: persists `visualizer.preset_path`
    /// (one owner -- the config field -- rather than a second copy in the
    /// bridge) and rescans that directory. A path that is not a directory is
    /// refused with the old one kept, so a typo cannot empty the library.
    void setFolder(const QString& path);

public slots:
    Q_INVOKABLE bool selectByIndex(int index);
    Q_INVOKABLE bool selectRandom();
    Q_INVOKABLE void toggleFavorite(int index);
    Q_INVOKABLE void toggleBlacklist(int index);
    Q_INVOKABLE void setRating(int index, int rating);
    Q_INVOKABLE QVariantList filteredPresets() const;
    Q_INVOKABLE void rescan();

signals:
    void presetsChanged();
    void scanningChanged();
    void currentPresetChanged();
    void searchQueryChanged();
    void selectedCategoryChanged();
    void folderChanged();

private slots:
    void onPresetChanged(const vc::PresetInfo* preset);
    void onListChanged();
    void onScanFailed();

private:
    /// Binds this bridge to the registered manager: it becomes the scan-result
    /// publish context and subscribes to the manager's signals. Idempotent, and
    /// driven from the constructor because QML creates this singleton lazily.
    void attachManager();

    /// Runs the name-keyed rating migration once, as soon as a non-empty preset
    /// generation exists, and persists the result. Safe to call repeatedly.
    void migrateRatingsOnFirstList();

    /// Value form handed to QML. `fullListIndex` is the preset's position in
    /// the FULL manager generation — the list every index-based QML-facing
    /// slot (selectByIndex, toggleFavorite, toggleBlacklisted, setRating)
    /// addresses — never the row's position in a filtered view: the panel
    /// renders filteredPresets(), so the two diverge whenever a search or
    /// category filter is active. QML passes the emitted key straight back
    /// into those slots; when this key was missing, modelData.index was
    /// undefined, QML coerced it to 0, and every visible row acted on
    /// preset #0.
    QVariantMap presetToVariant(const vc::PresetInfo& info, int fullListIndex) const;
    /// Copies a manager query result into the value-only form QML consumes.
    /// QML never sees a `const PresetInfo*`, so nothing on the QML side can
    /// outlive the generation the view pins.
    QVariantList toVariantList(const vc::PresetManager::PresetView& view) const;

    static vc::PresetManager* s_manager;

    bool signalsAttached_{false};
    /// True once the name-keyed ratings have been migrated (or there was nothing
    /// to migrate), so every later list change skips the pass and the save.
    bool ratingsMigrated_{false};
    QString searchQuery_;
    QString selectedCategory_;
};

} // namespace qml_bridge
