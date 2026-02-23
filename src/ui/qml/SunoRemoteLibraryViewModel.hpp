#pragma once

#include <QObject>
#include <QString>

#include <functional>
#include <optional>
#include <vector>

#include "suno/SunoModels.hpp"
#include "util/Signal.hpp"
#include "ui/qml/SunoClipListModel.hpp"

namespace vc::suno {
class SunoController;
}

namespace vc::ui::qml {

class SunoRemoteLibraryViewModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(SunoClipListModel* model READ model CONSTANT)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(QString searchQuery READ searchQuery WRITE setSearchQuery NOTIFY searchQueryChanged)
    Q_PROPERTY(int clipCount READ clipCount NOTIFY clipCountChanged)
    Q_PROPERTY(int totalClipCount READ totalClipCount NOTIFY totalClipCountChanged)
    Q_PROPERTY(int lyricsReadyCount READ lyricsReadyCount NOTIFY lyricsReadyCountChanged)
    Q_PROPERTY(bool syncing READ syncing NOTIFY syncingChanged)

public:
    explicit SunoRemoteLibraryViewModel(vc::suno::SunoController* controller, QObject* parent = nullptr);
    ~SunoRemoteLibraryViewModel() override;

    SunoClipListModel* model() { return &model_; }

    QString statusMessage() const { return statusMessage_; }
    QString searchQuery() const { return searchQuery_; }
    int clipCount() const;
    int totalClipCount() const;
    int lyricsReadyCount() const;
    bool syncing() const { return syncing_; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void authenticate();
    Q_INVOKABLE void playClipAt(int row);
    Q_INVOKABLE void playClipById(const QString& clipId);

public slots:
    void setSearchQuery(const QString& query);

signals:
    void statusMessageChanged();
    void searchQueryChanged();
    void clipCountChanged();
    void totalClipCountChanged();
    void lyricsReadyCountChanged();
    void syncingChanged();

private:
    void bindControllerSignals();
    void applyFilter();
    void setStatusMessage(const QString& message);
    void setSyncing(bool syncing);
    void invokeOnUiThread(std::function<void()> fn);

    vc::suno::SunoController* controller_{nullptr};
    SunoClipListModel model_;

    QString statusMessage_{"Suno remote library idle"};
    QString searchQuery_;
    bool syncing_{false};

    std::vector<vc::suno::SunoClip> allClips_;

    std::optional<Signal<const std::vector<vc::suno::SunoClip>&>::SlotId> libraryUpdatedConnection_;
    std::optional<Signal<const std::string&>::SlotId> clipUpdatedConnection_;
    std::optional<Signal<const std::string&>::SlotId> statusMessageConnection_;
};

} // namespace vc::ui::qml
