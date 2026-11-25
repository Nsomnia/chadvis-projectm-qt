// test_Lineage.cpp - the song-lineage model: text primitives, scoring, blocking,
// the graph layout and the durable store. Every case is headless: no network, no
// GL, no library sync. The store cases use an in-memory database.

#include <QtTest>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "suno/SunoModels.hpp"
#include "suno/lineage/LineageFingerprint.hpp"
#include "suno/lineage/LineageGraph.hpp"
#include "suno/lineage/LineageInference.hpp"
#include "suno/lineage/LineageScorer.hpp"
#include "suno/lineage/LineageText.hpp"
#include "suno/lineage/LineageTypes.hpp"
#include "suno/lineage/LineageWeights.hpp"
#include "suno/lineage/SunoLineageStore.hpp"

namespace {

namespace ln = vc::suno::lineage;

vc::suno::SunoClip makeClip(const std::string& id, const std::string& title, const std::string& tags,
                            const std::string& createdAt, const std::string& prompt = {},
                            const std::string& model = "chirp-v3-5") {
    vc::suno::SunoClip clip;
    clip.id = id;
    clip.title = title;
    clip.created_at = createdAt;
    clip.model_name = model;
    clip.major_model_version = model;
    clip.metadata.tags = tags;
    clip.metadata.prompt = prompt;
    clip.metadata.type = "gen";
    clip.metadata.duration = "02:30";
    return clip;
}

ln::Edge makeEdge(const std::string& child, const std::string& parent, ln::Provenance provenance,
                  double confidence) {
    ln::Edge edge;
    edge.childId = child;
    edge.parentId = parent;
    edge.provenance = provenance;
    edge.confidence = confidence;
    edge.relation = ln::Relation::Unknown;
    return edge;
}

ln::InfoProvider titles(std::vector<std::pair<std::string, std::string>> table) {
    return [table](std::string_view id) {
        ln::NodeInfo info;
        for (const auto& entry : table) {
            if (entry.first == id) {
                info.title = entry.second;
                // One timestamp for all of them: the layout's tie-break on id is
                // then the only ordering rule, which is what the assertions expect.
                info.createdAt = "2026-01-01T00:00:00Z";
                break;
            }
        }
        return info;
    };
}

const ln::Node* nodeFor(const ln::Graph& graph, std::string_view id) {
    for (const auto& node : graph.nodes) {
        if (node.id == id) return &node;
    }
    return nullptr;
}

} // namespace

class TestLineage : public QObject {
    Q_OBJECT

private slots:
    // ── text primitives ────────────────────────────────────────────────────
    void tokenizeIsLowercaseDedupedAndStopWordFree() {
        const auto tokens = ln::text::tokenize("Midnight Drive, midnight DRIVE of the Night");
        QCOMPARE(tokens.size(), std::size_t{3});
        QCOMPARE(tokens[0], std::string("midnight"));
        QCOMPARE(tokens[1], std::string("drive"));
        QCOMPARE(tokens[2], std::string("night"));
    }

    void tokenizeRespectsTheCap() {
        std::string text;
        for (int i = 0; i < 100; ++i) {
            text += "word" + std::to_string(i) + " ";
        }
        QVERIFY(ln::text::tokenize(text, 10).size() <= 10);
    }

    void normalizeStripsPunctuation() {
        QCOMPARE(ln::text::normalize("  Song (Cover)!  "), std::string("song cover"));
    }

    void jaccardIsZeroForEmptyEitherSide() {
        const std::vector<std::string> a{"one", "two"};
        const std::vector<std::string> empty;
        QCOMPARE(ln::text::jaccard(a, empty), 0.0);
        QCOMPARE(ln::text::jaccard(empty, empty), 0.0);
        QCOMPARE(ln::text::jaccard(a, a), 1.0);
    }

