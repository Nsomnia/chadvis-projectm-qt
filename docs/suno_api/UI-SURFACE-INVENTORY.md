---
schema: 1
updated: 2026-11-06
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

## What the 2026-11-06 HAR closed — `[T1]`

A signed-in full-API capture (`~/Documents/suno-recon-har-recordings-full-api/suno.com_Archive.har`)
supplied the request data this replay could not. Contracts in
[`ENDPOINT-INVENTORY.md`](ENDPOINT-INVENTORY.md) §4.10 and §5.14.

| Surface above | Now has a `[T1]` route |
|---|---|
| Playlist create / rename / description / public switch | `POST /playlist/create/`, `PATCH /playlist/v2/{id}` |
| Playlist delete | `POST /playlist/trash/` (204) |
| Playlist cover art | `POST /playlist/v2/{id}/cover-image`, `POST /video_gen/image/generate`, `GET /cms/cover-art-suggestions` |
| Record → Name this Voice | `POST /uploads/audio/` → `initialize-clip/` → `POST /clip/{id}/vox-stem` → `GET /processed_clip/{id}` → `POST /persona/create/` |
| Library filter: Uploads | `feed/v3` filter key `upload` |
| Library filter: Voices-only | `feed/v3` filter key `persona` |
| Workspaces / Studio Projects | `GET /project/me`, `GET /project/{id}`, `GET /project/{id}/pinned-clips`, `POST /project/{id}/clips` |
| Cover / mashup via uploaded audio | same upload chain; the generated persona is the reusable input |

## What the 2026-11-06/07 live UI capture closed — `[T1]`

A signed-in Chrome walk plus the owner's DevTools HAR (`suno.com4.har`) closed
the remaining surface list; full bodies are in
[`ENDPOINT-INVENTORY.md`](ENDPOINT-INVENTORY.md) §4.12.

| Surface above | Now has a `[T1]` route |
|---|---|
| Hooks / Liked Hooks | `GET /video/hooks/user_hooks`, `GET /video/hooks/me/liked/v2`, `GET /video/hooks/suggested_clips` |
| Albums | `POST /unified/feed` with `feed_id:"library_albums"` |
| History | `GET /profiles/listen-history?limit=20` |
| Trash restore-vs-permanent-delete | `POST /gen/trash` `{trash,clip_ids}` — restore path 200; permanent delete not clicked |
| Playlist recover | `POST /playlist/v2/{id}/trash` `{"undo":true}` (round-tripped) |
| Speech / Sounds | both share `POST /generate/v2-web/` (`metadata.is_speech`, `task:"sound"`) |
| Trim-handle save | **no standalone route** — the range rides the generation body |
| Studio pages | `GET /studio/list-projects`, `POST /studio/create-project`, `GET /studio/project/{id}`, `GET /agent/conversation/{id}`, `GET /plugins/catalog`, `GET /plugins/{id}/presets` |

**Still open after this pass, and why:** persona visibility (server-disabled
for an unused voice), `v2/integrations` with a real MCP connection (needs an
externally hosted MCP server), `preferences/submit` success shape and
`project/{id}/clips` `update_type:"remove"` (both need an app-originated call;
console fetches are 401), and `visual/models` / `suno_chat` bodies (403
`feature_disabled`).

