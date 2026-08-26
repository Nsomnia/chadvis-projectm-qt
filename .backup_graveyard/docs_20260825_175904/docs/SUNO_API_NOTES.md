# Suno API Reconnaissance Notes

Based on internal reconnaissance of network traffic and endpoint scans.

## Base URLs
- **Primary API**: `https://studio-api-prod.suno.com`
- **Frontend/Clerk**: `https://api.suno.ai` (Auth handled via Clerk)

## Key Endpoints
- `POST /api/feed/v3`: Likely the main library/feed retrieval endpoint. Supports pagination.
- `GET /api/clips/get_songs_by_ids`: Bulk retrieval of track metadata by ID.
- `POST /api/generate/v2-web/`: Song generation endpoint.
- `GET /api/billing/info/`: Subscription and credit balance.
- `GET /api/personalization/settings`: User preferences and experimental feature flags.

## Auth Strategy
- Authorization headers typically use a Bearer token obtained from Clerk session.
- Cookies `__client_uat` and session tokens are critical for persistent auth.

---
*Date: 2026-04-21*
