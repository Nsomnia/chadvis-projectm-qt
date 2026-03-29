#pragma once

#include <QObject>
#include <QtQml/qqml.h>
#include <QVariantList>
#include <QVariantMap>
#include <QStringList>
#include "visualizer/PresetData.hpp"

namespace vc {
class PresetManager;
}

namespace qml_bridge {

class PresetBridge : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QVariantList presets READ presets NOTIFY presetsChanged)
    Q_PROPERTY(QVariantList activePresets READ activePresets NOTIFY presetsChanged)
    Q_PROPERTY(QVariantList favoritePresets READ favoritePresets NOTIFY presetsChanged)
    Q_PROPERTY(QStringList categories READ categories NOTIFY presetsChanged)
    Q_PROPERTY(QVariantMap currentPreset READ currentPreset NOTIFY currentPresetChanged)
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY currentPresetChanged)
    Q_PROPERTY(int presetCount READ presetCount NOTIFY presetsChanged)
    Q_PROPERTY(int activeCount READ activeCount NOTIFY presetsChanged)
    Q_PROPERTY(QString searchQuery READ searchQuery WRITE setSearchQuery NOTIFY searchQueryChanged)
    Q_PROPERTY(QString selectedCategory READ selectedCategory WRITE setSelectedCategory NOTIFY selectedCategoryChanged)

public:
    explicit PresetBridge(QObject* parent = nullptr);
    ~PresetBridge() override = default;

    static PresetBridge* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);
    static void setPresetManager(vc::PresetManager* manager);
    static void connectSignals();

    QVariantList presets() const;
    QVariantList activePresets() const;
    QVariantList favoritePresets() const;
    QStringList categories() const;
    QVariantMap currentPreset() const;
    int currentIndex() const;
    int presetCount() const;
    int activeCount() const;
    QString searchQuery() const;
    QString selectedCategory() const;

    void setSearchQuery(const QString& query);
    void setSelectedCategory(const QString& category);

public slots:
    Q_INVOKABLE bool selectByIndex(int index);
    Q_INVOKABLE bool selectByName(const QString& name);
    Q_INVOKABLE bool selectRandom();
    Q_INVOKABLE bool selectNext();
    Q_INVOKABLE bool selectPrevious();
    Q_INVOKABLE void toggleFavorite(int index);
    Q_INVOKABLE void toggleBlacklist(int index);
    Q_INVOKABLE void setRating(int index, int rating);
    Q_INVOKABLE int getRating(const QString& presetName) const;
    Q_INVOKABLE QVariantList filteredPresets() const;
    Q_INVOKABLE void rescan();

signals:
    void presetsChanged();
    void currentPresetChanged();
    void searchQueryChanged();
    void selectedCategoryChanged();

private slots:
    void onPresetChanged(const vc::PresetInfo* preset);
    void onListChanged();

private:
    QVariantMap presetToVariant(const vc::PresetInfo& info) const;

    static vc::PresetManager* s_manager;
    static PresetBridge* s_instance;

    QString searchQuery_;
    QString selectedCategory_;
    QVariantList cachedPresets_;
};

} // namespace qml_bridge
