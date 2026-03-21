#pragma once

#include <QObject>
#include <QtQml/qqml.h>
#include <QVariantList>
#include <QVariantMap>

namespace vc {
namespace suno {
class SunoController;
struct SunoClip;
}
}

namespace qml_bridge {

class SunoBridge : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool isAuthenticated READ isAuthenticated NOTIFY authChanged)
    Q_PROPERTY(bool isSyncing READ isSyncing NOTIFY syncingChanged)
    Q_PROPERTY(QVariantList clips READ clips NOTIFY clipsChanged)
    Q_PROPERTY(int totalClips READ totalClips NOTIFY clipsChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusChanged)
    Q_PROPERTY(QString searchQuery READ searchQuery WRITE setSearchQuery NOTIFY searchQueryChanged)
    Q_PROPERTY(QVariantList searchResults READ searchResults NOTIFY searchResultsChanged)

public:
    explicit SunoBridge(QObject* parent = nullptr);
    ~SunoBridge() override = default;

    static SunoBridge* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);
    static void setSunoController(vc::suno::SunoController* controller);
    static void connectSignals();

    bool isAuthenticated() const;
    bool isSyncing() const;
    QVariantList clips() const;
    int totalClips() const;
    QString statusMessage() const;
    QString searchQuery() const;
    QVariantList searchResults() const;

    void setSearchQuery(const QString& query);

public slots:
    Q_INVOKABLE void authenticate();
    Q_INVOKABLE void signOut();
    Q_INVOKABLE void refreshLibrary(int page = 1);
    Q_INVOKABLE void syncDatabase();
    Q_INVOKABLE void downloadAndPlay(const QString& clipId);
    Q_INVOKABLE QVariantMap getClip(const QString& clipId) const;
    Q_INVOKABLE bool hasLyrics(const QString& clipId) const;
    Q_INVOKABLE void fetchLyrics(const QString& clipId);

signals:
    void authChanged();
    void syncingChanged();
    void clipsChanged();
    void statusChanged();
    void searchQueryChanged();
    void searchResultsChanged();
    void clipDownloaded(const QString& clipId, const QString& path);

private slots:
    void onLibraryUpdated(const std::vector<vc::suno::SunoClip>& clips);
    void onClipUpdated(const std::string& clipId);
    void onStatusMessage(const std::string& message);

private:
    QVariantMap clipToVariant(const vc::suno::SunoClip& clip) const;
    void updateSearchResults();

    static vc::suno::SunoController* s_controller;
    static SunoBridge* s_instance;

    QString statusMessage_;
    QString searchQuery_;
    QVariantList searchResults_;
    bool isSyncing_{false};
};

} // namespace qml_bridge
