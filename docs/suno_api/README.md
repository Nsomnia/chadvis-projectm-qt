# Suno API Documentation Index

> ⚠️ **Disclaimer — Unofficial, Reverse-Engineered API**
>
> Suno provides **no official public API**. Everything in this directory was
> reverse-engineered by observing the production web app's network traffic,
> scanning assets, and probing endpoints. It is undocumented, subject to change
> without notice, and use of it may violate Suno's Terms of Service. ChadVis
> uses this knowledge to build a better client experience for its users' own
> accounts; nothing here is endorsed by or affiliated with Suno. Expect breakage.

## Overview

Suno has no official public API. This documentation set covers reverse-engineered endpoints observed in the production web app and related services.

## Base URLs

- `https://suno.com` - Main web app
- `https://auth.suno.com/v1` - Clerk auth
- `https://studio-api-prod.suno.com` - Studio API (main backend)
- `https://studio-api.sky.suno.com` - Sky environment
- `https://suno-ai--orpheus-prod-web.modal.run` - Orpheus chat backend (Modal)
- `https://cdn-o.suno.com` - CDN for audio/images
- `https://clerk.suno.com` - Clerk SDK
- `https://hcaptcha-endpoint-prod.suno.com` - hCaptcha

## Authentication

Authentication is two-tier:

1. Clerk session cookies authenticate the browser session.
2. The session is exchanged for a JWT bearer token.

### Key cookies

- `__session`
- `__client`
- `__client_uat`
- `__client_uat_Jnxw-muT`

### JWT exchange

- `POST /v1/client/sessions/{sid}/tokens`

### JWT claims

- `suno.com/claims/user_id`
- `https://suno.ai/claims/clerk_id`
- `suno.com/claims/token_type`
- `aud: "suno-api"`
- `fva: [0, -1]`

### Required headers

- `Authorization: Bearer {jwt}`
- `Device-Id: {uuid}`
- `Browser-Token: {base64}`

## API Categories

- [Auth & Session](auth.md)
- [Generation](generation.md)
- [Library & Feed](library.md)
- [Billing & Credits](billing.md)
- [Projects & Studio](projects.md)
- [Persona & Voice](persona.md)
- [Social & Feed](social.md)
- [B-Side & Experimental](b-side.md)
- [Feature Flags & Gates](feature-flags.md)
- [Upload & Processing](upload.md)

## Model Versions

- **V3** (`chirp-v3`, `chirp-v3-5`) - radio quality, 2 min
- **V4** (`chirp-v4`) - 4 min, covers, 2-stem separation
- **V4.5** (`chirp-v4-5`) - 8 min, Creative Style Helper
- **V5** (`chirp-v5`, `chirp-crow`) - studio-grade, 12-stem separation, Persona Voices
- **V5.5** (`chirp-v5-5`) - Voice Cloning (verified), Custom Models (3 per user), My Taste

## Key Notes

- All generation is async: requests return `task_id`; poll for completion.
- `430` status indicates requests are too frequent.
- `429` status indicates insufficient credits.
- hCaptcha may be required; check `/api/c/check` first.

## Raw Data

- [`raw/endpoints_sniffed.list`](raw/endpoints_sniffed.list) — unfiltered endpoint sniff dump (706KB, one URL per line). See [`raw/README.md`](raw/README.md) for provenance.

## Related Documents

- [Recon Archive](RECON-ARCHIVE.md) - Consolidated early reconnaissance notes (supersedes the former `SUNO_API_NOTES.md` and `SUNO_B_SIDE_DISCOVERY.md`)
- [Suno Integration Deep Dive](../integration/SUNO.md)
- [Endpoint Inventory](ENDPOINT-INVENTORY.md)
