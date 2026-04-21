#pragma once
#include <QObject>
#include <QtQml/qqml.h>
#include <QVariantList>

namespace vc {
namespace suno {
class SunoController;
class SunoClient;
}
}

namespace qml_bridge {

class SunoBridge : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QVariantList clips READ clips NOTIFY clipsChanged)

public:
    explicit SunoBridge(QObject* parent = nullptr);
    static QObject* create(QQmlEngine*, QJSEngine*);
    static void setSunoController(vc::suno::SunoController* controller);

    bool loading() const;
    QVariantList clips() const;

public slots:
    Q_INVOKABLE void generate(const QString& prompt, const QString& tags, bool instrumental, const QString& model);
    Q_INVOKABLE void fetchLibrary();

signals:
    void loadingChanged();
    void clipsChanged();
    void generationStarted();

private:
    static vc::suno::SunoController* s_controller;
    static vc::suno::SunoClient* s_client;
    static SunoBridge* s_instance;
};

} // namespace qml_bridge
