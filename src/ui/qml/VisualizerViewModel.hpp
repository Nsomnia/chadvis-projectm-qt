#pragma once

#include <QObject>
#include <QString>

namespace vc {
class VisualizerWindow;
}

namespace vc::ui::qml {

class VisualizerViewModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool presetLocked READ presetLocked WRITE setPresetLocked NOTIFY presetLockedChanged)
    Q_PROPERTY(QString presetName READ presetName NOTIFY presetNameChanged)

public:
    explicit VisualizerViewModel(VisualizerWindow* visualizer, QObject* parent = nullptr);

    bool presetLocked() const { return presetLocked_; }
    QString presetName() const { return presetName_; }

    Q_INVOKABLE void nextPreset();
    Q_INVOKABLE void previousPreset();
    Q_INVOKABLE void randomPreset();
    Q_INVOKABLE void togglePresetLock();

public slots:
    void setPresetLocked(bool locked);

signals:
    void presetLockedChanged();
    void presetNameChanged();

private:
    VisualizerWindow* visualizer_{nullptr};
    bool presetLocked_{false};
    QString presetName_{"No preset loaded"};
};

} // namespace vc::ui::qml
