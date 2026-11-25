#include "SunoLineageStore.hpp"

#include <QDateTime>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include "core/Logger.hpp"

namespace vc::suno::lineage {

namespace {

/// Deliberately not `suno_db`: one process-wide registration name per connection,
/// and `SunoDatabase` already owns that one.
constexpr auto kConnectionName = "suno_lineage_db";

Edge edgeFromQuery(const QSqlQuery& query) {
    Edge edge;
    edge.childId = query.value(0).toString().toStdString();
    edge.parentId = query.value(1).toString().toStdString();
    edge.relation = relationFromKey(query.value(2).toString().toStdString());
    edge.provenance = provenanceFromKey(query.value(3).toString().toStdString());
    edge.confidence = query.value(4).toDouble();
    edge.evidence = query.value(5).toString().toStdString();
    return edge;
}

QString text(std::string_view value) {
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

} // namespace

SunoLineageStore::SunoLineageStore() = default;

SunoLineageStore::~SunoLineageStore() {
    if (db_.isOpen()) {
        db_.close();
    }
    if (!db_.connectionName().isEmpty()) {
        const QString name = db_.connectionName();
        db_ = QSqlDatabase();
        QSqlDatabase::removeDatabase(name);
    }
}

Result<void> SunoLineageStore::init(const std::string& dbPath) {
    if (QSqlDatabase::contains(kConnectionName)) {
        return Result<void>::err("A lineage database connection already exists in this process; "
                                 "init() refuses to re-register it");
    }
    path_ = dbPath;
    db_ = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), kConnectionName);
    db_.setDatabaseName(QString::fromStdString(dbPath));
    if (!db_.open()) {
        return Result<void>::err("Failed to open the lineage database: " +
                                 db_.lastError().text().toStdString());
    }

    const bool inMemory = dbPath == ":memory:";
    QSqlQuery pragma(db_);
    if (!inMemory) {
        // Same reasoning as SunoDatabase: WAL keeps the library reader unblocked
        // while a rebuild writes, and NORMAL is its documented companion.
        if (!pragma.exec(QStringLiteral("PRAGMA journal_mode=WAL")) || !pragma.next() ||
            pragma.value(0).toString().compare(QStringLiteral("wal"), Qt::CaseInsensitive) != 0) {
            return Result<void>::err("Failed to enable WAL journal mode for lineage: " +
                                     pragma.lastError().text().toStdString());
        }
    }
    if (!pragma.exec(QStringLiteral("PRAGMA synchronous=NORMAL")) ||
        !pragma.exec(QStringLiteral("PRAGMA busy_timeout=5000"))) {
        return Result<void>::err("Failed to set lineage database pragmas: " +
                                 pragma.lastError().text().toStdString());
    }

    if (auto schema = ensureSchema(); !schema) return schema;
    initialized_ = true;
    LOG_INFO("Suno lineage store initialized at {}", dbPath);
    return Result<void>::ok();
}

Result<void> SunoLineageStore::ensureSchema() {
    QSqlQuery query(db_);
    if (!query.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS lineage_edges ("
                                   "child_id TEXT NOT NULL, "
                                   "parent_id TEXT NOT NULL, "
                                   "relation TEXT NOT NULL DEFAULT 'unknown', "
                                   "provenance TEXT NOT NULL DEFAULT 'inferred', "
                                   "confidence REAL NOT NULL DEFAULT 0, "
                                   "evidence TEXT NOT NULL DEFAULT '', "
                                   "updated_at INTEGER NOT NULL DEFAULT 0, "
                                   "PRIMARY KEY (child_id, parent_id))"))) {
        return Result<void>::err("Failed to create lineage_edges: " +
                                 query.lastError().text().toStdString());
    }
    // The upward traversal asks "who derives from this clip" on every focus change.
    if (!query.exec(QStringLiteral(
                "CREATE INDEX IF NOT EXISTS idx_lineage_edges_parent ON lineage_edges (parent_id)"))) {
        return Result<void>::err("Failed to index lineage_edges.parent_id: " +
                                 query.lastError().text().toStdString());
    }
    if (!query.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_lineage_edges_provenance "
                                   "ON lineage_edges (provenance)"))) {
        return Result<void>::err("Failed to index lineage_edges.provenance: " +
                                 query.lastError().text().toStdString());
    }
    if (!query.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS lineage_meta ("
                                   "key TEXT PRIMARY KEY, "
                                   "value TEXT NOT NULL)"))) {
        return Result<void>::err("Failed to create lineage_meta: " +
                                 query.lastError().text().toStdString());
    }
    return Result<void>::ok();
}

