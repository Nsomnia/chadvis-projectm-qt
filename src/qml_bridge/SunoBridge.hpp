#pragma once
#include <QObject>
#include <QtQml/qqml.h>
#include <QVariantList>
#include <QString>

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
    Q_PROPERTY(int totalClips READ totalClips NOTIFY clipsChanged)
    Q_PROPERTY(QVariantList chatHistory READ chatHistory NOTIFY chatHistoryChanged)

public:
    explicit SunoBridge(QObject* parent = nullptr);
    static QObject* create(QQmlEngine*, QJSEngine*);
    static void setSunoController(vc::suno::SunoController* controller);

    bool loading() const;
    QVariantList clips() const;
    int totalClips() const;
    QVariantList chatHistory() const;

public slots:
    Q_INVOKABLE void generate(const QString& prompt, const QString& tags, bool instrumental, const QString& model);
    Q_INVOKABLE void refreshLibrary(int page = 1);
    Q_INVOKABLE void sendChatMessage(const QString& message);
    Q_INVOKABLE void fetchChatHistory();

signals:
    void loadingChanged();
    void clipsChanged();
    void chatHistoryChanged();
    void generationStarted();

private slots:
    void onLibraryUpdated();

private:
    static vc::suno::SunoController* s_controller;
    static vc::suno::SunoClient* s_client;
    static SunoBridge* s_instance;

    QVariantList clips_;
    QVariantList chatHistory_;
    bool loading_{false};
};

} // namespace qml_bridge
