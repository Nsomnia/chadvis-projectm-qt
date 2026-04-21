# ChadVis ProjectM-QT Refactor: AGENTS.md

## High Priority: Core Infrastructure (Milestones)
- [x] Refactor Main.qml with responsive Drawer and SplitView layout
- [x] Refactor AudioEngine for better organization and granular responsibility
- [x] Implement throttled bridge updates in VisualizerBridge/AudioBridge
- [ ] Complete full migration of Sidebar panels (Library, Presets, Recording)
- [ ] Finalize robust persistence for all settings (SettingsPanel + JSON)

## High Priority: Suno "Chad" Integration
- [x] Upgrade Suno API to feed/v3 for library access
- [ ] **[NEW]** Implement "B-Side" feature set: Access hidden/beta endpoints (orchestrator, experiment gates)
- [ ] **[NEW]** Implement Generation Surface: Full creation suite (prompt, style, seeds) with client-side overrides
- [ ] **[NEW]** B-Side Chat/Orchestrator: Integration of experimental Suno conversational generation
- [ ] Implement infinite scrolling for Suno Library (Pagination)
- [ ] Refine Suno Library search and filtering (local + remote)

## Medium Priority: UI/UX & Polish
- [ ] Implement smooth height animations for AccordionPanel transitions
- [ ] Expand Settings.qml with comprehensive engine/recorder controls
- [ ] Implement "Modern Visualizer Overlay" with reactive text/graphics
- [ ] Add "Karaoke Master" mode: Synced lyrics with custom aesthetic overrides

## Strategic Goals: Maximum Customizability
- [ ] TOML-based "Chad Config": Every UI constant and engine parameter exposed
- [ ] Profile Support: Save/Load different UI themes and visualizer preset banks
- [ ] Persistent state for all sidebar toggles and view modes

---
*Date: 2026-04-21*
