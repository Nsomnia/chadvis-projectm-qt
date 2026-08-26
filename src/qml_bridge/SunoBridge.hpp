#pragma once
#include <QObject>
#include <QtQml/qqml.h>
#include <QTimer>
#include <QVariantList>
#include <QString>
#include "QmlSingletonBridge.hpp"

namespace vc {
namespace suno {
class SunoController;
class SunoClient;
}
}

namespace qml_bridge {

class SunoBridge : public QObject,
                   public QmlSingletonBridge<SunoBridge, SingletonPolicy::CachedUnparented> {

// The CRTP mixin constructs this singleton via its private
// constructor; grant only the exact instantiation access.
friend class QmlSingletonBridge<SunoBridge, SingletonPolicy::CachedUnparented>;
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

  Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
  Q_PROPERTY(QVariantList clips READ clips NOTIFY clipsChanged)
  Q_PROPERTY(int totalClips READ totalClips NOTIFY clipsChanged)
  Q_PROPERTY(bool hasMorePages READ hasMorePages NOTIFY hasMorePagesChanged)
  Q_PROPERTY(int currentPage READ currentPage NOTIFY currentPageChanged)
  Q_PROPERTY(QVariantList chatHistory READ chatHistory NOTIFY chatHistoryChanged)
  Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY filterTextChanged)

  // Account snapshot (read-only; populated after auth turns ActiveValid).
  Q_PROPERTY(int credits READ credits NOTIFY billingInfoChanged)
  Q_PROPERTY(QString planName READ planName NOTIFY billingInfoChanged)
  Q_PROPERTY(QString userName READ userName NOTIFY accountInfoChanged)

public:
    explicit SunoBridge(QObject* parent = nullptr);
    static void setSunoController(vc::suno::SunoController* controller);

  bool loading() const;
  QVariantList clips() const;
  int totalClips() const;
  bool hasMorePages() const;
  int currentPage() const;
  QVariantList chatHistory() const;
    QString filterText() const { return filterText_; }
    void setFilterText(const QString& filter);

    // Account snapshot getters (tolerant: empty/0 before first fetch).
    int credits() const;
    QString planName() const;
    QString userName() const;

public slots:
    Q_INVOKABLE void generate(const QString& prompt, const QString& tags, bool instrumental, const QString& model);
    Q_INVOKABLE void refreshLibrary(int page = 1);
    /// Cursor-based "load more" for the infinite-scroll path.
    Q_INVOKABLE void requestNextLibraryPage();
    /// Server-side library search (debounced 350 ms inside the bridge).
    Q_INVOKABLE void searchLibrary(const QString& searchText);
    Q_INVOKABLE void sendChatMessage(const QString& message, const QString& workspaceId = {});
    Q_INVOKABLE void fetchChatHistory();

signals:
  void loadingChanged();
  void clipsChanged();
  void hasMorePagesChanged();
  void currentPageChanged();
  void chatHistoryChanged();
    void generationStarted();
    void filterTextChanged();
    void billingInfoChanged();
    void accountInfoChanged();

private slots:
    void onLibraryUpdated();

private:
    void updateFilteredClips();

    static vc::suno::SunoController* s_controller;
    static vc::suno::SunoClient* s_client;

  QVariantList clips_;
  QVariantList allClips_; // Full cache from backend
  QVariantList chatHistory_;
  QString filterText_;
  bool loading_{false};
  bool hasMorePages_{false};
  int currentPage_{1};
    QTimer searchDebounce_; // 350 ms server-search debounce
    QString searchDebounceText_;
};

} // namespace qml_bridge
