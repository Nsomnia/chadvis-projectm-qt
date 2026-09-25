# ChadVis Documentation

Source-verified documentation hub for ChadVis, a Suno-first desktop client with a native projectM video workspace.

## Using ChadVis

- [Installation](user/INSTALL.md) — build and installation notes.
- [Configuration](user/CONFIG.md) — configuration-file reference.
- [Usage](user/USAGE.md) — current Library, Explore, Create, Listen, Video, Notifications, Settings, and account flow.
- [Testing](dev/TESTING.md) — automated test inventory and current-shell smoke checklist.

## Current Client Surfaces

- [Main shell](../src/qml/main.qml) — persistent navigation and primary window.
- [Settings window](../src/qml/SettingsWindow.qml) — standalone paged settings surface.
- [Video page](../src/qml/views/VideoView.qml) — projectM, overlays, karaoke, presets, and recording UI.
- [Account page](../src/qml/settings/AccountPage.qml) — Suno session and manual credential path.

## Development

- [Architecture](dev/ARCHITECTURE.md) — process ownership, QML shell, native visualizer embedding, bridges, and Suno data flow.
- [Contributing](dev/CONTRIBUTING.md) — repository contribution notes.
- [Testing](dev/TESTING.md) — unit sources, integration harness, and manual GUI checks.

## Suno API Research

The material in this section is unofficial and capture-based; Suno does not publish a supported API contract for this client. The live authority is intentionally limited to an index, the canonical inventory, the OAuth gate, and raw provenance.

- [API index and disclaimer](suno_api/README.md) — authority boundary and research rules.
- [Endpoint inventory](suno_api/ENDPOINT-INVENTORY.md) — sole API-spec master, including the dated 2026-09-24 evidence-source map and `[T1]`/`[LEAD]`/`[VERIFY]` status.
- [OAuth redirect gate](suno_api/OAUTH_REDIRECT_ANALYSIS.md) — observed web flow and the disabled native sign-in gate.
- [Endpoint implementation map](../src/suno/SunoEndpoints.hpp) — implementation mirror, not evidence authority.
- [Raw evidence provenance and hashes](suno_api/raw/README.md), including the [endpoint scan](suno_api/raw/endpoints_sniffed.list) and [sanitized OAuth recon](suno_api/raw/sanitized-recon-2026-09-22.json).

The external directory labeled `sept-09-2026` contains a Burp export whose item timestamps are 2026-09-24. Its raw XML is not sanitized and is intentionally not retained in the repository.

## Integration Notes

- [Integration hub](integration/INDEX.md).
- [Suno integration](integration/SUNO.md).
- [projectM integration](integration/PROJECTM.md).

## Project Updates and Lore

- [Current changelog](../CHANGELOG_CURRENT.md).
- [Project history](lore/HISTORY.md).
- [Manifesto](lore/MANIFESTO.md).
