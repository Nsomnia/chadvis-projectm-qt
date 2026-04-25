# Suno Social API

## Profiles

- `GET /api/profiles/` — List profiles
- `GET /api/profiles/{handle}` — Get profile by handle
- `GET /api/profiles/{handle}/info` — Get profile info
- `GET /api/profiles/follow` — Follow profile
- `GET /api/profiles/pinned-clips` — Get pinned clips

## Comments

- `GET /api/comment/{comment_id}` — Get comment
- `POST /api/comment/block-user` — Block user
- `POST /api/comment/unblock-user` — Unblock user
- `POST /api/comment/{comment_id}/reaction` — React to comment
- `POST /api/comment/{comment_id}/replies` — Reply to comment
- `POST /api/comment/{comment_id}/report` — Report comment

## Notifications

- `GET /api/notification/v2` — Get notifications (v2)
- `GET /api/notification/v2/badge-count` — Get badge count
- `GET /api/notification/v2/clear-badge` — Clear badge
- `GET /api/notification/v2/read` — Mark as read

## Sharing

- `GET /api/share/attribute/` — Attribute share
- `GET /api/share/event` — Share event
- `GET /api/share/link` — Share link
- `GET /api/share/stats` — Share stats

## Social Feed

- `GET /api/social/following-feed` — Following feed

## Song Copy/Send

- `GET /api/song_copy/send-song` — Send song copy

## Recommendations

- `POST /api/recommend/hide-creator` — Hide creator from recommendations

## User Data

- `GET /api/user/me` — Current user profile
- `GET /api/user/metadata` — User metadata

## Followers/Following

- `GET /me/followers` — My followers
- `GET /me/following` — My following
