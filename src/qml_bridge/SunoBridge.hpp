#pragma once
#include <QObject>
#include <QtQml/qqml.h>
#include <QVariantList>
#include <QVariantMap>

namespace vc::suno { class SunoClient; }

namespace qml_bridge {

class SunoBridge : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool isLoading READ isLoading NOTIFY isLoadingChanged)
    Q_PROPERTY(QVariantList library READ library NOTIFY libraryChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY totalCountChanged)

public:
    explicit SunoBridge(QObject* parent = nullptr);
    ~SunoBridge() override = default;

    bool isLoading() const { return isLoading_; }
    QVariantList library() const { return library_; }
    int totalCount() const { return totalCount_; }

    Q_INVOKABLE void refreshLibrary();
    Q_INVOKABLE void loadMore();
    Q_INVOKABLE void generate(const QString& prompt, const QString& tags, bool instrumental, const QString& modelId);
    
    // B-Side Orchestrator
    Q_INVOKABLE void sendChatMessage(const QString& message, const QString& workspaceId = "");
    Q_INVOKABLE void fetchChatHistory();

signals:
    void isLoadingChanged();
    void libraryChanged();
    void totalCountChanged();
    void generationStarted();
    void chatMessageReceived(const QString& response, const QString& workspaceId);
    void chatHistoryFetched(const QVariantList& sessions);
    void errorOccurred(const QString& error);

private slots:
    void onLibraryUpdated();
    void onGenerationStarted();
    void onChatMessageReceived(const QString& response, const QString& workspaceId);

private:
    vc::suno::SunoClient* client_{nullptr};
    QVariantList library_;
    bool isLoading_{false};
    int totalCount_{0};
    int currentPage_{0};
};

} // namespace qml_bridge
