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
  Q_PROPERTY(bool hasMorePages READ hasMorePages NOTIFY hasMorePagesChanged)
  Q_PROPERTY(int currentPage READ currentPage NOTIFY currentPageChanged)
  Q_PROPERTY(QVariantList chatHistory READ chatHistory NOTIFY chatHistoryChanged)
  Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY filterTextChanged)

public:
    explicit SunoBridge(QObject* parent = nullptr);
    static QObject* create(QQmlEngine*, QJSEngine*);
    static void setSunoController(vc::suno::SunoController* controller);

  bool loading() const;
  QVariantList clips() const;
  int totalClips() const;
  bool hasMorePages() const;
  int currentPage() const;
  QVariantList chatHistory() const;
    QString filterText() const { return filterText_; }
    void setFilterText(const QString& filter);

public slots:
    Q_INVOKABLE void generate(const QString& prompt, const QString& tags, bool instrumental, const QString& model);
    Q_INVOKABLE void refreshLibrary(int page = 1);
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

private slots:
    void onLibraryUpdated();

private:
    void updateFilteredClips();

    static vc::suno::SunoController* s_controller;
    static vc::suno::SunoClient* s_client;
    static SunoBridge* s_instance;

  QVariantList clips_;
  QVariantList allClips_; // Full cache from backend
  QVariantList chatHistory_;
  QString filterText_;
  bool loading_{false};
  bool hasMorePages_{false};
  int currentPage_{1};
};

} // namespace qml_bridge
