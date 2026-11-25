#include "SunoLineageBridge.hpp"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <expected>
#include <unordered_map>
#include <vector>

#include "core/Logger.hpp"
#include "suno/SunoClient.hpp"
#include "suno/SunoEndpoints.hpp"
#include "suno/lineage/LineageFingerprint.hpp"
#include "suno/lineage/LineageGraph.hpp"
#include "suno/lineage/LineageInference.hpp"
#include "suno/lineage/SunoLineageStore.hpp"
#include "ui/controllers/SunoController.hpp"

namespace qml_bridge {

namespace {

constexpr auto kWeightsKey = "weights";

QString text(const std::string& value) {
    return QString::fromStdString(value);
}

QString roleKey(vc::suno::lineage::Role role) {
    switch (role) {
    case vc::suno::lineage::Role::Ancestor:
        return QStringLiteral("ancestor");
    case vc::suno::lineage::Role::Descendant:
        return QStringLiteral("descendant");
    case vc::suno::lineage::Role::Focus:
        break;
    }
    return QStringLiteral("focus");
}

/// "1 link" / "2 links" without a `const char*` implicit conversion at every call
/// site (the tree builds with ASCII casts disabled in some consumer configs).
template <typename Count>
QString plural(Count count) {
    return count == 1 ? QString() : QStringLiteral("s");
}

} // namespace

vc::suno::SunoController* SunoLineageBridge::s_controller = nullptr;

SunoLineageBridge::SunoLineageBridge(QObject* parent) : QObject(parent) {
    setInstance(this);
    weights_ = vc::suno::lineage::Weights{};
}

SunoLineageBridge::~SunoLineageBridge() {
    delete store_;
    store_ = nullptr;
}

void SunoLineageBridge::setSunoController(vc::suno::SunoController* controller) {
    s_controller = controller;
    if (auto* bridge = instance()) {
        bridge->detachController();
    }
}

void SunoLineageBridge::detachController() {
    // The store cannot be re-opened in place (the connection name is process-wide),
    // so it is dropped; the next query opens whatever file the new controller owns.
    delete store_;
    store_ = nullptr;
    nodes_.clear();
    edges_.clear();
    focusId_.clear();
    columns_ = 0;
    rows_ = 0;
    inferredEdgeCount_ = 0;
    remoteEdgeCount_ = 0;
    emit graphChanged();
}

// ── Store plumbing ──────────────────────────────────────────────────────────

bool SunoLineageBridge::ensureStore() {
    if (store_ != nullptr) return true;
    if (s_controller == nullptr) {
        setError(QStringLiteral("The Suno library is not connected yet."));
        return false;
    }
    // The library cache file, so both schemas live in one file and one backup.
    const std::string& path = s_controller->db().path();
    if (path.empty()) {
        setError(QStringLiteral("The library cache has no database file yet."));
        return false;
    }
    store_ = new vc::suno::lineage::SunoLineageStore();
    if (auto opened = store_->init(path); !opened) {
        setError(QStringLiteral("The lineage store could not be opened: %1")
                         .arg(text(opened.error().message)));
        delete store_;
        store_ = nullptr;
        return false;
    }
    loadWeights();
    emit statusChanged();
    return true;
}

void SunoLineageBridge::loadLibrary(std::vector<vc::suno::SunoClip>& clips) const {
    clips.clear();
    if (s_controller == nullptr) return;
    auto stored = s_controller->db().getAllClips();
    if (!stored) {
        LOG_WARN("SunoLineageBridge: cannot read the library cache: {}",
                 stored.error().message);
        return;
    }
    clips = std::move(stored.value());
}

// ── Weights ─────────────────────────────────────────────────────────────────

QVariantMap SunoLineageBridge::weights() const {
    return vc::suno::lineage::toVariantMap(weights_);
}

int SunoLineageBridge::minConfidencePercent() const {
    return static_cast<int>(weights_.threshold * 100.0 + 0.5);
}

void SunoLineageBridge::setMinConfidencePercent(int percent) {
    const int clamped = std::clamp(percent, 0, 100);
    const double next = static_cast<double>(clamped) / 100.0;
    if (std::abs(next - weights_.threshold) < 0.0005) return;
    weights_.threshold = next;
    persistWeights();
    emit weightsChanged();
    if (!focusId_.isEmpty()) refresh();
}

int SunoLineageBridge::ancestorDepth() const {
    return weights_.ancestorDepth;
}

void SunoLineageBridge::setAncestorDepth(int depth) {
    const int clamped = std::clamp(depth, 1, 12);
    if (clamped == weights_.ancestorDepth) return;
    weights_.ancestorDepth = clamped;
    persistWeights();
    emit weightsChanged();
    if (!focusId_.isEmpty()) refresh();
}

int SunoLineageBridge::descendantDepth() const {
    return weights_.descendantDepth;
}

void SunoLineageBridge::setDescendantDepth(int depth) {
    const int clamped = std::clamp(depth, 1, 12);
    if (clamped == weights_.descendantDepth) return;
    weights_.descendantDepth = clamped;
    persistWeights();
    emit weightsChanged();
    if (!focusId_.isEmpty()) refresh();
}

void SunoLineageBridge::setWeight(const QString& key, double value) {
    QVariantMap values = vc::suno::lineage::toVariantMap(weights_);
    if (!values.contains(key)) {
        setError(QStringLiteral("'%1' is not a lineage weight.").arg(key));
        return;
    }
    values.insert(key, value);
    vc::suno::lineage::Weights candidate = vc::suno::lineage::fromVariantMap(values);
    // A bad slider value is clamped, never rejected: the panel is live and a
    // refusal loop between the UI and the model would fight the user's drag.
    const QStringList repaired = vc::suno::lineage::sanitize(candidate);
    weights_ = candidate;
    persistWeights();
    emit weightsChanged();
    if (repaired.contains(key)) {
        LOG_INFO("SunoLineageBridge: weight '{}' clamped while setting {}", key.toStdString(),
                 value);
    }
}

void SunoLineageBridge::resetWeights() {
    weights_ = vc::suno::lineage::Weights{};
    persistWeights();
    emit weightsChanged();
    if (!focusId_.isEmpty()) refresh();
}

void SunoLineageBridge::loadWeights() {
    if (store_ == nullptr) return;
    auto stored = store_->meta(kWeightsKey);
    if (!stored || stored.value().empty()) {
        weights_ = vc::suno::lineage::Weights{};
        return;
    }
    const QJsonDocument document =
            QJsonDocument::fromJson(QByteArray::fromStdString(stored.value()));
    if (!document.isObject()) {
        weights_ = vc::suno::lineage::Weights{};
        return;
    }
    weights_ = vc::suno::lineage::fromVariantMap(document.object().toVariantMap());
    const QStringList repaired = vc::suno::lineage::sanitize(weights_);
    if (!repaired.isEmpty()) {
        LOG_WARN("SunoLineageBridge: repaired lineage weights: {}",
                 repaired.join(QStringLiteral(", ")).toStdString());
        persistWeights();
    }
}

void SunoLineageBridge::persistWeights() {
    if (store_ == nullptr) return;
    const QJsonDocument document(
            QJsonObject::fromVariantMap(vc::suno::lineage::toVariantMap(weights_)));
    auto written = store_->setMeta(
            kWeightsKey, document.toJson(QJsonDocument::Compact).toStdString());
    if (!written) {
        LOG_WARN("SunoLineageBridge: could not persist lineage weights: {}",
                 written.error().message);
    }
}

// ── Rebuild ─────────────────────────────────────────────────────────────────

void SunoLineageBridge::rebuild() {
    if (!ensureStore()) return;
    std::vector<vc::suno::SunoClip> clips;
    loadLibrary(clips);
    if (clips.empty()) {
        setError(QStringLiteral("No cached library to analyse yet — refresh the library first."));
        return;
    }

    // Clips are read from one connection and edges written to another. The read
    // happens fully before the write, so the transaction never holds a write lock
    // across a read of the same file.
    QElapsedTimer timer;
    timer.start();
    std::vector<vc::suno::lineage::Fingerprint> prints;
    prints.reserve(clips.size());
    for (const auto& clip : clips) {
        auto print = vc::suno::lineage::fingerprint(clip);
        if (print.valid) prints.push_back(std::move(print));
    }

    const auto inferred = vc::suno::lineage::inferEdges(prints, weights_);
    if (auto written = store_->replaceInferred(inferred.edges); !written) {
        setError(QStringLiteral("The lineage rebuild could not be stored: %1")
                         .arg(text(written.error().message)));
        return;
    }

    libraryCount_ = static_cast<int>(prints.size());
    lastRebuildMs_ = static_cast<int>(timer.elapsed());
    error_.clear();
    emit errorChanged();

    QString summary = QStringLiteral("Scored %1 pair%2 across %3 clips; %4 link%5 stored in "
                                     "%6 ms.")
                              .arg(inferred.stats.pairsScored)
                              .arg(plural(inferred.stats.pairsScored))
                              .arg(libraryCount_)
                              .arg(inferred.edges.size())
                              .arg(plural(inferred.edges.size()))
                              .arg(lastRebuildMs_);
    if (inferred.stats.pairCapHit) {
        summary += QStringLiteral(" The comparison ceiling was reached, so this is a partial "
                                  "rebuild — raise the threshold or narrow the library.");
    }
    if (inferred.stats.bucketCapHit) {
        summary += QStringLiteral(" Some style tags are shared by too many clips to compare.");
    }
    setStatus(summary);
    LOG_INFO("SunoLineageBridge: {} clips, {} candidates, {} pairs, {} edges, {} ms",
             inferred.stats.clips, inferred.stats.candidates, inferred.stats.pairsScored,
             inferred.edges.size(), lastRebuildMs_);

    if (focusId_.isEmpty() && !prints.empty()) {
        // First rebuild has no focus: centre on the newest clip, which is the one a
        // power user is most likely looking at.
        focusId_ = text(prints.front().id);
    }
    refresh();
}

// ── Graph ───────────────────────────────────────────────────────────────────

void SunoLineageBridge::focusClip(const QString& clipId) {
    if (clipId.isEmpty()) return;
    if (!ensureStore()) return;
    if (clipId == focusId_ && !nodes_.isEmpty()) return;
    focusId_ = clipId;
    refresh();
}

void SunoLineageBridge::refresh() {
    if (!ensureStore()) return;
    if (focusId_.isEmpty()) {
        publishEmptyGraph();
        return;
    }

    auto stored = store_->all();
    if (!stored) {
        setError(QStringLiteral("The lineage graph could not be read: %1")
                         .arg(text(stored.error().message)));
        return;
    }

    std::vector<vc::suno::SunoClip> clips;
    loadLibrary(clips);
    libraryCount_ = static_cast<int>(clips.size());
    std::unordered_map<std::string, const vc::suno::SunoClip*> byId;
    byId.reserve(clips.size());
    for (const auto& clip : clips) {
        byId.emplace(clip.id, &clip);
    }

    vc::suno::lineage::Options options;
    options.ancestorDepth = weights_.ancestorDepth;
    options.descendantDepth = weights_.descendantDepth;
    // The graph floor follows the same confidence the rebuild uses, so raising one
    // dial never leaves the other showing edges the model would not produce.
    options.minConfidence = weights_.threshold;

    const vc::suno::lineage::InfoProvider provider = [&byId](std::string_view id) {
        vc::suno::lineage::NodeInfo info;
        const auto found = byId.find(std::string(id));
        if (found == byId.end()) return info;
        const vc::suno::SunoClip& clip = *found->second;
        info.title = clip.title;
        info.tags = clip.metadata.tags;
        info.createdAt = clip.created_at;
        info.modelName = !clip.model_name.empty() ? clip.model_name : clip.major_model_version;
        info.playCount = clip.play_count;
        info.instrumental = clip.metadata.make_instrumental;
        return info;
    };

    publish(vc::suno::lineage::buildGraph(stored.value(), focusId_.toStdString(), options, provider));

    inferredEdgeCount_ = 0;
    remoteEdgeCount_ = 0;
    for (const auto& edge : stored.value()) {
        if (edge.provenance == vc::suno::lineage::Provenance::Remote) {
            ++remoteEdgeCount_;
        } else {
            ++inferredEdgeCount_;
        }
    }
    if (nodes_.size() == 1) {
        setStatus(QStringLiteral("Nothing is linked to “%1” yet. Rebuild from the library, or "
                                 "fetch the server's attribution for it.")
                          .arg(focusTitle()));
    } else if (status_.isEmpty()) {
        setStatus(QStringLiteral("%1 related clip%2, %3 link%4.")
                          .arg(nodes_.size() - 1)
                          .arg(plural(nodes_.size() - 1))
                          .arg(edges_.size())
                          .arg(plural(edges_.size())));
    }
    emit statusChanged();
}

void SunoLineageBridge::publishEmptyGraph() {
    nodes_.clear();
    edges_.clear();
    columns_ = 0;
    rows_ = 0;
    truncated_ = false;
    hiddenNeighbours_ = 0;
    emit graphChanged();
}

void SunoLineageBridge::publish(const vc::suno::lineage::Graph& graph) {
    QVariantList nodes;
    nodes.reserve(static_cast<qsizetype>(graph.nodes.size()));
    for (const auto& node : graph.nodes) {
        QVariantMap map;
        map.insert(QStringLiteral("id"), text(node.id));
        map.insert(QStringLiteral("title"), text(node.info.title));
        map.insert(QStringLiteral("tags"), text(node.info.tags));
        map.insert(QStringLiteral("createdAt"), text(node.info.createdAt));
        map.insert(QStringLiteral("model"), text(node.info.modelName));
        map.insert(QStringLiteral("playCount"), static_cast<qlonglong>(node.info.playCount));
        map.insert(QStringLiteral("instrumental"), node.info.instrumental);
        map.insert(QStringLiteral("role"), roleKey(node.role));
        map.insert(QStringLiteral("depth"), node.depth);
        map.insert(QStringLiteral("column"), node.column);
        map.insert(QStringLiteral("row"), node.row);
        map.insert(QStringLiteral("linkedTo"), static_cast<qlonglong>(node.linkedTo));
        map.insert(QStringLiteral("relation"),
                   text(std::string(vc::suno::lineage::relationKey(node.relation))));
        map.insert(QStringLiteral("provenance"),
                   text(std::string(vc::suno::lineage::provenanceKey(node.provenance))));
        map.insert(QStringLiteral("confidence"), node.confidence);
        nodes.push_back(map);
    }

    QVariantList edges;
    edges.reserve(static_cast<qsizetype>(graph.edges.size()));
    for (const auto& edge : graph.edges) {
        QVariantMap map;
        map.insert(QStringLiteral("from"), text(edge.childId));
        map.insert(QStringLiteral("to"), text(edge.parentId));
        map.insert(QStringLiteral("relation"),
                   text(std::string(vc::suno::lineage::relationKey(edge.relation))));
        map.insert(QStringLiteral("provenance"),
                   text(std::string(vc::suno::lineage::provenanceKey(edge.provenance))));
        map.insert(QStringLiteral("confidence"), edge.confidence);
        map.insert(QStringLiteral("evidence"), text(edge.evidence));
        edges.push_back(map);
    }

    nodes_ = std::move(nodes);
    edges_ = std::move(edges);
    columns_ = graph.columns;
    rows_ = graph.rows;
    truncated_ = graph.truncated;
    hiddenNeighbours_ = static_cast<int>(graph.hiddenNeighbours);
    emit graphChanged();
}

QString SunoLineageBridge::focusTitle() const {
    for (const auto& entry : nodes_) {
        const QVariantMap map = entry.toMap();
        if (map.value(QStringLiteral("role")).toString() == QStringLiteral("focus")) {
            const QString title = map.value(QStringLiteral("title")).toString();
            return title.isEmpty() ? map.value(QStringLiteral("id")).toString() : title;
        }
    }
    return {};
}

int SunoLineageBridge::ancestorCount() const {
    return static_cast<int>(std::count_if(nodes_.begin(), nodes_.end(), [](const QVariant& entry) {
        return entry.toMap().value(QStringLiteral("role")).toString() == QStringLiteral("ancestor");
    }));
}

int SunoLineageBridge::descendantCount() const {
    return static_cast<int>(std::count_if(nodes_.begin(), nodes_.end(), [](const QVariant& entry) {
        return entry.toMap().value(QStringLiteral("role")).toString() ==
               QStringLiteral("descendant");
    }));
}

// ── Cleanup and correction ──────────────────────────────────────────────────

void SunoLineageBridge::clearInferred() {
    if (!ensureStore()) return;
    auto removed = store_->removeInferred();
    if (!removed) {
        setError(QStringLiteral("The inferred links could not be cleared: %1")
                         .arg(text(removed.error().message)));
        return;
    }
    setStatus(QStringLiteral("Removed %1 inferred link%2. Library data is untouched.")
                      .arg(removed.value())
                      .arg(plural(removed.value())));
    refresh();
}

void SunoLineageBridge::rejectEdge(const QString& parentId) {
    if (!ensureStore() || focusId_.isEmpty() || parentId.isEmpty()) return;
    auto nearby = store_->neighbourhood(focusId_.toStdString());
    if (nearby) {
        for (const auto& edge : nearby.value()) {
            if (edge.childId != focusId_.toStdString() || edge.parentId != parentId.toStdString()) {
                continue;
            }
            if (edge.provenance == vc::suno::lineage::Provenance::Remote) {
                setError(QStringLiteral("That link came from Suno's own attribution and cannot be "
                                        "removed locally."));
                return;
            }
            break;
        }
    }
    auto removed = store_->removeEdge(focusId_.toStdString(), parentId.toStdString());
    if (!removed) {
        setError(QStringLiteral("That link could not be removed: %1")
                         .arg(text(removed.error().message)));
        return;
    }
    setStatus(QStringLiteral("Link removed. It stays gone until the next rebuild."));
    refresh();
}

// ── Remote attribution ──────────────────────────────────────────────────────

void SunoLineageBridge::fetchRemoteAncestry(const QString& clipId) {
    if (!ensureStore()) return;
    if (s_controller == nullptr || s_controller->client() == nullptr) {
        setError(QStringLiteral("The Suno client is not available yet."));
        return;
    }
    if (!s_controller->client()->isAuthenticated()) {
        setError(QStringLiteral("Sign in to Suno to read the server's attribution."));
        return;
    }
    if (clipId.isEmpty()) return;

    QString path = vc::suno::qstr(vc::suno::endpoints::CLIP_ATTRIBUTION);
    path.replace(QStringLiteral("{}"),
                 QString::fromUtf8(QUrl::toPercentEncoding(clipId)));

    ++pendingRemote_;
    emit busyChanged();
    QPointer<SunoLineageBridge> guard(this);
    s_controller->client()->enqueueAuthenticatedRequest(
            path, "GET", {}, [guard, clipId](QNetworkReply* reply) {
                if (!guard) {
                    reply->deleteLater();
                    return;
                }
                guard->handleAttribution(reply, clipId);
            });
}

void SunoLineageBridge::handleAttribution(QNetworkReply* reply, const QString& clipId) {
    const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    auto body = (s_controller != nullptr && s_controller->client() != nullptr)
                        ? s_controller->client()->readTrackedBody(reply)
                        : std::expected<QByteArray, QString>(
                                  QStringLiteral("The Suno client is no longer available."));
    reply->deleteLater();

    --pendingRemote_;
    if (pendingRemote_ < 0) pendingRemote_ = 0;
    emit busyChanged();

    if (!body || status < 200 || status >= 300) {
        setError(QStringLiteral("The server did not answer the attribution request for this "
                                "clip."));
        return;
    }
    const QJsonDocument document = QJsonDocument::fromJson(*body);
    if (!document.isObject()) {
        setError(QStringLiteral("The attribution reply was not readable."));
        return;
    }
    const QJsonArray sources = document.object().value(QStringLiteral("source_clips")).toArray();
    if (sources.isEmpty()) {
        setStatus(QStringLiteral("Suno lists no upstream source for this clip — it is an "
                                 "original as far as the server knows."));
        return;
    }

    std::vector<vc::suno::lineage::Edge> recorded;
    recorded.reserve(static_cast<std::size_t>(sources.size()));
    for (const QJsonValue& entry : sources) {
        if (!entry.isObject()) continue;
        const QJsonObject object = entry.toObject();
        const QJsonValue nested = object.value(QStringLiteral("clip"));
        const QJsonObject clipObject = nested.isObject() ? nested.toObject() : object;
        const QString parentId = clipObject.value(QStringLiteral("id")).toString();
        if (parentId.isEmpty() || parentId == clipId) continue;
        vc::suno::lineage::Edge edge;
        edge.childId = clipId.toStdString();
        edge.parentId = parentId.toStdString();
        edge.relation = vc::suno::lineage::relationFromWire(
                object.value(QStringLiteral("relationship")).toString().toStdString());
        edge.provenance = vc::suno::lineage::Provenance::Remote;
        // Server attribution is a statement, not a similarity: full weight.
        edge.confidence = 1.0;
        edge.evidence = "server attribution";
        recorded.push_back(std::move(edge));
    }

    if (recorded.empty()) {
        setStatus(QStringLiteral("The attribution reply named no source clip this client could "
                                 "use."));
        return;
    }
    if (auto written = store_->upsert(recorded); !written) {
        setError(QStringLiteral("The server's attribution could not be stored: %1")
                         .arg(text(written.error().message)));
        return;
    }
    setStatus(QStringLiteral("Recorded %1 upstream source%2 from the server.")
                      .arg(recorded.size())
                      .arg(plural(recorded.size())));
    if (focusId_.isEmpty()) focusId_ = clipId;
    refresh();
}

} // namespace qml_bridge