    void languageHintSeparatesScripts() {
        QCOMPARE(ln::text::languageHint("dreamy synthwave with female vocals"),
                 std::string("latin"));
        QCOMPARE(ln::text::languageHint("夜のドライブ、シンセウェーブ"), std::string("cjk"));
        QCOMPARE(ln::text::languageHint(""), std::string("none"));
    }

    void isoEpochSecondsParsesTheWireShape() {
        // 2026-09-03T10:00:00Z
        QCOMPARE(ln::text::isoEpochSeconds("2026-09-03T10:00:00.123456Z"),
                 ln::i64{1788429600});
        QCOMPARE(ln::text::isoEpochSeconds(""), ln::i64{0});
        QCOMPARE(ln::text::isoEpochSeconds("not a date"), ln::i64{0});
        // An impossible month is refused rather than wrapped.
        QCOMPARE(ln::text::isoEpochSeconds("2026-13-03T10:00:00Z"), ln::i64{0});
    }

    void durationsAreSortedOut() {
        // Guard against the two spellings quietly becoming one: the digit count on
        // the left of a colon decides the minute field.
        QCOMPARE(ln::parseDurationSeconds("00:59"), ln::i64{59});
        QCOMPARE(ln::parseDurationSeconds("1:00"), ln::i64{60});
    }

    // ── fingerprints ───────────────────────────────────────────────────────
    void modelMajorIsTheFirstDigitRun() {
        QCOMPARE(ln::parseModelMajor("chirp-v3-5"), 3);
        QCOMPARE(ln::parseModelMajor("chirp-v4"), 4);
        QCOMPARE(ln::parseModelMajor("v4.5"), 4);
        QCOMPARE(ln::parseModelMajor(""), 0);
        QCOMPARE(ln::parseModelMajor("chirp"), 0);
    }

    void durationAcceptsBothWireSpellings() {
        QCOMPARE(ln::parseDurationSeconds("02:31"), ln::i64{151});
        QCOMPARE(ln::parseDurationSeconds("151.4"), ln::i64{151});
        QCOMPARE(ln::parseDurationSeconds(""), ln::i64{0});
        QCOMPARE(ln::parseDurationSeconds("02:99"), ln::i64{0});
    }

    void fingerprintRequiresAnId() {
        vc::suno::SunoClip anonymous;
        QVERIFY(!ln::fingerprint(anonymous).valid);

        const auto print = ln::fingerprint(makeClip("c1", "Midnight Drive", "synthwave, dreamy",
                                                    "2026-09-01T00:00:00Z", "night drive"));
        QVERIFY(print.valid);
        QCOMPARE(print.modelMajor, 3);
        QCOMPARE(print.durationSeconds, ln::i64{150});
        QCOMPARE(print.language, std::string("latin"));
        QVERIFY(!print.titleTokens.empty());
    }

    // ── scoring ────────────────────────────────────────────────────────────
    void identicalDescribeSimilar() {
        const ln::Weights weights;
        const auto a = ln::fingerprint(makeClip("a", "Midnight Drive", "synthwave, dreamy",
                                                 "2026-09-01T00:00:00Z", "night drive synthwave"));
        const auto b = ln::fingerprint(makeClip("b", "Midnight Drive", "synthwave, dreamy",
                                                 "2026-10-01T00:00:00Z", "night drive synthwave"));
        const auto scored = ln::score(a, b, weights);
        QVERIFY(scored.title >= 0.99);
        QVERIFY(scored.style >= 0.99);
        QVERIFY(scored.eligible);
        QVERIFY(!ln::evidenceText(scored).empty());
    }

    void aCoverNameIsRecognisedAsACover() {
        const ln::Weights weights;
        const auto parent = ln::fingerprint(
                makeClip("p", "Midnight Drive", "synthwave", "2026-09-01T00:00:00Z"));
        const auto child = ln::fingerprint(makeClip("c", "Midnight Drive (Cover)", "synthwave",
                                                     "2026-09-02T00:00:00Z"));
        const auto scored = ln::score(parent, child, weights);
        QCOMPARE(scored.relation, ln::Relation::Cover);
        // Containment keeps the title component high even though the token sets differ.
        QVERIFY(scored.title >= 0.79);
    }

