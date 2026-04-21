# Suno B-Side & Beta Feature Discovery

Analysis of experimental flags and hidden endpoints for "Chad" integration.

## Experimental Models & Versions
- **v4**: Improving sound quality, currently in rollout.
- **v4.5 (Chirp-Auk)**: Intelligent prompts, pro-only feature.
- **v4.5-all**: High-quality free model.
- **v5 / v5.5**: Advanced models listed in subscription comparisons.

## Feature Gates & Capabilities
- `create_control_sliders`: Direct manipulation of generation parameters (weirdness, style weight).
- `tag_upsample`: Intelligent tag expansion.
- `persona`: Voice/style consistency features.
- `infill`: Part-specific generation (intro, outro, mid-song).
- `studio`: Advanced multi-track editing workspace.
- `auk`: Native intelligent prompt engine.

## Orchestrator & Conversational Generation
- Endpoints under `/api/statsig/experiment/` and `/api/cms/nudges/` suggest a behavioral steering engine.
- Conversational generation (chat-based) likely utilizes a stateful session on `studio-api-prod.suno.com`.

## Integration Strategy for ChadVis
- **Client-Side Overrides**: Inject `statsig` overrides to force-enable pro/beta UI features.
- **Direct Parameter Access**: Expose `weirdness_constraint` and `style_weight` in the generation UI.
- **Model Forcing**: Allow selecting `chirp-auk` or `chirp-v4` directly via API headers.

---
*Generated: 2026-04-21*
