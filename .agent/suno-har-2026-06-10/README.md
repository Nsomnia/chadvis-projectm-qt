# Suno HAR 2026-06-10 Analysis Workspace

This hidden directory holds all notes, scratch, research, and thinking files for the
Suno.com API endpoint inspection HAR log analysis task.

## HAR Source
- File: `0b836191-ff02-4e94-a206-4ee7e79c7e26.txt`
- Captured by: Rest API Inspector v2.0.0
- Device: iOS "Orion" browser
- Date: 2026-06-10T02:12-02:13 UTC
- User: sderek02@gmail.com (Danny "Prompt Engineer" Steel), handle `djdannysteel`, Premier plan

## Key Findings Summary
1. Suno now serves audio in TWO formats via `media_urls` array:
   - m4a-opus on CloudFront: `https://d2lwuy8qc234o3.cloudfront.net/1/clip/{id}.m4a`
   - mp3 on cdn1.suno.ai: `https://cdn1.suno.ai/{id}.mp3`
2. No HLS/DASH streaming audio endpoints found
3. 16 new endpoints not previously documented
4. Orpheus is a new AI assistant feature (currently in CONTROL group)
5. New realtime SSE stream via Ably: `/api/realtime/discover`

## Files
- `har_endpoints_extracted.md` - Full structured extraction of all Suno API endpoints
- `audio_format_matrix.md` - Audio download/stream format analysis
- `endpoint_diff_vs_existing.md` - Diff between HAR endpoints and existing docs
- `implementation_plan.md` - Code change implementation plan
- `security_audit.md` - JWT scrubbing audit and security notes