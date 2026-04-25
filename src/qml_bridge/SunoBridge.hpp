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
class SunoOrchestrator;
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
    Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY filterTextChanged)
    Q_PROPERTY(QVariantList featureGates READ featureGates NOTIFY featureGatesChanged)

public:
    explicit SunoBridge(QObject* parent = nullptr);
    static QObject* create(QQmlEngine*, QJSEngine*);
    static void setSunoController(vc::suno::SunoController* controller);

    bool loading() const;
    QVariantList clips() const;
    int totalClips() const;
    QVariantList chatHistory() const;
    QVariantList featureGates() const { return featureGates_; }
    QString filterText() const { return filterText_; }
    void setFilterText(const QString& filter);

public slots:
    Q_INVOKABLE void generate(const QString& prompt, const QString& tags, bool instrumental, const QString& model,
                              const QString& customLyrics = "", double weirdness = 0.0, double styleWeight = 0.0,
                              int seed = -1, const QString& personaId = "", const QString& continueClipId = "",
                              double continueAt = -1.0);
    Q_INVOKABLE void refreshLibrary(int page = 1);
    Q_INVOKABLE void sendChatMessage(const QString& message);
    Q_INVOKABLE void fetchChatHistory();
    Q_INVOKABLE void fetchFeatureGates();

signals:
    void loadingChanged();
    void clipsChanged();
    void chatHistoryChanged();
    void generationStarted();
    void filterTextChanged();
    void featureGatesChanged();

private slots:
    void onLibraryUpdated();

private:
    void updateFilteredClips();

    static vc::suno::SunoController* s_controller;
    static vc::suno::SunoClient* s_client;
    static vc::SunoOrchestrator* s_orchestrator;
    static SunoBridge* s_instance;

    QVariantList clips_;
    QVariantList allClips_; // Full cache from backend
    QVariantList chatHistory_;
    QVariantList featureGates_;
    QString filterText_;
    bool loading_{false};
};

} // namespace qml_bridge
