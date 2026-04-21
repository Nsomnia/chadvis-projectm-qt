#pragma once
#include <QObject>
#include <QtQml/qqml.h>
#include <QVariantList>
#include <QVariantMap>
#include <vector>
#include "suno/SunoModels.hpp"

namespace vc::suno { class SunoClient; class SunoController; }

namespace qml_bridge {

class SunoBridge : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool isLoading READ isLoading NOTIFY isLoadingChanged)
    Q_PROPERTY(QVariantList clips READ library NOTIFY libraryChanged)
    Q_PROPERTY(int totalClips READ totalCount NOTIFY totalCountChanged)
    Q_PROPERTY(QVariantList chatHistory READ chatHistory NOTIFY chatHistoryChanged)

public:
    explicit SunoBridge(QObject* parent = nullptr);
    ~SunoBridge() override = default;

    static SunoBridge* create(QQmlEngine*, QJSEngine*) {
        return new SunoBridge();
    }

    bool isLoading() const { return isLoading_; }
    QVariantList library() const { return library_; }
    int totalCount() const { return totalCount_; }
    QVariantList chatHistory() const { return chatHistory_; }

    void setSunoController(vc::suno::SunoController* controller);
    void connectSignals();

    Q_INVOKABLE void refreshLibrary(int page = 0);
    Q_INVOKABLE void loadMore();
    Q_INVOKABLE void generate(const QString& prompt, const QString& tags, bool instrumental, const QString& modelId);
    
    Q_INVOKABLE void sendChatMessage(const QString& message, const QString& workspaceId = "");
    Q_INVOKABLE void fetchChatHistory();

signals:
    void isLoadingChanged();
    void libraryChanged();
    void totalCountChanged();
    void chatHistoryChanged();
    void generationStarted();
    void chatMessageReceived(const QString& response, const QString& workspaceId);
    void chatHistoryFetched(const QVariantList& sessions);
    void errorOccurred(const QString& error);

private slots:
    void onLibraryUpdated(const std::vector<vc::suno::SunoClip>& clips);
    void onGenerationStarted();
    void onChatMessageReceived(const QString& response, const QString& workspaceId);

private:
    vc::suno::SunoController* controller_{nullptr};
    vc::suno::SunoClient* client_{nullptr};
    QVariantList library_;
    QVariantList chatHistory_;
    bool isLoading_{false};
    int totalCount_{0};
    int currentPage_{0};
};

} // namespace qml_bridge