namespace {

/// One prepared statement for both write paths.
///
/// The `WHERE` clause on the upsert is the invariant: an inferred row may be
/// refreshed by anything, a remote row may only be refreshed by another remote
/// row. Written in SQL so no future caller can lose it.
Result<void> writeEdges(QSqlDatabase& db, const std::vector<Edge>& edges) {
    if (edges.empty()) return Result<void>::ok();
    QSqlQuery query(db);
    if (!query.prepare(QStringLiteral(
                "INSERT INTO lineage_edges "
                "(child_id, parent_id, relation, provenance, confidence, evidence, updated_at) "
                "VALUES (:child, :parent, :relation, :provenance, :confidence, :evidence, :now) "
                "ON CONFLICT(child_id, parent_id) DO UPDATE SET "
                "relation=excluded.relation, provenance=excluded.provenance, "
                "confidence=excluded.confidence, evidence=excluded.evidence, "
                "updated_at=excluded.updated_at "
                "WHERE excluded.provenance='remote' OR lineage_edges.provenance!='remote'"))) {
        return Result<void>::err("Failed to prepare lineage upsert: " +
                                 query.lastError().text().toStdString());
    }
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    for (const auto& edge : edges) {
        if (edge.childId.empty() || edge.parentId.empty() || edge.childId == edge.parentId) {
            continue;
        }
        query.bindValue(QStringLiteral(":child"), text(edge.childId));
        query.bindValue(QStringLiteral(":parent"), text(edge.parentId));
        query.bindValue(QStringLiteral(":relation"), text(relationKey(edge.relation)));
        query.bindValue(QStringLiteral(":provenance"), text(provenanceKey(edge.provenance)));
        query.bindValue(QStringLiteral(":confidence"), edge.confidence);
        query.bindValue(QStringLiteral(":evidence"), text(edge.evidence));
        query.bindValue(QStringLiteral(":now"), now);
        if (!query.exec()) {
            return Result<void>::err("Failed to write a lineage edge: " +
                                     query.lastError().text().toStdString());
        }
    }
    return Result<void>::ok();
}

} // namespace

Result<void> SunoLineageStore::replaceInferred(const std::vector<Edge>& edges) {
    if (!initialized_) return Result<void>::err("Lineage database not initialized");
    if (!db_.transaction()) {
        return Result<void>::err("Failed to begin a lineage transaction: " +
                                 db_.lastError().text().toStdString());
    }
    QSqlQuery clear(db_);
    if (!clear.exec(QStringLiteral("DELETE FROM lineage_edges WHERE provenance='inferred'"))) {
        db_.rollback();
        return Result<void>::err("Failed to clear inferred edges: " +
                                 clear.lastError().text().toStdString());
    }
    auto written = writeEdges(db_, edges);
    if (!written) {
        db_.rollback();
        return written;
    }
    if (!db_.commit()) {
        const std::string message = db_.lastError().text().toStdString();
        db_.rollback();
        return Result<void>::err("Failed to commit lineage edges: " + message);
    }
    return Result<void>::ok();
}

Result<void> SunoLineageStore::upsert(const std::vector<Edge>& edges) {
    if (!initialized_) return Result<void>::err("Lineage database not initialized");
    return writeEdges(db_, edges);
}

Result<std::vector<Edge>> SunoLineageStore::all() {
    if (!initialized_) return Result<std::vector<Edge>>::err("Lineage database not initialized");
    QSqlQuery query(db_);
    if (!query.exec(QStringLiteral("SELECT child_id, parent_id, relation, provenance, confidence, "
                                   "evidence FROM lineage_edges"))) {
        return Result<std::vector<Edge>>::err("Failed to read lineage edges: " +
                                              query.lastError().text().toStdString());
    }
    std::vector<Edge> edges;
    while (query.next()) {
        edges.push_back(edgeFromQuery(query));
    }
    return Result<std::vector<Edge>>::ok(std::move(edges));
}

