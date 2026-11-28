# Song lineage

The Lineage view answers one question the Suno web app cannot: **what came from what**,
across the whole library rather than one song page at a time.

Two kinds of link exist and they are never blended:

| Provenance | Source | Trust |
| :--- | :--- | :--- |
| `remote` | `GET /clips/{id}/attribution` → `source_clips[].relationship` | The server's statement. Never overwritten, never removed locally. |
| `inferred` | ChadVis comparing the clips' own text, model and age | A guess. Drawn dashed, removable, replaced wholesale on every rebuild. |

The other side of the same question is a **read**, not a link:
`GET /clips/clip_roots?clip_id=` answers `{clips[], clip_attribution_type}`, so
"this is an original" arrives as an empty list plus `"original"` rather than as
silence. It is served to the clip sheet by `SunoClipDetailService`, is **not**
stored as an edge, and must not be read as "no parents were found" when the
server said nothing at all.

## Where it lives

| Piece | File | Job |
| :--- | :--- | :--- |
| Vocabulary | `src/suno/lineage/LineageTypes.hpp` | `Relation`, `Provenance`, `Edge`. Wire codes (`COV`, `EX`, …) map here; an unknown code is `Unknown`, never the wrong verb. |
| Text primitives | `src/suno/lineage/LineageText.hpp` | Tokenizer, Jaccard, script hint, ISO-8601 → epoch. Header-only and Qt-free. |
| Fingerprint | `LineageFingerprint.*` | One clip reduced to title/style/description tokens, script, model generation, creation time. |
| Scorer | `LineageScorer.*` | Six weighted signals → `Score` with the components kept separately, so a link can explain itself. |
| Inference | `LineageInference.*` | Candidate blocking, then pairwise scoring, top-K parents per child. Deterministic. |
| Graph | `LineageGraph.*` | Focus-centred BFS, one parent per node for layout, other edges kept for drawing, layered grid. |
| Store | `SunoLineageStore.*` | `lineage_edges` + `lineage_meta` in the library cache file, on its own SQLite connection. |
| View | `src/qml/views/LineageView.qml`, `components/LineageNode.qml` | Pan/zoom constellation, tuning panel. |
| Bridge | `src/qml_bridge/SunoLineageBridge.*` | QML singleton: chooses clips, owns the store, marshals the graph. |

## The heuristic

Confidence is a weighted mean of six components, each in `[0, 1]`:

| Signal | Default weight | What it measures |
| :--- | ---: | :--- |
| `title` | 0.35 | Jaccard of title tokens, with containment ("X" vs "X (Cover)") lifted to 0.80 |
| `style` | 0.30 | Jaccard of `metadata.tags` — Suno's style/genre sentence |
| `prompt` | 0.15 | Jaccard of the free-form description |
| `language` | 0.05 | Same dominant writing system |
| `model` | 0.05 | Same generation (adjacent generations earn 0.4) |
| `age` | 0.10 | Creation proximity, capped at two years |

Two gates keep unrelated songs apart:

- **A strong signal is required.** `title >= titleFloor (0.45)` or `style >= styleFloor (0.60)`.
  A blended score alone links two songs that share one common word and a generic tag.
- **The instrumental veto.** An instrumental clip is not linked to a vocal one by default;
  a user who works in instrumental-cover lineage can turn it off.

Direction comes from creation time (`olderFirst`), and the relation word from the child's own
title/tags (`relationHint`): mashup, cover, remix, extend, stem — in that priority order,
whole-word only, so "covered in dust" is not a cover.

Confidence is a guess, not a probability. It ranks candidates; it does not claim 55% certainty.

### Cost

Naive comparison is O(n²): 22.8 million pairs on a 6752-clip library. Clips are therefore
blocked first — two clips are compared only when they already share **two** index tokens
(one title + one style, or two title words). Buckets shared by more than 160 clips are
skipped as too generic, and the run stops at 250 000 scored pairs. Both truncations are
reported in the status line and in `InferenceStats`; neither is silently presented as a
complete answer.

The pass runs synchronously on the GUI thread, so it is bounded on purpose. Moving it to a
worker is backlog `T0467`.

## Weights

The six weights, the threshold, the gates and the graph depths live in `lineage::Weights`
and are edited from the view's tuning panel. They persist in `lineage_meta` inside the
library cache, not in `config.toml` — see backlog `T0466` for the config-file half.

## Invariants

1. A remote edge is never overwritten by an inferred one. Enforced by the `WHERE` clause on
   the upsert in `SunoLineageStore`, not by callers.
2. `replaceInferred` deletes only `provenance='inferred'` rows, in one transaction.
3. A rebuild reads the whole library before it writes any edge, so the write transaction never
   holds a lock across a read of the same file.
4. Graph traversal is cycle-safe: a clip reachable in both directions appears once.
5. Nothing here fetches media, loads an image, or calls a host outside the allowlist. The only
   network requests are the attribution read and its `clips/clip_roots` sibling, both through
   `SunoClient`.