    void unrelatedStylesAreNotEligible() {
        const ln::Weights weights;
        const auto a = ln::fingerprint(
                makeClip("a", "Glass Harbour", "ambient, piano", "2026-09-01T00:00:00Z"));
        const auto b = ln::fingerprint(
                makeClip("b", "Iron Verdict", "metal, drums", "2026-09-02T00:00:00Z"));
        QVERIFY(!ln::score(a, b, weights).eligible);
    }

    void instrumentalVetoCanBeTurnedOff() {
        ln::Weights weights;
        const auto vocal = ln::fingerprint(
                makeClip("v", "Midnight Drive", "synthwave", "2026-09-01T00:00:00Z"));
        auto instrumentalClip =
                makeClip("i", "Midnight Drive", "synthwave", "2026-09-02T00:00:00Z");
        instrumentalClip.metadata.make_instrumental = true;
        const auto instrumental = ln::fingerprint(instrumentalClip);

        QVERIFY(!ln::score(vocal, instrumental, weights).eligible);
        weights.instrumentalVeto = false;
        QVERIFY(ln::score(vocal, instrumental, weights).eligible);
    }

    void olderFirstUsesCreationThenId() {
        const auto older = ln::fingerprint(makeClip("a", "T", "s", "2026-01-01T00:00:00Z"));
        const auto newer = ln::fingerprint(makeClip("b", "T", "s", "2026-02-01T00:00:00Z"));
        const ln::Fingerprint* parent = nullptr;
        const ln::Fingerprint* child = nullptr;
        ln::olderFirst(newer, older, &parent, &child);
        QCOMPARE(parent->id, std::string("a"));
        QCOMPARE(child->id, std::string("b"));

        // Equal timestamps fall back to the id, so the direction never depends on
        // argument order.
        const auto same1 = ln::fingerprint(makeClip("x", "T", "s", "2026-01-01T00:00:00Z"));
        const auto same2 = ln::fingerprint(makeClip("y", "T", "s", "2026-01-01T00:00:00Z"));
        ln::olderFirst(same2, same1, &parent, &child);
        QCOMPARE(parent->id, std::string("x"));
        QCOMPARE(child->id, std::string("y"));
    }

    // ── weights ────────────────────────────────────────────────────────────
    void defaultWeightsValidate() {
        QVERIFY(ln::validate(ln::Weights{}).has_value());
    }

    void negativeOrEmptyWeightsAreRefused() {
        ln::Weights negative;
        negative.title = -1.0;
        QVERIFY(!ln::validate(negative).has_value());

        ln::Weights zero;
        zero.title = 0.0;
        zero.style = 0.0;
        zero.prompt = 0.0;
        zero.language = 0.0;
        zero.model = 0.0;
        zero.age = 0.0;
        QVERIFY(!ln::validate(zero).has_value());

        ln::Weights lowThreshold;
        lowThreshold.threshold = 2.0;
        QVERIFY(!ln::validate(lowThreshold).has_value());
    }

    void sanitizeClampsAndReportsWhatItChanged() {
        ln::Weights broken;
        broken.threshold = 9.0;
        broken.edgesPerClip = 0;
        broken.ancestorDepth = 99;
        const QStringList repaired = ln::sanitize(broken);
        QVERIFY(!repaired.isEmpty());
        QVERIFY(broken.threshold <= 1.0);
        QCOMPARE(broken.edgesPerClip, 1);
        QCOMPARE(broken.ancestorDepth, 12);

        // Every signal off is a broken file, not a preference: the defaults return.
        ln::Weights zero;
        zero.title = zero.style = zero.prompt = zero.language = zero.model = zero.age = 0.0;
        ln::sanitize(zero);
        QVERIFY(zero.title > 0.0);
    }