Result<std::vector<Edge>> SunoLineageStore::neighbourhood(const std::string& clipId, int limit) {
    if (!initialized_) return Result<std::vector<Edge>>::err("Lineage database not initialized");
    if (clipId.empty()) return Result<std::vector<Edge>>::ok({});
    QSqlQuery query(db_);
    if (!query.prepare(QStringLiteral(
                "SELECT child_id, parent_id, relation, provenance, confidence, evidence "
                "FROM lineage_edges WHERE child_id = :id OR parent_id = :id "
                "ORDER BY confidence DESC, child_id, parent_id LIMIT :limit"))) {
        return Result<std::vector<Edge>>::err("Failed to prepare a lineage lookup: " +
                                              query.lastError().text().toStdString());
    }
    query.bindValue(QStringLiteral(":id"), QString::fromStdString(clipId));
    query.bindValue(QStringLiteral(":limit"), std::max(limit, 1));
    if (!query.exec()) {
        return Result<std::vector<Edge>>::err("Failed to read a lineage neighbourhood: " +
                                              query.lastError().text().toStdString());
    }
    std::vector<Edge> edges;
    while (query.next()) {
        edges.push_back(edgeFromQuery(query));
    }
    return Result<std::vector<Edge>>::ok(std::move(edges));
}

Result<std::size_t> SunoLineageStore::inferredCount() {
    if (!initialized_) return Result<std::size_t>::err("Lineage database not initialized");
    QSqlQuery query(db_);
    if (!query.exec(QStringLiteral(
                "SELECT COUNT(*) FROM lineage_edges WHERE provenance='inferred'")) ||
        !query.next()) {
        return Result<std::size_t>::err("Failed to count inferred edges: " +
                                        query.lastError().text().toStdString());
    }
    return Result<std::size_t>::ok(static_cast<std::size_t>(query.value(0).toLongLong()));
}

Result<int> SunoLineageStore::removeInferred() {
    if (!initialized_) return Result<int>::err("Lineage database not initialized");
    QSqlQuery query(db_);
    if (!query.exec(QStringLiteral("DELETE FROM lineage_edges WHERE provenance='inferred'"))) {
        return Result<int>::err("Failed to clear inferred edges: " +
                                query.lastError().text().toStdString());
    }
    return Result<int>::ok(std::max(query.numRowsAffected(), 0));
}

Result<void> SunoLineageStore::removeEdge(const std::string& childId, const std::string& parentId) {
    if (!initialized_) return Result<void>::err("Lineage database not initialized");
    QSqlQuery query(db_);
    if (!query.prepare(QStringLiteral(
                "DELETE FROM lineage_edges WHERE child_id = :child AND parent_id = :parent"))) {
        return Result<void>::err("Failed to prepare an edge delete: " +
                                 query.lastError().text().toStdString());
    }
    query.bindValue(QStringLiteral(":child"), QString::fromStdString(childId));
    query.bindValue(QStringLiteral(":parent"), QString::fromStdString(parentId));
    if (!query.exec()) {
        return Result<void>::err("Failed to delete a lineage edge: " +
                                 query.lastError().text().toStdString());
    }
    return Result<void>::ok();
}

Result<void> SunoLineageStore::setMeta(const std::string& key, const std::string& value) {
    if (!initialized_) return Result<void>::err("Lineage database not initialized");
    QSqlQuery query(db_);
    if (!query.prepare(QStringLiteral("INSERT INTO lineage_meta (key, value) VALUES (:key, :value) "
                                      "ON CONFLICT(key) DO UPDATE SET value=excluded.value"))) {
        return Result<void>::err("Failed to prepare a lineage meta write: " +
                                 query.lastError().text().toStdString());
    }
    query.bindValue(QStringLiteral(":key"), QString::fromStdString(key));
    query.bindValue(QStringLiteral(":value"), QString::fromStdString(value));
    if (!query.exec()) {
        return Result<void>::err("Failed to write lineage meta: " +
                                 query.lastError().text().toStdString());
    }
    return Result<void>::ok();
}

Result<std::string> SunoLineageStore::meta(const std::string& key, const std::string& fallback) {
    if (!initialized_) return Result<std::string>::err("Lineage database not initialized");
    QSqlQuery query(db_);
    if (!query.prepare(QStringLiteral("SELECT value FROM lineage_meta WHERE key = :key"))) {
        return Result<std::string>::err("Failed to prepare a lineage meta read: " +
                                        query.lastError().text().toStdString());
    }
    query.bindValue(QStringLiteral(":key"), QString::fromStdString(key));
    if (!query.exec()) {
        return Result<std::string>::err("Failed to read lineage meta: " +
                                        query.lastError().text().toStdString());
    }
    if (query.next()) return Result<std::string>::ok(query.value(0).toString().toStdString());
    return Result<std::string>::ok(fallback);
}

} // namespace vc::suno::lineage
