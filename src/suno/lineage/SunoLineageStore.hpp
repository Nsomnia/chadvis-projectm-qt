#pragma once
// SunoLineageStore.hpp - durable lineage edges, beside the library cache.
//
// A separate SQLite connection to the SAME file as `SunoDatabase`, with its own
// tables. Two reasons, both deliberate:
//
//   * `SunoDatabase` owns the clip cache and is already at its size budget; the
//     lineage tables have a different lifecycle (rebuilt wholesale, replaced on
//     every inference pass) and mixing them would make one migration serve two
//     unrelated schemas.
//   * WAL means a reader and a writer of different connections do not block each
//     other, which is what lets a rebuild write edges while the library view is
//     still reading clips.
//
// The one invariant worth stating loudly: a REMOTE edge (server attribution) is
// never overwritten by an INFERRED one. That is enforced in SQL, not in the
// caller, because a caller can be forgotten and a WHERE clause cannot.

#include <cstddef>
#include <string>
#include <vector>

#include "LineageTypes.hpp"
#include "util/Result.hpp"

#include <QSqlDatabase>

namespace vc::suno::lineage {

class SunoLineageStore {
public:
    SunoLineageStore();
    ~SunoLineageStore();

    SunoLineageStore(const SunoLineageStore&) = delete;
    SunoLineageStore& operator=(const SunoLineageStore&) = delete;

    /// Open `dbPath` (the library cache file) and create the lineage tables if
    /// they are absent. `":memory:"` is supported for tests and skips WAL.
    Result<void> init(const std::string& dbPath);

    [[nodiscard]] bool isOpen() const { return initialized_; }
    [[nodiscard]] const std::string& path() const { return path_; }

    /// Replace every inferred edge in one transaction. Used by a rebuild: the
    /// result must be exactly what the last pass produced, not an accumulation.
    Result<void> replaceInferred(const std::vector<Edge>& edges);

    /// Insert or refresh edges without ever downgrading a remote row.
    Result<void> upsert(const std::vector<Edge>& edges);

    Result<std::vector<Edge>> all();
    Result<std::vector<Edge>> neighbourhood(const std::string& clipId, int limit = 64);
    Result<std::size_t> inferredCount();
    Result<int> removeInferred();
    Result<void> removeEdge(const std::string& childId, const std::string& parentId);

    Result<void> setMeta(const std::string& key, const std::string& value);
    Result<std::string> meta(const std::string& key, const std::string& fallback = {});

private:
    Result<void> ensureSchema();

    QSqlDatabase db_;
    std::string path_;
    bool initialized_{false};
};

} // namespace vc::suno::lineage
