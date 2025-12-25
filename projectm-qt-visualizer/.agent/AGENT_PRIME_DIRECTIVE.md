# AGENT PRIME DIRECTIVE - READ FIRST
Welcome, fellow Chad developer. You've inherited this codebase. Here's how we roll.
## Core Philosophy: "I use Arch, BTW"
We do things RIGHT. No shortcuts. No hacks. Clean, modular, tested code.
## THE RULES (Non-Negotiable)
### 1. NEVER USE DESTRUCTIVE COMMANDS
```bash
# FORBIDDEN:
rm, rmdir, rm -rf, unlink
# INSTEAD USE:
./scripts/utils/backup_file.sh <path>
# This moves to .backup_graveyard/ with timestamp
```
### 2. SMALL STEPS ONLY
```
WRONG: Implement 5 features, then compile, then wonder why nothing works
RIGHT: One change -> compile -> test -> verify -> next change
```
### 3. WHEN IN DOUBT, ASK THE HUMAN
You CANNOT verify:
- Does the window look right?
- Does the audio sound correct?
- Is the visualization smooth?
- Does the UI feel responsive?
SAY: "HUMAN_VERIFICATION_REQUIRED: [what to test]"
THEN: STOP AND WAIT
### 4. DOCUMENT EVERYTHING
The next agent session has NO MEMORY of what you did.
Write it down or it's lost forever.
Update: .agent/CURRENT_STATE.md, .agent/SESSION_HANDOFF.md
### 5. RESEARCH BEFORE CODE
```bash
# Search for help:
gh search repos "projectm v4" --language=cpp
lynx -dump https://some-docs-page.com
git clone <reference-repo> scratch/refs/<name>
```
## Quick Commands
```bash
./scripts/check_deps.sh # Verify dependencies
./scripts/build.sh # Standard build
./scripts/run_app.sh # Run the application
./scripts/utils/backup_file.sh # Safe "delete"
```
## Current Session Checklist
- [ ] Read .agent/CURRENT_STATE.md
- [ ] Read .agent/NEXT_TASKS.md
- [ ] Check .agent/KNOWN_ISSUES.md
- [ ] Do ONE task
- [ ] Update documentation
- [ ] Update .agent/SESSION_HANDOFF.md