    void weightsRoundTripThroughAVariantMap() {
        ln::Weights weights;
        weights.title = 0.5;
        weights.threshold = 0.7;
        weights.edgesPerClip = 3;
        weights.instrumentalVeto = false;
        const auto parsed = ln::fromVariantMap(ln::toVariantMap(weights));
        QCOMPARE(parsed.title, 0.5);
        QCOMPARE(parsed.threshold, 0.7);
        QCOMPARE(parsed.edgesPerClip, 3);
        QVERIFY(!parsed.instrumentalVeto);
    }

    // ── inference ──────────────────────────────────────────────────────────
    void relatedClipsBecomeAnEdgeFromOldToNew() {
        const std::vector<vc::suno::SunoClip> clips = {
                makeClip("new", "Midnight Drive", "synthwave, dreamy",
                         "2026-10-01T00:00:00Z", "night drive synthwave"),
                makeClip("old", "Midnight Drive", "synthwave, dreamy",
                         "2026-09-01T00:00:00Z", "night drive synthwave"),
        };
        std::vector<ln::Fingerprint> prints;
        for (const auto& clip : clips) {
            prints.push_back(ln::fingerprint(clip));
        }
        const auto result = ln::inferEdges(prints, ln::Weights{});
        QCOMPARE(result.edges.size(), std::size_t{1});
        QCOMPARE(result.edges[0].childId, std::string("new"));
        QCOMPARE(result.edges[0].parentId, std::string("old"));
        QCOMPARE(result.edges[0].provenance, ln::Provenance::Inferred);
        QVERIFY(result.edges[0].confidence >= ln::Weights{}.threshold);
        QVERIFY(!result.edges[0].evidence.empty());
    }

    void clipsWithNothingInCommonProduceNoEdge() {
        const std::vector<vc::suno::SunoClip> clips = {
                makeClip("a", "Glass Harbour", "ambient, piano", "2026-09-01T00:00:00Z"),
                makeClip("b", "Iron Verdict", "metal, drums", "2026-09-02T00:00:00Z"),
        };
        std::vector<ln::Fingerprint> prints;
        for (const auto& clip : clips) {
            prints.push_back(ln::fingerprint(clip));
        }
        QCOMPARE(ln::inferEdges(prints, ln::Weights{}).edges.size(), std::size_t{0});
    }

    void inferenceIsDeterministic() {
        std::vector<ln::Fingerprint> prints;
        for (int i = 0; i < 5; ++i) {
            prints.push_back(ln::fingerprint(
                    makeClip("c" + std::to_string(i), "Same Song", "pop, bright",
                             "2026-09-0" + std::to_string(i + 1) + "T00:00:00Z")));
        }
        const auto first = ln::inferEdges(prints, ln::Weights{});
        const auto second = ln::inferEdges(prints, ln::Weights{});
        QCOMPARE(first.edges.size(), second.edges.size());
        for (std::size_t i = 0; i < first.edges.size(); ++i) {
            QCOMPARE(first.edges[i].childId, second.edges[i].childId);
            QCOMPARE(first.edges[i].parentId, second.edges[i].parentId);
            QCOMPARE(first.edges[i].confidence, second.edges[i].confidence);
        }
    }

    void aPairCapIsReportedNotHidden() {
        std::vector<ln::Fingerprint> prints;
        for (int i = 0; i < 4; ++i) {
            prints.push_back(ln::fingerprint(
                    makeClip("c" + std::to_string(i), "Same Song", "pop, bright",
                             "2026-09-0" + std::to_string(i + 1) + "T00:00:00Z")));
        }
        ln::InferenceLimits limits;
        limits.pairCap = 0;
        const auto result = ln::inferEdges(prints, ln::Weights{}, limits);
        QVERIFY(result.stats.pairCapHit);
        QVERIFY(result.edges.empty());
    }

