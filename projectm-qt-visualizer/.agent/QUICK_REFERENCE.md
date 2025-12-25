# Quick Reference Card
## Build Commands
```bash
./scripts/build.sh # Debug build
./scripts/build_release.sh # Release build
./scripts/build_clean.sh # Clean + rebuild
./scripts/run_app.sh # Run application
./scripts/run_tests.sh # Run test suite
```
## Safe Operations
```bash
./scripts/utils/backup_file.sh <file> # "Delete" safely
./scripts/git/commit_safe.sh "message" # Commit with checks
```
## Research Commands
```bash
# Search GitHub for projectM projects
./scripts/utils/search_projectm_repos.sh
# Dump webpage for parsing
./scripts/utils/dump_webpage.sh <url>
# Clone to scratch
git clone <url> scratch/refs/<name>
```
## Package Management (Arch, BTW)
```bash
sudo pacman -S <package> # Official repos
paru -S <package> # AUR
pacman -Ss <query> # Search
pacman -Qi <package> # Info
```
## projectM v4 API Cheatsheet
```c
#include <projectm-4/projectm.h>
// Create
projectm_handle h = projectm_create();
// Configure
projectm_set_window_size(h, width, height);
// Render (call in GL context)
projectm_opengl_render_frame(h);
// Audio
projectm_pcm_add_float(h, data, count, PROJECTM_STEREO);
// Cleanup
projectm_destroy(h);
```
## Qt OpenGL Pattern
```cpp
void VisualizerWidget::initializeGL() {
// One-time setup
}
void VisualizerWidget::paintGL() {
// Called each frame - render here
}
void VisualizerWidget::resizeGL(int w, int h) {
// Handle resize
}
```
