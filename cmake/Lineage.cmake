// ─────────────────────────────────────────────────────────────────────────────
// SONG LINEAGE — the inference engine, the graph and the durable edge store
// (appended 2026-11-24)
//
// Three things land here, and the non-globbing rule is why:
//
//  1. `src/suno/lineage/*` — the model. Fingerprints, a weighted scorer, a blocked
//     pairwise inference pass, a layered graph layout and a SQLite edge store. Pure
//     and Qt-free except the store, so `test_Lineage` proves every rule headlessly.
//  2. `src/qml_bridge/SunoLineageBridge.*` — a QML singleton, not a `SunoBridge`
//     section: it reads the library cache directly and owns a second SQLite
//     connection, neither of which belongs on the object that owns the transport.
//  3. The view itself, registered with the other QML files.
//
// SUNO_SOURCES specifically, for the same non-globbing reason every block above
// records: `cmake/TargetSetup.cmake` names each set() variable explicitly in
// `add_library(project_lib STATIC ...)` and does NOT glob, so a fresh
// `set(LINEAGE_SOURCES ...)` would be dropped silently and every symbol here would
// surface as a link error naming a CALLER.
// ─────────────────────────────────────────────────────────────────────────────
list(APPEND SUNO_SOURCES
    src/suno/lineage/LineageTypes.hpp
    src/suno/lineage/LineageText.hpp
    src/suno/lineage/LineageWeights.hpp
    src/suno/lineage/LineageWeights.cpp
    src/suno/lineage/LineageFingerprint.hpp
    src/suno/lineage/LineageFingerprint.cpp
    src/suno/lineage/LineageScorer.hpp
    src/suno/lineage/LineageScorer.cpp
    src/suno/lineage/LineageInference.hpp
    src/suno/lineage/LineageInference.cpp
    src/suno/lineage/LineageGraph.hpp
    src/suno/lineage/LineageGraph.cpp
    src/suno/lineage/SunoLineageStore.hpp
    src/suno/lineage/SunoLineageStore.cpp
)

list(APPEND QML_BRIDGE_SOURCES
    src/qml_bridge/SunoLineageBridge.hpp
    src/qml_bridge/SunoLineageBridge.cpp
)

list(APPEND QML_SOURCES
    src/qml/components/LineageNode.qml
    src/qml/views/LineageView.qml
)
