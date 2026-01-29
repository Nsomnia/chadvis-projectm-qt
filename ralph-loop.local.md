---
active: true
iteration: 3
max_iterations: 0
completion_promise: null
started_at: "2026-01-29T00:00:00Z"
---

## Ralph Loop Status: ITERATION 2 - INVESTIGATION COMPLETE ✅

### Summary of Investigation

**Suno Authentication System:**
- ✅ **Status: WORKING** - No changes needed
- `SunoCookieDialog` properly uses `QWebEngineView` for browser login
- Cookie capture (`__session`, `__client`) works correctly  
- Token refresh via Clerk API is functional
- Auth state persists in config and database
- All authentication logic is production-ready

**Karaoke/Lyrics System:**
- ⚠️ **Status: NEEDS REWRITE** - Major improvements needed
- `KaraokeWidget` has basic functionality but needs smoother rendering
- `LyricsAligner` works but alignment logic is complex
- Missing: Search in Suno library, Lyrics panel tab, subtitle export toggle
- Suno lyrics fetch works but "processing" state handling is messy

### Key Findings

1. **Suno auth is DONE** - The previous agent completed it well
2. **Karaoke needs work** - But the foundation exists
3. **Database is solid** - SQLite schema with migrations works
4. **Lyrics parsing works** - JSON, SRT, LRC all supported

### Created Branches

1. `suno-auth-refactor` - Contains working Suno auth (completed)
2. `feat/video-recording-rewrite` - Contains recording improvements (completed)  
3. `beta/integration` - Integration branch for testing all features
4. `feat/suno-karaoke-rewrite` - Ready for karaoke implementation

### Next Steps (Ralph Loop Iteration 3)

**Option A: Implement Karaoke Rewrite**
- Create `src/lyrics/` directory with new architecture
- Implement LyricsLoader, LyricsSync, LyricsRenderer
- Create LyricsPanel canvas tab
- Rewrite KaraokeWidget with smoother rendering
- Add subtitle file export (.srt, .lrc)
- Add search to Suno library

**Option B: Create Beta Release**
- Test beta/integration branch
- Fix any merge issues
- Create documentation for beta testers
- Prepare for master merge

**Option C: User Decision Needed**
- Which feature is higher priority?
- Should we merge current work to master first?
- Any specific karaoke features wanted?

### Files Ready for Implementation

Based on TODO.md analysis:
- `src/suno/SunoDatabase.cpp` - Add searchClips() implementation
- `src/ui/KaraokeWidget.cpp` - Gut and rewrite
- `src/ui/LyricsPanel.cpp` - Create new (NEW FILE)
- `src/lyrics/LyricsLoader.cpp` - Create new (NEW FILE)
- `src/lyrics/LyricsSync.cpp` - Create new (NEW FILE)
- `src/lyrics/LyricsRenderer.cpp` - Create new (NEW FILE)

### Notes for Future Agents

The Suno authentication system that was described as potentially incomplete is actually **well-implemented and working**. The previous agent (or the current codebase) has:

1. Working QWebEngineView login dialog
2. Proper cookie extraction and storage
3. Clerk API token refresh
4. Database persistence
5. Error handling and retry logic

**DO NOT REWRITE THE AUTH SYSTEM** - Focus on karaoke/lyrics improvements only.

The main work items are:
1. Suno library search functionality
2. Lyrics panel/tool tab
3. Karaoke display improvements
4. Subtitle file export toggle

---

*"The auth system works, the karaoke doesn't. Focus on the lyrics, not the login."* - Ancient Agent Wisdom
