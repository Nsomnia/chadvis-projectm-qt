# ChadVis Documentation

Source-verified documentation hub for ChadVis, a Suno-first desktop client with a native projectM video workspace.

## Using ChadVis

- [Installation](user/INSTALL.md) — build and installation notes.
- [Configuration](user/CONFIG.md) — configuration-file reference.
- [Usage](user/USAGE.md) — current Library, Create, Listen, Video, Settings, and account flow.
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

The material in this section is unofficial and capture-based; Suno does not publish a public API contract for this client.

- [API index and disclaimer](suno_api/README.md) — scope and research boundaries.
- [Endpoint inventory](suno_api/ENDPOINT-INVENTORY.md) — sole API-spec master for observed endpoints and evidence status.
- [Desktop callback analysis](suno_api/OAUTH_REDIRECT_ANALYSIS.md) — captured web flow and the native sign-in gate.
- [Endpoint implementation map](../src/suno/SunoEndpoints.hpp) — centralized constants and `[T1]`/`[LEAD]` tiers.
- [Retained raw provenance](suno_api/raw/README.md), including the [endpoint scan](suno_api/raw/endpoints_sniffed.list) and [sanitized OAuth recon](suno_api/raw/sanitized-recon-2026-09-22.json).

## Integration Notes

- [Integration hub](integration/INDEX.md).
- [Suno integration](integration/SUNO.md).
- [projectM integration](integration/PROJECTM.md).

## Project Updates and Lore

- [Current changelog](../CHANGELOG_CURRENT.md).
- [Project history](lore/HISTORY.md).
- [Manifesto](lore/MANIFESTO.md).
