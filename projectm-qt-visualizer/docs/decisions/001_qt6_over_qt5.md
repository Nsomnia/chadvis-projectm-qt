# ADR 001: Qt6 Over Qt5
## Status
Accepted
## Context
Need a GUI framework for the visualizer.
## Decision
Use Qt6 instead of Qt5.
## Rationale
- Qt6 is current and actively developed
- Better OpenGL/Vulkan integration
- Modern C++ support
- Arch Linux packages Qt6 by default
- "I use Arch, BTW" - we use modern tools
## Consequences
- Positive: Modern API, better performance
- Negative: Some older examples may need porting
