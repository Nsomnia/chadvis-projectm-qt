# User Message - 2025-12-31

Computer battery ran out. Continue.

## Reported Issues

1. **Preset Shuffling Issue**: projectM visualizer shader presets are not shuffling correctly, whether in shuffle mode or not. The user has to use prev/next actions manually regardless of shuffle mode being enabled in settings.
2. **GUI/Resizing Rendering Issue**: When the window is resized smaller than the draw resolution for projectM canvas draw area, the Qt GUI shows up behind the visualizer, but only for certain parts of it. It looks like an alpha channel issue for a specific color or color range.

## Plan

1. **Investigate Shuffle Issue**:
    - Check `PresetManager` logic for automatic transitions.
    - Verify `ProjectMBridge` and `VisualizerWindow` timers for preset changes.
    - Ensure `shufflePresets` config is actually used to trigger `randomPreset()` vs `nextPreset()`.

2. **Investigate GUI/Resizing Issue**:
    - Check `VisualizerWindow` clear color and alpha settings.
    - Check how `VisualizerPanel` (the container) handles resizing and if it clips the `QWindow` properly.
    - Inspect the `RenderTarget` resizing logic to ensure it doesn't leave junk or have improper alpha.
