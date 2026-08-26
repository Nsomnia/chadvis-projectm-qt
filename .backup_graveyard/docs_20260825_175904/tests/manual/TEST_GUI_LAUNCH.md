# Manual Test: GUI Launch
## Test ID: MT-001
## Category: GUI
## Prerequisites
- Application compiled successfully
- Desktop environment available
## Steps
1. Run the application:
```bash
./build.sh run
```
2. Observe the window:
- [ ] Window appears
- [ ] Window title shows "projectM Visualizer - Chad Edition"
- [ ] Window is resizable
- [ ] Menu bar visible with File and Help menus
3. Test File menu:
- [ ] File -> Open Audio File... shows file dialog
- [ ] File -> Exit closes the application
4. Test Help menu:
- [ ] Help -> About shows about dialog with "I use Arch, BTW"
5. Test the visualization area:
- [ ] Central area shows visualization (or hot pink if broken)
- [ ] Visualization animates (even without audio)
## Expected Results
- All checkboxes should pass
- No crashes or freezes
- Console should not show errors
## Report Format
```
MT-001 Result: [PASS/FAIL]
Date: [date]
Notes: [any observations]
```
