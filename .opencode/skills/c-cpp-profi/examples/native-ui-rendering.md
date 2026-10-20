# Example: Native UI Or Rendering Change

Use for native UI, terminal UI, graphics, images, video, CAD, games, font rendering, color, or pixel/artifact output.

## Starting Point

```text
Task: change rendering layout or drawing behavior.
Boundary: visual artifact and user-observable pixels.
Primary risks: DPI, font stack, color mode, platform backend, nondeterministic rendering, performance.
```

## Required Skill Path

1. Read `NATIVE-UI-GOLDENS.md`, `PERFORMANCE.md`, and `QUALITY-GATES.md`.
2. Capture baseline artifact before editing:

```text
viewport/window:
scale factor:
font stack:
color mode:
platform/backend:
baseline artifact path:
```

3. Change one visual lever at a time.
4. Capture candidate artifact under the same matrix.
5. Compare with perceptual or project-approved pixel threshold.
6. For interactive scenes, include motion/interactivity smoke and performance frame-time evidence when relevant.

## Evidence Packet

```text
Native UI evidence:
- Surface:
- Platform/backend matrix:
- Baseline artifacts:
- Candidate artifacts:
- Diff command and threshold:
- Accepted differences:
- Frame-time/performance:
- Accessibility/text-fit notes:
- Residual risk:
```

## Refusal Conditions

- "Looks right" without saved artifact comparison.
- Single DPI/font/color-mode evidence for a multi-platform surface.
- Visual refactor that changes layout and rendering backend in one diff.
