---
schema: 1
updated: 2026-10-31
grades: "This file records our own unauthenticated observations at `[T1]`; everything
else is announcement/marketing prose at `[LEAD]`. No credentials were attached to any
probe below: every request was unauthenticated, and the responses carry no secrets."
---

# Suno MCP — `https://api.suno.com/mcp`

Suno launched an official Model Context Protocol server (beta, early access).
Connected by URL + one-time sign-in; no API key, no code. Songs made through it
save to the caller's Suno library and spend its credits, per the announcement
([suno.com/mcp](https://suno.com/mcp), [help article](https://help.suno.com/en/articles/14486145)).
Early access: <https://sunomusic.typeform.com/to/JMPYxZqQ> (24–48 h turnaround).

Works with MCP-connector tools: ChatGPT, Claude, Claude Code, Cursor, Gemini —
and any MCP client we can build.

## Why it matters here

The T0356 OAuth loopback flow asked for `resource=https://api.suno.com/mcp`
because the reference MCP client did. The resource now **exists live**. The
authorization server it names in discovery is the **same** `external/oauth`
family T0356's `register` probe exercised — the whole chain is one system:

```
suno MCP auth chain, verified 2026-10-31, unauthenticated:

  api.suno.com/mcp                401  (Bearer challenge, see below)
  └─ WWW-Authenticate: Bearer resource_metadata=
        https://api.suno.com/.well-known/oauth-protected-resource/mcp

  …/oauth-protected-resource/mcp   200
  └─ authorization_servers[0] = https://studio-api-prod.suno.com/api/v2/external/oauth
     scopes_supported = api:v0 credits:spend download
                        library:read library:search library:write
                        offline_access profile:read

  studio-api-prod…/api/v2/external/oauth/.well-known/oauth-authorization-server
                                   200  (RFC 8414, below)
```

That closes the mapping side of T0356(b): the loopback-registered client's
access token is a **bearer for this resource**. What still needs one authorized
exchange (never committed): mint a token for one of our registered `127.0.0.1`
clients and answer `initialize` with it — that settles whether the MCP bearer
also speaks the studio-api routes this client already uses, or only MCP.

## Auth surface `[T1]`

All of the following observed live, unauthenticated (`curl`, 2026-10-31). Full
body of the first two is in git history via this file; never logged with credentials.

### Resource server

`GET https://api.suno.com/mcp`

- Unauthenticated GET (SSE accept) → **401**, JSON body
  `{"error":"sign in to Suno to use this server"}`.
- Unauthenticated POST `initialize` (JSON-RPC, `Accept: application/json, text/event-stream`)
  → same 401 body; server **is** JSON-RPC-over-HTTP behind auth.
- `WWW-Authenticate: Bearer resource_metadata="…/oauth-protected-resource/mcp"`
  — RFC 9728 pointer, exactly the MCP-spec auth flow.

`GET https://api.suno.com/.well-known/oauth-protected-resource/mcp` → 200:

- `resource`: `https://api.suno.com/mcp`
- `authorization_servers[0]`: `https://studio-api-prod.suno.com/api/v2/external/oauth`
- `bearer_methods_supported`: `["header"]`
- `scopes_supported` (8): `api:v0`, `credits:spend`, `download`,
  `library:read`, `library:search`, `library:write`, `offline_access`, `profile:read`

### Authorization server (RFC 8414, both well-known spellings → 200)

issuer `https://studio-api-prod.suno.com/api/v2/external/oauth`:

| Endpoint | Value |
| :--- | :--- |
| authorization_endpoint | `…/api/v2/external/oauth/authorize/` |
| token_endpoint | `…/api/v2/external/oauth/token/` |
| revocation_endpoint | `…/api/v2/external/oauth/revoke_token/` |
| registration_endpoint | `…/api/v2/external/oauth/register/` |
| jwks_uri | `…/api/v2/external/oauth/.well-known/jwks.json` |

- `response_types_supported`: `code`
- `grant_types_supported`: `authorization_code`, `refresh_token`
- `code_challenge_methods_supported`: `S256` (PKCE required-able)
- `token_endpoint_auth_methods_supported`: `none`, `client_secret_basic`, `client_secret_post`
- `client_id_metadata_document_supported`: `true` — a client can be registered by
  pointing at its own served metadata document instead of dynamic registration.
- `scopes_supported` (21): `read_profile`, `generate_music`, `read_music`,
  `api:v0`, `offline_access`, `credits:spend`, `library:write`, `download`,
  `profile:read`, `profile:write`, `create:read`, `create:search`,
  `library:read`, `library:search`, `create:generate`, `create:upload`,
  `create:playback`, `catalog:read`, `catalog:sync`, `catalog:link`,
  `studio:write`, `telemetry:write`

Note the mismatch: the resource declares 8 MCP scopes; the server accepts 21.
The extra `create:*` / `catalog:*` / `studio:*` scopes are the wider OAuth
surface, MCP being one consumer of it.

## Announced capabilities `[LEAD]`

From the announcement page, in product terms:

- **Create conversationally** — plain-text prompt; the assistant supplies lyrics/styles.
- **Upload own audio** — voice memos/soundbites → full songs.
- **Remix anything** — Covers, Mashups, Extend over *your library* songs, from chat.
- **Use your Voice** — saved Suno voices sing new creations.

Example prompts from the page (for prompt-engineering later, not commitments):
cover of last song slowed down; voice memo → indie-folk track keeping melody;
lo-fi beat + 3 mood variations; iterate with "Make it slower" / "Add a bridge" / "Give me three more".

## Plan facts `[LEAD]` (help FAQ)

- Beta works on **all plans including Free**; generation spends credits per plan.
- Multiple AI assistants may connect to one account.
- Songs appear in the normal Suno library; credits are queryable via MCP ("ask
  your AI how many credits you have left").

## Connection how-to `[LEAD]`

Two steps per the page: add a custom connector/MCP server in the assistant's
settings, give it the URL `https://api.suno.com/mcp`, then complete the Suno
sign-in + approval once. No key is issued. Official per-assistant setup
(help article, edited 2026-10):

- **Claude (web/desktop)**: Customize → Connectors → + → Add custom connector.
  Name `Suno`, URL `https://api.suno.com/mcp`. Select Connect → sign in → approve.
- **Claude Code** (terminal):
  `claude mcp add --transport http suno https://api.suno.com/mcp`
  then `/mcp` in a session to sign in.
- **Gemini**: Customize → Connectors → + → Add Custom MCP server → URL → Create.
  Mention `@Suno` in chat to use it.
- **ChatGPT**: Plugins → Add → Add custom MCP server. Name `Suno`,
  Connection (Server URL) `https://api.suno.com/mcp` → "Create as a plugin" →
  "Connect Suno" → authorize.
- **Cursor**: Customize → MCPs → New MCP Server (User) → `mcp.json`:

  ```json
  {
    "mcpServers": {
      "suno": {
        "url": "https://api.suno.com/mcp"
      }
    }
  }
  ```

- **Other assistants**: any tool that takes a custom MCP server/connector URL —
  same URL, no key. For ChadVis itself the equivalent is T0357: speak MCP as a
  client, walk the OAuth chain above, and chat-drive generation through it.

## Disciplinary note

- Everything above was observed **without attaching a credential**. When a token
  is minted for ChadVis, record status + top-level keys + counts only — body,
  token, and header never leave the session (AGENTS.md credential rule).
- Host discipline: `api.suno.com` is a **new host** for `CapturedHosts.cpp` if and
  when we call it **authenticated**; the unauthenticated probes above are not a
  capture. Until then this doc is the record.
- Board: T0356 (auth loopback), T0357 (MCP chat surface). Recon linkage lives in
  TODO.md, not here.
