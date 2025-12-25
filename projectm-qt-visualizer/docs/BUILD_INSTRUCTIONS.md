# Build Instructions
## Prerequisites (Arch Linux)
```bash
# Core dependencies
sudo pacman -S qt6-base qt6-multimedia libprojectm cmake gcc
# Optional tools
sudo pacman -S github-cli lynx
```
## Verify Setup
```bash
./scripts/check_deps.sh
```
## Build
### Debug Build (default)
```bash
./scripts/build.sh
```
### Release Build
```bash
./scripts/build_release.sh
```
### Clean Rebuild
```bash
./scripts/build_clean.sh
```
## Run
```bash
./scripts/run_app.sh
```
## Test
```bash
./scripts/run_tests.sh
```
## Troubleshooting
See docs/troubleshooting/ for common issues.
