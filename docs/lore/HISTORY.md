# 📜 The History of ChadVis

Dates and milestones below are taken from `git log` and `CHANGELOG.md`. No lore was invented; where the two disagree, the commit log wins.

## 2025-12: The First Render

The repository opens on 2025-12-25 with a scaffold and the commit message that started the entire vibe. Rendering is the hard part: the first milestone lands on 2025-12-27, followed by a fortnight of audio-rate, framebuffer, and preset-selection debugging.

## 2026-01 to 2026-02: The Visualizer Shipped

- `1.0.0` (2026-01-27) and `1.1.0` (2026-01-28) — first releases.
- 2026-02-02 — build system, persistent Suno cookies, and sidebar navigation land in one sitting. The changelog's summary of the last item: "Tabs are so 2010. Icons are the future."

## 2026-04: The Audit

- `2.0.0` (2026-04-19) and `2.1.0` (2026-04-20).
- 2026-04-28 — a full codebase audit: 19,294 LOC across 10 modules, 24 issues, 18 fixed across five phases. Lyrics parsing unified, the endpoint map centralized, CLI arguments moved to a table-driven X-macro system, `SettingsBridge` collapsed from 453 to roughly 120 lines, `SettingsPanel` split into per-category sub-panels, and dead code archived to `.backup_graveyard/`. Net result: 38 files changed, 894 net lines removed.

## 2026-08-26: The Pivot

The product changed identity. [`docs/PIVOT_PLAN.md`](../PIVOT_PLAN.md) records the decision: **Suno.com frontend first, projectM second**. The primary product is the Suno client shell — library, generation, downloads, playlists, account — and projectM becomes the secondary visualizer, karaoke, and music-video engine. The same day, the monolithic `CMakeLists.txt` is split into `cmake/` modules with a real toolchain floor (AppleClang 15 / GCC 13 / MSVC 19.29), and Suno auth moves into a dedicated subsystem with keychain-backed `CredentialStore`.

## 2026-09-24: The Capture Audit

432 Burp items are reviewed. Their timestamps are 2026-09-24 even though the external source directory is labeled `sept-09-2026`. The raw XML is not sanitized, so it stays outside the repository and is only hash-indexed for provenance. Only directly observed behaviour is promoted to contract; everything else is labeled `[LEAD]` or `[VERIFY]`. The live API authority is reduced to four files, and the Suno client shell merges into `main` the same day.

## 2026-09-26: 1.1.0

`v1.1.0` is the first release whose source, executable, and release metadata come from the same `version.txt` value. It is also the release where the project's documentation stops being aspirational and starts being source-verified.

---

> "We're not done. We're just done pretending we know things we don't." — *The Senior Dev*
