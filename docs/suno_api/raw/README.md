# Raw Scan Data

## `endpoints_sniffed.list`

Formerly `docs/endpoints_sniffed_sort-uniq-tee.list.md` (~706KB; renamed because
it is a raw data dump, not prose).

**Provenance:** Output of an endpoint-discovery pass against Suno's web
properties: candidate URLs harvested from JS bundles, saved HTML, and network
sniffing, then `sort | uniq | tee`'d into this list. One entry per line. No
guarantee any given entry is live, correct, or safe to call.

Curated, human-verified endpoint documentation lives in
[`../ENDPOINT-INVENTORY.md`](../ENDPOINT-INVENTORY.md) and the topic files in
[`..`](../README.md).
