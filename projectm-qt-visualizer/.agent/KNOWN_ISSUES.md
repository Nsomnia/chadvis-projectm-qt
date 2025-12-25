# Known Issues & Gotchas
## projectM v4 Specific
### Package Naming Confusion
- `projectM` (pacman) = v3, C++ API - WRONG
- `libprojectm` (pacman) = v4, C API - CORRECT
```bash
# Verify you have the right one:
pacman -Qi libprojectm
# Should show version 4.x.x
```
### Include Path
```cpp
// WRONG (v3 style):
#include <projectM.hpp>
// CORRECT (v4 style):
#include <projectm-4/projectm.h>
```
### Library Path
Libraries are in `/usr/lib/` but pkg-config is `projectm-4`
```cmake
find_package(PkgConfig REQUIRED)
pkg_check_modules(PROJECTM4 REQUIRED projectm-4)
```
## OpenGL Gotchas
### Qt OpenGL Widget State
Qt's QOpenGLWidget manages its own FBO. projectM also wants to manage GL state.
- MUST call `makeCurrent()` before any GL operations
- projectM's `projectm_opengl_render_frame()` may modify GL state
- May need to reset viewport after projectM renders
### GL Version
projectM v4 needs OpenGL 3.3+
Qt6 defaults to requesting highest available - should be fine on Arch.
## Audio
### Silent Mode
projectM NEEDS audio data even when silent. Feed zeros:
```cpp
std::vector<float> silence(2048, 0.0f);
projectm_pcm_add_float(handle, silence.data(), silence.size(), PROJECTM_STEREO);
```
