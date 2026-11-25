#include "LineageGraph.hpp"

#include <algorithm>
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace vc::suno::lineage {

namespace {

struct Adjacency {
    /// clip -> its parents, highest confidence first.
    std::unordered_map<std::string, std::vector<const Edge*>> parents;
    std::unordered_map<std::string, std::vector<const Edge*>> children;

    void add(const Edge& edge) {
        parents[edge.childId].push_back(&edge);
        children[edge.parentId].push_back(&edge);
    }

    void sort() {
        const auto byConfidence = [](const Edge* lhs, const Edge* rhs) {
            if (lhs->confidence != rhs->confidence) return lhs->confidence > rhs->confidence;
            return lhs->childId < rhs->childId || lhs->parentId < rhs->parentId;
        };
        for (auto& entry : parents) {
            std::sort(entry.second.begin(), entry.second.end(), byConfidence);
        }
        for (auto& entry : children) {
            std::sort(entry.second.begin(), entry.second.end(), byConfidence);
        }
    }
};

} // namespace

Graph buildGraph(const std::vector<Edge>& edges, std::string_view focusId, const Options& options,
                 const InfoProvider& info) {
    Graph graph;
    if (focusId.empty()) return graph;

    const double floor = std::clamp(options.minConfidence, 0.0, 1.0);
    Adjacency adjacency;
    for (const auto& edge : edges) {
        if (edge.confidence < floor) continue;
        if (edge.childId.empty() || edge.parentId.empty()) continue;
        if (edge.childId == edge.parentId) continue; // never render a self-loop
        adjacency.add(edge);
    }
    adjacency.sort();

    const std::size_t maxNodes = std::max<std::size_t>(options.maxNodes, 1);
    const int upDepth = std::clamp(options.ancestorDepth, 0, 12);
    const int downDepth = std::clamp(options.descendantDepth, 0, 12);

    std::unordered_map<std::string, std::size_t> indexOf;
    const auto provider = info ? info : [](std::string_view) { return NodeInfo{}; };
    const auto emplace = [&](const std::string& id, Role role, int depth, std::size_t linkedTo,
                             Relation relation, Provenance provenance, double confidence) {
        const std::size_t index = graph.nodes.size();
        Node node;
        node.id = id;
        node.info = provider(id);
        node.role = role;
        node.depth = depth;
        node.linkedTo = linkedTo;
        node.relation = relation;
        node.provenance = provenance;
        node.confidence = confidence;
        graph.nodes.push_back(std::move(node));
        indexOf.emplace(id, index);
        return index;
    };

    emplace(std::string(focusId), Role::Focus, 0, 0, Relation::Unknown, Provenance::Inferred, 0.0);

    const auto full = [maxNodes, &graph]() {
        if (graph.nodes.size() < maxNodes) return false;
        graph.truncated = true;
        return true;
    };

    // Walk outwards in both directions in one pass each, so a clip that is both an
    // ancestor and a descendant of the focus (a cycle) is reached once and the
    // second sighting is counted as hidden rather than drawn twice.
    for (const bool upward : {true, false}) {
        const int maxDepth = upward ? upDepth : downDepth;
        if (maxDepth <= 0) continue;
        std::deque<std::pair<std::string, int>> frontier;
        frontier.emplace_back(std::string(focusId), 0);
        std::unordered_set<std::string> seen{std::string(focusId)};
        while (!frontier.empty()) {
            const auto [id, depth] = frontier.front();
            frontier.pop_front();
            if (depth >= maxDepth) continue;
            const auto owner = indexOf.find(id);
            if (owner == indexOf.end()) continue;
            const auto& table = upward ? adjacency.parents : adjacency.children;
            const auto found = table.find(id);
            if (found == table.end()) continue;
            for (const Edge* edge : found->second) {
                const std::string& next = upward ? edge->parentId : edge->childId;
                if (next.empty()) continue;
                if (!seen.insert(next).second) continue;
                // A clip reachable in BOTH directions (a loop in the data) is one
                // node, not two: the first sighting owns the tree link and the
                // second survives as a cross-edge in `Graph::edges`.
                if (indexOf.contains(next)) continue;
                if (full()) {
                    ++graph.hiddenNeighbours;
                    continue;
                }
                const int nextDepth = upward ? -(depth + 1) : depth + 1;
                emplace(next, upward ? Role::Ancestor : Role::Descendant, nextDepth, owner->second,
                        edge->relation, edge->provenance, edge->confidence);
                frontier.emplace_back(next, depth + 1);
            }
        }
    }

    // ── Layered layout ──────────────────────────────────────────────────────
    const auto spanOf = [&graph](Role role) {
        int span = 0;
        for (const auto& node : graph.nodes) {
            if (node.role != role) continue;
            const int magnitude = node.depth < 0 ? -node.depth : node.depth;
            span = std::max(span, magnitude);
        }
        return span;
    };
    const int ancestorSpan = spanOf(Role::Ancestor);
    graph.columns = ancestorSpan + 1 + spanOf(Role::Descendant);
    for (auto& node : graph.nodes) {
        node.column = node.depth + ancestorSpan;
    }

    std::vector<std::vector<std::size_t>> byColumn(static_cast<std::size_t>(graph.columns));
    for (std::size_t i = 0; i < graph.nodes.size(); ++i) {
        const int column = std::clamp(graph.nodes[i].column, 0, graph.columns - 1);
        byColumn[static_cast<std::size_t>(column)].push_back(i);
    }
    graph.rows = 1;
    for (auto& column : byColumn) {
        // Oldest first inside a column: the visual reading of a generation is
        // chronological, and the tie-break on id keeps it stable.
        std::sort(column.begin(), column.end(), [&graph](std::size_t lhs, std::size_t rhs) {
            const Node& a = graph.nodes[lhs];
            const Node& b = graph.nodes[rhs];
            if (a.info.createdAt != b.info.createdAt) return a.info.createdAt < b.info.createdAt;
            return a.id < b.id;
        });
        graph.rows = std::max<int>(graph.rows, static_cast<int>(column.size()));
        for (std::size_t row = 0; row < column.size(); ++row) {
            graph.nodes[column[row]].row = static_cast<int>(row);
        }
    }

    // Emit nodes in render order and remap `linkedTo` onto the new indices.
    std::vector<std::size_t> order(graph.nodes.size());
    for (std::size_t i = 0; i < order.size(); ++i) {
        order[i] = i;
    }
    std::sort(order.begin(), order.end(), [&graph](std::size_t lhs, std::size_t rhs) {
        const Node& a = graph.nodes[lhs];
        const Node& b = graph.nodes[rhs];
        if (a.column != b.column) return a.column < b.column;
        return a.row < b.row;
    });
    std::vector<std::size_t> remap(graph.nodes.size());
    std::vector<Node> sorted;
    sorted.reserve(graph.nodes.size());
    for (std::size_t newIndex = 0; newIndex < order.size(); ++newIndex) {
        remap[order[newIndex]] = newIndex;
    }
    for (const std::size_t oldIndex : order) {
        sorted.push_back(std::move(graph.nodes[oldIndex]));
    }
    for (auto& node : sorted) {
        node.linkedTo = node.linkedTo < remap.size() ? remap[node.linkedTo] : 0;
    }
    graph.nodes = std::move(sorted);

    // Only edges whose both ends survived the caps are drawable.
    std::unordered_set<std::string> present;
    present.reserve(graph.nodes.size());
    for (const auto& node : graph.nodes) {
        present.insert(node.id);
    }
    for (const auto& edge : edges) {
        if (edge.confidence < floor) continue;
        if (!present.contains(edge.childId) || !present.contains(edge.parentId)) continue;
        graph.edges.push_back(edge);
    }
    std::sort(graph.edges.begin(), graph.edges.end(), [](const Edge& lhs, const Edge& rhs) {
        if (lhs.childId != rhs.childId) return lhs.childId < rhs.childId;
        return lhs.parentId < rhs.parentId;
    });
    return graph;
}

} // namespace vc::suno::lineage
