# Architecture Overview
## Philosophy
"I use Arch, BTW" - We build things RIGHT.
This codebase follows:
- **Single Responsibility**: One file = one concern
- **Modular Design**: Components are independently testable
- **Clean Interfaces**: Dependencies flow inward
- **No God Objects**: If it does too much, split it
## Layer Diagram
```
┌─────────────────────────────────────────┐
│ GUI │
│ (MainWindow, VisualizerWidget, etc.) │
├─────────────────────────────────────────┤
│ Application │
│ (App lifecycle, config, CLI parsing) │
├─────────────────┬───────────────────────┤
│ projectM │ Audio │
│ Integration │ Handling │
├─────────────────┴───────────────────────┤
│ Core │
│ (Types, Interfaces, Utilities, Logger) │
├─────────────────────────────────────────┤
│ Platform │
│ (Linux-specific, OpenGL helpers) │
└─────────────────────────────────────────┘
```
## Directory -> Responsibility Mapping
| Directory | Responsibility |
|-----------|---------------|
| src/app/ | Application lifecycle, configuration |
| src/core/ | Types, interfaces, utilities (no Qt GUI) |
| src/projectm/ | projectM v4 integration |
| src/audio/ | Audio file loading, PCM handling |
| src/gui/ | Qt widgets and windows |
| src/platform/ | OS-specific implementations |
## Key Design Decisions
See docs/decisions/ for Architecture Decision Records (ADRs).