    // ── graph ──────────────────────────────────────────────────────────────
    void graphSeparatesAncestorsFromDescendants() {
        const std::vector<ln::Edge> edges = {
                makeEdge("b", "a", ln::Provenance::Remote, 1.0),
                makeEdge("c", "b", ln::Provenance::Inferred, 0.9),
        };
        ln::Options options;
        const auto graph = ln::buildGraph(edges, "b", options,
                                          titles({{"a", "Alpha"}, {"b", "Beta"}, {"c", "Gamma"}}));
        QCOMPARE(graph.nodes.size(), std::size_t{3});
        const ln::Node* a = nodeFor(graph, "a");
        const ln::Node* b = nodeFor(graph, "b");
        const ln::Node* c = nodeFor(graph, "c");
        QVERIFY(a != nullptr && b != nullptr && c != nullptr);
        QCOMPARE(a->role, ln::Role::Ancestor);
        QCOMPARE(a->depth, -1);
        QCOMPARE(b->role, ln::Role::Focus);
        QCOMPARE(b->depth, 0);
        QCOMPARE(c->role, ln::Role::Descendant);
        QCOMPARE(c->depth, 1);
        QCOMPARE(graph.columns, 3);
        QCOMPARE(a->column, 0);
        QCOMPARE(b->column, 1);
        QCOMPARE(c->column, 2);
        QCOMPARE(graph.edges.size(), std::size_t{2});
        // The tree links point at the node each one was reached from.
        QCOMPARE(graph.nodes[c->linkedTo].id, std::string("b"));
    }

    void aCycleIsDrawnOnce() {
        const std::vector<ln::Edge> edges = {
                makeEdge("a", "b", ln::Provenance::Inferred, 0.9),
                makeEdge("b", "a", ln::Provenance::Inferred, 0.9),
        };
        ln::Options options;
        const auto graph = ln::buildGraph(edges, "a", options, titles({{"a", "A"}, {"b", "B"}}));
        // Focus plus exactly one sighting of the other clip; no infinite walk.
        QCOMPARE(graph.nodes.size(), std::size_t{2});
    }

    void confidenceFloorFiltersEdges() {
        const std::vector<ln::Edge> edges = {
                makeEdge("b", "a", ln::Provenance::Inferred, 0.4),
                makeEdge("c", "b", ln::Provenance::Inferred, 0.9),
        };
        ln::Options options;
        options.minConfidence = 0.5;
        const auto graph = ln::buildGraph(edges, "b", options,
                                          titles({{"a", "A"}, {"b", "B"}, {"c", "C"}}));
        QCOMPARE(graph.nodes.size(), std::size_t{2});
        QVERIFY(nodeFor(graph, "a") == nullptr);
        QVERIFY(nodeFor(graph, "c") != nullptr);
    }

    void theNodeCapIsReported() {
        const std::vector<ln::Edge> edges = {
                makeEdge("b", "a", ln::Provenance::Inferred, 0.9),
                makeEdge("c", "b", ln::Provenance::Inferred, 0.9),
        };
        ln::Options options;
        options.maxNodes = 2;
        const auto graph = ln::buildGraph(edges, "b", options,
                                          titles({{"a", "A"}, {"b", "B"}, {"c", "C"}}));
        QCOMPARE(graph.nodes.size(), std::size_t{2});
        QVERIFY(graph.truncated);
        QVERIFY(graph.hiddenNeighbours >= 1);
    }

    void anEmptyFocusYieldsAnEmptyGraph() {
        QCOMPARE(ln::buildGraph({}, "", ln::Options{}, titles({})).nodes.size(), std::size_t{0});
        // An unknown id still yields the focus node, so the view can say "nothing
        // is linked to this" rather than showing a blank canvas with no title.
        const auto graph = ln::buildGraph({}, "missing", ln::Options{}, titles({}));
        QCOMPARE(graph.nodes.size(), std::size_t{1});
        QCOMPARE(graph.nodes[0].role, ln::Role::Focus);
    }

