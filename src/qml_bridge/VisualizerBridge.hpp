/**
 * @file VisualizerBridge.hpp
 * @brief QML bridge for ProjectM visualizer control
 *
 * Single-responsibility: QML interface for visualizer settings and presets
 *
 * @version 1.0.0
 */

#pragma once

#include <QObject>
#include <QtQml/qqml.h>
#include <QStringList>
#include <QAbstractListModel>

namespace vc {
class VisualizerWindow;
}

namespace qml_bridge {

/**
 * @brief Model for preset list in QML
 */
class PresetModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        PathRole,
        RatingRole,
        IsCurrentRole
    };

    explicit PresetModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setPresets(const QStringList& presets);
    void setCurrentIndex(int idx);

private:
    QStringList presets_;
    int currentIdx_{-1};
};

/**
 * @brief QML bridge for visualizer control
 *
 * Provides:
 * - Preset selection and navigation
 * - FPS and render settings
 * - Fullscreen control
 * - Preset search
 */
class VisualizerBridge : public QObject {
    Q_OBJECT

    Q_PROPERTY(int fps READ fps WRITE setFps NOTIFY fpsChanged)
    Q_PROPERTY(bool fullscreen READ fullscreen WRITE setFullscreen NOTIFY fullscreenChanged)
    Q_PROPERTY(bool presetLocked READ presetLocked WRITE setPresetLocked NOTIFY presetLockedChanged)
    Q_PROPERTY(int currentPresetIndex READ currentPresetIndex NOTIFY currentPresetChanged)
    Q_PROPERTY(QString currentPresetName READ currentPresetName NOTIFY currentPresetChanged)
    Q_PROPERTY(QObject* presetModel READ presetModel CONSTANT)

public:
    explicit VisualizerBridge(QObject* parent = nullptr);
    ~VisualizerBridge() override = default;

    static VisualizerBridge* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);

    void setVisualizerWindow(vc::VisualizerWindow* window);

    int fps() const;
    void setFps(int fps);

    bool fullscreen() const;
    void setFullscreen(bool fullscreen);

    bool presetLocked() const;
    void setPresetLocked(bool locked);

    int currentPresetIndex() const;
    QString currentPresetName() const;

    QObject* presetModel() const;

public slots:
    Q_INVOKABLE void nextPreset();
    Q_INVOKABLE void previousPreset();
    Q_INVOKABLE void randomPreset();
    Q_INVOKABLE void selectPreset(int index);
    Q_INVOKABLE void selectPresetByName(const QString& name);
    Q_INVOKABLE void searchPresets(const QString& query);

signals:
    void fpsChanged();
    void fullscreenChanged();
    void presetLockedChanged();
    void currentPresetChanged();
    void presetsUpdated();

private:
    vc::VisualizerWindow* window_{nullptr};
    PresetModel* presetModel_{nullptr};
    int fps_{60};
    bool fullscreen_{false};
    bool presetLocked_{false};
    int currentPresetIndex_{-1};
};

} // namespace qml_bridge
