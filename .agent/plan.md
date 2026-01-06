# Suno Integration Plan

## Overview
Implement a Suno AI song library scraper and database within the project. This includes a Qt-based GUI for cookie input, metadata scraping, and karaoke-style lyric display using Suno's word-level alignment data.

## Tasks
1. [ ] **GUI Design**
   - Create a dialog/popup for cookie input.
   - Display the JS snippet for the user to copy/run in their browser.
   - Provide a "Copy Snippet" button.
2. [ ] **Suno Scraper Implementation**
   - Implement `SunoClient` in C++ (or using a script bridge if needed).
   - Fetch song feed from `https://studio-api.prod.suno.com/api/feed/v2`.
   - Fetch aligned lyrics from `https://studio-api.prod.suno.com/api/gen/{song_id}/aligned_lyrics/v2/`.
3. [ ] **Database Management**
   - Use SQLite to store song metadata.
   - Schema: `id`, `suno_id`, `title`, `image_url`, `audio_url`, `lyric_text`, `aligned_lyrics_json`, `model`, `created_at`, `tags`.
4. [ ] **Karaoke Engine Integration**
   - Research/Integrate a library for word-by-word lyric display (e.g., AMLL-style).
   - Implement a visualizer overlay for synced lyrics.
5. [ ] **Refinement**
   - Handle pagination of the Suno feed.
   - Error handling for expired cookies.

## JS Snippet for Cookie
```javascript
(function() {
  const cookie = document.cookie;
  const clerkVersion = '5.15.0'; // Example version from suno-api
  // Actually, users just need the whole Cookie string from the request headers usually.
  // We can provide a snippet that extracts it or just instruct them.
  prompt("Copy your Suno Cookie:", document.cookie);
})();
```

## Relevant Repositories Found
- `gcui-art/suno-api`: Excellent API reference.
- `xiliourt/Suno-Lyrics`: LRC/SRT generation from Suno data.
- `Linho1219/AMLL-Editor`: Advanced lyric timing inspiration.
