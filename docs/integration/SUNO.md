# 🤖 Suno AI Integration: The Deep Dive

ChadVis is arguably the most advanced desktop client for Suno AI. Here's how we bridge the gap between AI generation and real-time visualization.

---

## 🔐 Authentication Lore

Suno uses a complex Clerk-based auth system. We've mastered it so you don't have to.

1.  **Session Cookies**: We take your `__client` and `__session` cookies.
2.  **JWT Extraction**: We pull the Bearer token from the session.
3.  **Persistence**: We store these in a persistent Qt WebEngine profile. No more logging in every time you restart the app.
4.  **Auto-Refresh**: Our `SunoClient` knows how to talk to the Clerk API to refresh tokens when they expire.

---

## 🗄️ Persistence (SQLite)

We don't just stream; we cache.
- **`SunoDatabase`**: A local SQLite DB that stores all your clip metadata.
- **Migration System**: We have an auto-migration system that updates your schema without nuking your data.
- **Synced Lyrics**: We store the word-level aligned JSON in the DB for that perfect karaoke experience.

---

## 🎤 Karaoke & Alignment

1.  **The Good Stuff**: If Suno provides aligned lyrics, we use them.
2.  **The Heuristic Fallback**: If there's no alignment, our `SunoLyrics::align` algorithm attempts to map the text to the audio duration using frequency analysis and black magic.
3.  **Rendering**: The `OverlayEngine` takes these timestamps and renders them with sub-pixel precision.

---

## 🗣️ The Tech Talk

**Senior Dev:** "The sync queue is rate-limited and includes jitter. We're not trying to DDOS Suno. We're being polite Chads. We fetch about 1.8 clips per second."

**Richard Stallman:** "Wait, 'Suno' is a proprietary service! You are encouraging people to sacrifice their computational sovereignty for a collection of machine-generated sounds! This integration is a Trojan horse for the cloud-based surveillance state!"

**Linus (LTT):** "Richard, it's just cool music. And look at the UI! The progress bars are so smooth. I can sync my 3,000 song library in about 20 minutes. That's faster than I can cable manage a PC! **Speaking of cable management... LTT Store dot com!**"

---

> "AI music isn't the future. It's the present. And ChadVis is the best way to see it." — *The Senior Dev*
