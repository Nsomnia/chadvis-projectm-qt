---
schema: 1
updated: 2026-10-31
source: "scratch/suno-missing-api-flows.json (Puppeteer replay, 732 steps, user-recorded 2026-10-30)"
grade: "UI evidence only — [LEAD]. A replay records clicks, not requests: it proves a
surface EXISTS and what its controls are, never a method, path, or body. Every route
behind a surface below is still uncaptured until a HAR or a `[T1]` probe names it."
---

# suno.com UI surface inventory (from a recorded session)

What the web client's own UI exposes, observed by driving it. Useful for T0346's
family list: a capability that has a button is a capability whose API exists
somewhere. It is **not** an endpoint catalogue — see
[`ENDPOINT-INVENTORY.md`](ENDPOINT-INVENTORY.md) for contracts.

Recorded flow, in order: Create → sign-out (Google credential picker →
`platform.suno.com` error) → sign-in → `/me` library → filters/sort → playlist
create/rename/description/cover → playlists page → playlist open → remix/mashup/
sample/trim → playbar/queue → `/me` trash → restore/delete → recover/delete
playlist → Explore → Speech/Sounds/Voices/cover-art generation.

## New host seen: `platform.suno.com`

Signing out sent the browser to
`https://platform.suno.com/auth/error?message=No platform account found for this
email. Contact your account administrator.`

That page's own copy names a **platform account** distinct from a Suno account,
and offers "Try Again" / "Sign in with Google". Read as a lead: there is a
platform/developer console on its own host, reachable through an auth flow that
failed for this account. Do **not** wire `platform.suno.com` — it is not a
`CapturedHosts` entry, and "an account does not exist there" is not a contract.
Capture it deliberately or leave it alone.

## Library (`/me`)

- Filters: Liked, Disliked, Public, Private, Uploads, Full song, Cover,
  Voices-only, downloaded-only, Hide Disliked, Hide Stems.
- Sort: Oldest, Newest, Most liked, Least liked.
- Views: grid / list.
- Sections in the sidebar: Albums, Workspaces, Studio Projects, Cover Art, Hooks,
  Liked Hooks, History, Trash, Playlists.
- Trash actions: **Restore to Library**, **Delete Permanently** (confirmation
  dialog). Both were clicked; neither is wired here (T0267).

## Playlists

- Create, rename (inline title edit), description, a switch (public/private),
  cover art (generate or upload/remix), "More actions" → **Recover playlist**,
  **Delete playlist**.
- Playlist page: "More options" → Reuse Prompt; add-to-playlist from a clip;
  Mashup; remove-clip; Cover; Edit with trim handles (start/end times typed in).

## Explore

- "Best of v6" feed; Create entry points: Speech (script + tone), Sounds
  (sound description + BPM/key/mode + Apply), Record → **Start recording** →
  **Name this Voice**, "Prompt Suno to create an image…" (cover art).
- Read as: the v6 create surface is a **multi-modal picker** (song / speech /
  sounds / voice / cover art), not the single song form this client models.

## Playbar

Play/pause, previous, shuffle, repeat, Queue, **Add to Queue**, Like/Dislike,
Copy Link, song details panel. Queue management is a first-class surface here
and has no counterpart in ChadVis (T0321's open half).

## Endpoint families this re-orders

- Playlist CRUD **including recover and delete** is fully exposed in the UI, not
  just create/trash (T0264/T0268).
- Trash is a real two-step surface (restore vs permanent delete), so `gen/trash`
  has a visible counterpart even though the route is uncaptured (T0267).
- Voices are creatable from a recording **inside Explore**, which is broader than
  the persona family assumed (T0263/T0349).
- Cover art generation is reachable from a playlist and from Explore (T0262).
- Hooks and Liked Hooks are library sections (T0264-class), and Albums,
  Workspaces, Studio Projects, Cover Art, History are too.

## What is still missing, and can only come from a network capture

For every line above: method, path, request body, response status and shape.
The replay contains **zero** request data (`api/` appears 0 times), so no row can
be promoted to `[T1]` from it. The next artifact needed is one HAR export
(DevTools → Network → Export HAR, "with content") recorded while repeating these
surfaces; see the local checklist file in the repo root (gitignored).