    // ── store ──────────────────────────────────────────────────────────────
    void storeRoundTripsAndProtectsRemoteEdges() {
        ln::SunoLineageStore store;
        QVERIFY(store.init(":memory:").isOk());
        QCOMPARE(store.inferredCount().value(), std::size_t{0});

        QVERIFY(store.replaceInferred({makeEdge("b", "a", ln::Provenance::Inferred, 0.6)}).isOk());
        QCOMPARE(store.inferredCount().value(), std::size_t{1});

        // A remote edge lands, then an inferred pass tries to overwrite it with a
        // weaker claim. The server's answer must survive.
        QVERIFY(store.upsert({makeEdge("b", "a", ln::Provenance::Remote, 1.0)}).isOk());
        QVERIFY(store.upsert({makeEdge("b", "a", ln::Provenance::Inferred, 0.2)}).isOk());
        const auto edges = store.all().value();
        QCOMPARE(edges.size(), std::size_t{1});
        QCOMPARE(edges[0].provenance, ln::Provenance::Remote);
        QCOMPARE(edges[0].confidence, 1.0);

        // And the wholesale replacement cannot delete it either.
        QVERIFY(store.replaceInferred({}).isOk());
        QCOMPARE(store.all().value().size(), std::size_t{1});
        QCOMPARE(store.removeInferred().value(), 0);
    }

    void storeNeighbourhoodFindsBothDirections() {
        ln::SunoLineageStore store;
        QVERIFY(store.init(":memory:").isOk());
        QVERIFY(store.replaceInferred({makeEdge("b", "a", ln::Provenance::Inferred, 0.6),
                                       makeEdge("c", "b", ln::Provenance::Inferred, 0.8)})
                        .isOk());
        const auto around = store.neighbourhood("b").value();
        QCOMPARE(around.size(), std::size_t{2});
        QCOMPARE(store.neighbourhood("").value().size(), std::size_t{0});
        QCOMPARE(store.neighbourhood("nobody").value().size(), std::size_t{0});
    }

    void storeMetaRoundTrips() {
        ln::SunoLineageStore store;
        QVERIFY(store.init(":memory:").isOk());
        QCOMPARE(store.meta("weights", "fallback").value(), std::string("fallback"));
        QVERIFY(store.setMeta("weights", "{}").isOk());
        QCOMPARE(store.meta("weights", "fallback").value(), std::string("{}"));
        QVERIFY(store.setMeta("weights", "{\"title\":1}").isOk());
        QCOMPARE(store.meta("weights", "fallback").value(), std::string("{\"title\":1}"));
    }

    void storeRefusesSelfEdgesAndEmptyIds() {
        ln::SunoLineageStore store;
        QVERIFY(store.init(":memory:").isOk());
        QVERIFY(store.replaceInferred({makeEdge("a", "a", ln::Provenance::Inferred, 1.0),
                                       makeEdge("", "a", ln::Provenance::Inferred, 1.0),
                                       makeEdge("b", "a", ln::Provenance::Inferred, 1.0)})
                        .isOk());
        const auto edges = store.all().value();
        QCOMPARE(edges.size(), std::size_t{1});
        QCOMPARE(edges[0].childId, std::string("b"));
    }

    void relationKeysAndWireCodesRoundTrip() {
        QCOMPARE(ln::relationFromKey(ln::relationKey(ln::Relation::Cover)), ln::Relation::Cover);
        QCOMPARE(ln::relationFromWire("COV"), ln::Relation::Cover);
        QCOMPARE(ln::relationFromWire("EX"), ln::Relation::Extend);
        // An unseen code is Unknown, never the wrong verb.
        QCOMPARE(ln::relationFromWire("WHATEVER"), ln::Relation::Unknown);
        QCOMPARE(ln::provenanceFromKey("remote"), ln::Provenance::Remote);
        // A mislabelled row is inferred, so a rebuild can still replace it.
        QCOMPARE(ln::provenanceFromKey("server"), ln::Provenance::Inferred);
    }
};

#include "test_Lineage.moc"

int runTestLineage(int argc, char** argv) {
    TestLineage tc;
    return QTest::qExec(&tc, argc, argv);
}
