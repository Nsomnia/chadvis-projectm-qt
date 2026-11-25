#pragma once
// SunoLineageBridge.hpp - the lineage view's one contact with C++.
//
// It is a separate QML singleton rather than another section of `SunoBridge` on
// purpose: `SunoBridge` already owns the library, the transport borrow, the
// mutation channel and the download latch, and a lineage query is none of those.
// The heavy lifting lives in `vc::suno::lineage` (pure, headless-tested); this
// class only decides WHICH clips to fingerprint, WHEN a rebuild is allowed, and
// how a graph becomes a list of QVariantMaps.
//
// Two kinds of edge reach the view and they are never blended:
//   * remote   - `GET /clips/{id}/attribution` said so. Refreshed on request.
//   * inferred - ChadVis compared titles, styles, descriptions, model and age.
//                 Rebuilt wholesale, presented as a guess, removable by the user.

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqml.h>

#include "QmlSingletonBridge.hpp"
#include "suno/SunoModels.hpp"
#include "suno/lineage/LineageWeights.hpp"

namespace vc {
namespace suno {
class SunoController;
}
namespace suno::lineage {
class SunoLineageStore;
}
} // namespace vc

class QNetworkReply;

namespace qml_bridge {

class SunoLineageBridge
    : public QObject,
      public QmlSingletonBridge<SunoLineageBridge, SingletonPolicy::CachedUnparented> {
    friend class QmlSingletonBridge<SunoLineageBridge, SingletonPolicy::CachedUnparented>;
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /// Flat node list, already laid out: `id`, `title`, `tags`, `createdAt`,
    /// `model`, `playCount`, `instrumental`, `role` (focus/ancestor/descendant),
    /// `depth`, `column`, `row`, `linkedTo`, `relation`, `provenance`,
    /// `confidence`. Ordered by (column, row), so a Repeater can draw it as is.
    Q_PROPERTY(QVariantList nodes READ nodes NOTIFY graphChanged)
    /// Drawable relationships among those nodes: `from` (child), `to` (parent),
    /// `relation`, `confidence`, `provenance`, `evidence`.
    Q_PROPERTY(QVariantList edges READ edges NOTIFY graphChanged)
    Q_PROPERTY(QString focusId READ focusId NOTIFY graphChanged)
    Q_PROPERTY(QString focusTitle READ focusTitle NOTIFY graphChanged)
    Q_PROPERTY(int columns READ columns NOTIFY graphChanged)
    Q_PROPERTY(int rows READ rows NOTIFY graphChanged)
    /// The node cap or the depth limit cut the neighbourhood. Never presented as
    /// "that is all there is".
    Q_PROPERTY(bool truncated READ truncated NOTIFY graphChanged)
    Q_PROPERTY(int hiddenNeighbours READ hiddenNeighbours NOTIFY graphChanged)
    Q_PROPERTY(int ancestorCount READ ancestorCount NOTIFY graphChanged)
    Q_PROPERTY(int descendantCount READ descendantCount NOTIFY graphChanged)

    /// The six signal weights plus the gates. Read/write as one map; every write
    /// is validated, clamped and persisted before it can affect a rebuild.
    Q_PROPERTY(QVariantMap weights READ weights NOTIFY weightsChanged)
    Q_PROPERTY(int minConfidencePercent READ minConfidencePercent WRITE setMinConfidencePercent
                       NOTIFY weightsChanged)
    Q_PROPERTY(int ancestorDepth READ ancestorDepth WRITE setAncestorDepth NOTIFY weightsChanged)
    Q_PROPERTY(int descendantDepth READ descendantDepth WRITE setDescendantDepth NOTIFY
                       weightsChanged)

    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY statusChanged)
    Q_PROPERTY(int libraryCount READ libraryCount NOTIFY statusChanged)
    Q_PROPERTY(int inferredEdgeCount READ inferredEdgeCount NOTIFY statusChanged)
    Q_PROPERTY(int remoteEdgeCount READ remoteEdgeCount NOTIFY statusChanged)
    Q_PROPERTY(int lastRebuildMs READ lastRebuildMs NOTIFY statusChanged)

public:
    explicit SunoLineageBridge(QObject* parent = nullptr);
    ~SunoLineageBridge() override;

    /// The composition root's one injection point, matching `SunoBridge`'s.
    static void setSunoController(vc::suno::SunoController* controller);

    QVariantList nodes() const { return nodes_; }
    QVariantList edges() const { return edges_; }
    QString focusId() const { return focusId_; }
    QString focusTitle() const;
    int columns() const { return columns_; }
    int rows() const { return rows_; }
    bool truncated() const { return truncated_; }
    int hiddenNeighbours() const { return hiddenNeighbours_; }
    int ancestorCount() const;
    int descendantCount() const;

    QVariantMap weights() const;
    int minConfidencePercent() const;
    void setMinConfidencePercent(int percent);
    int ancestorDepth() const;
    void setAncestorDepth(int depth);
    int descendantDepth() const;
    void setDescendantDepth(int depth);

    QString status() const { return status_; }
    QString error() const { return error_; }
    bool busy() const { return pendingRemote_ > 0; }
    bool ready() const { return store_ != nullptr; }
    int libraryCount() const { return libraryCount_; }
    int inferredEdgeCount() const { return inferredEdgeCount_; }
    int remoteEdgeCount() const { return remoteEdgeCount_; }
    int lastRebuildMs() const { return lastRebuildMs_; }

public slots:
    /// Fingerprint the cached library and replace every inferred edge. Refuses
    /// while a library sync is in flight, because a partial cache would produce a
    /// partial graph that then looks like a real answer.
    Q_INVOKABLE void rebuild();
    /// Make `clipId` the graph's centre. Idempotent; costs one SQL read.
    Q_INVOKABLE void focusClip(const QString& clipId);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void clearInferred();
    /// Ask the server what this clip derives from and record it as `remote`.
    Q_INVOKABLE void fetchRemoteAncestry(const QString& clipId);
    /// Drop one inferred edge the user says is wrong. A remote edge is refused:
    /// the server's attribution is a fact, the heuristic is a guess.
    Q_INVOKABLE void rejectEdge(const QString& parentId);
    Q_INVOKABLE void setWeight(const QString& key, double value);
    Q_INVOKABLE void resetWeights();

signals:
    void graphChanged();
    void weightsChanged();
    void statusChanged();
    void errorChanged();
    void busyChanged();

private:
    bool ensureStore();
    void loadLibrary(std::vector<vc::suno::SunoClip>& clips) const;
    void publish(const vc::suno::lineage::Graph& graph);
    void publishEmptyGraph();
    void setStatus(const QString& message);
    void setError(const QString& message);
    void persistWeights();
    void loadWeights();
    void handleAttribution(QNetworkReply* reply, const QString& clipId);
    /// Drop the store and the published graph. Called when the controller is
    /// replaced; it lives here rather than at the call site so a signal is emitted
    /// from inside the object that owns the state.
    void detachController();

    static vc::suno::SunoController* s_controller;

    vc::suno::lineage::SunoLineageStore* store_{nullptr};
    vc::suno::lineage::Weights weights_;
    QVariantList nodes_;
    QVariantList edges_;
    QString focusId_;
    QString status_;
    QString error_;
    int columns_{0};
    int rows_{0};
    bool truncated_{false};
    int hiddenNeighbours_{0};
    int libraryCount_{0};
    int inferredEdgeCount_{0};
    int remoteEdgeCount_{0};
    int lastRebuildMs_{0};
    int pendingRemote_{0};
};

} // namespace qml_bridge
