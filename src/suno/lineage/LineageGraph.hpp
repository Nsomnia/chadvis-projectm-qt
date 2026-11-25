#pragma once
// LineageGraph.hpp - a focus clip's neighbourhood, laid out for rendering.
//
// The store holds a graph; the view needs a TREE, because a person reading it
// wants "what did this come from, and what came from it". So traversal here picks
// ONE parent per node (the highest-confidence one) and keeps every other edge in
// the edge list for drawing, which is how a DAG is shown as a tree with cross-links
// rather than as a hairball.
//
// Layout is a layered grid: one column per generation, one row per node within it.
// Deterministic, so the same library produces the same picture — a graph that
// rearranges itself on every open is a graph nobody can learn.

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "LineageTypes.hpp"
#include "util/Types.hpp"

namespace vc::suno::lineage {

struct NodeInfo {
    std::string title;
    std::string tags;
    std::string createdAt;
    std::string modelName;
    i64 playCount{0};
    bool instrumental{false};
};

/// Everything the graph builder cannot know from the edges alone.
using InfoProvider = std::function<NodeInfo(std::string_view clipId)>;

enum class Role { Focus, Ancestor, Descendant };

struct Node {
    std::string id;
    NodeInfo info;
    Role role{Role::Focus};
    /// Relative generation: negative = ancestor, 0 = focus, positive = descendant.
    int depth{0};
    /// 0-based render column and row. `column` is `depth` shifted so the leftmost
    /// ancestor is column 0.
    int column{0};
    int row{0};
    /// Index (into the result's node list) of the node this one was reached from:
    /// the child for an ancestor, the parent for a descendant, itself for the focus.
    /// This is the tree edge; the full relation list is `Graph::edges`.
    std::size_t linkedTo{0};
    Relation relation{Relation::Unknown};
    Provenance provenance{Provenance::Inferred};
    double confidence{0.0};
};

struct Options {
    int ancestorDepth{4};
    int descendantDepth{4};
    /// Edges below this confidence are not traversed. 0 keeps everything stored.
    double minConfidence{0.0};
    /// Ceiling on rendered nodes. Exceeding it sets `truncated`.
    std::size_t maxNodes{240};
};

struct Graph {
    std::vector<Node> nodes;
    std::vector<Edge> edges;
    int columns{0};
    int rows{0};
    bool truncated{false};
    /// Clip ids that were one step beyond the depth/cap boundary, so the UI can
    /// say "more exists" instead of "that is all there is".
    std::size_t hiddenNeighbours{0};
};

Graph buildGraph(const std::vector<Edge>& edges, std::string_view focusId, const Options& options,
                 const InfoProvider& info);

} // namespace vc::suno::lineage
