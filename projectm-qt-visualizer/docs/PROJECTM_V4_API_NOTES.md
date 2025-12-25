# projectM v4 API Notes
## CRITICAL: v4 vs v3
| Aspect | v3 | v4 |
|--------|----|----|
| API Style | C++ | C |
| Header | projectM.hpp | projectm-4/projectm.h |
| Handle | class projectM | projectm_handle |
| Arch Package | projectM | libprojectm |
## v4 C API Quick Reference
### Lifecycle
```c
#include <projectm-4/projectm.h>
projectm_handle pm = projectm_create();
// ... use ...
projectm_destroy(pm);
```
### Configuration
```c
projectm_set_window_size(pm, width, height);
projectm_set_preset_path(pm, "/usr/share/projectM/presets");
```
### Rendering
```c
// Call in OpenGL context
projectm_opengl_render_frame(pm);
```
### Audio Input
```c
float pcm_data[2048]; // stereo interleaved
projectm_pcm_add_float(pm, pcm_data, 1024, PROJECTM_STEREO);
```
### Presets
```c
projectm_load_preset_file(pm, "/path/to/preset.milk", true);
projectm_playlist_play_next(pm, true);
projectm_playlist_play_previous(pm, true);
```
## Arch Linux Specifics
Preset path: `/usr/share/projectM/presets/` (if installed)
Install presets: `sudo pacman -S projectm-presets-cream-of-the-crop` or similar
