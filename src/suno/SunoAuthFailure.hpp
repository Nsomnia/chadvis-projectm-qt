#pragma once
// Purpose: Single classifier for Suno API authentication failures (HTTP 401 /
//          403) so every consumer funnels through one check. This class does
//          NOT perform token refreshes or network I/O; callers decide what
//          to do after classification.

namespace vc::suno {

/// True when a reply's HTTP status indicates an expired or invalid Suno
/// session. The status is the ONLY input, deliberately: server text, byte
/// counts, clip ids and transfer error strings have all been observed to
/// contain "401"/"Unauthorized" with no auth failure present, and matching
/// them tore down live sessions. A caller with no status is by construction
/// not an auth failure — a transport error reads as 0 (the attribute is
/// invalid and toInt() defaults), and a pure message broadcast has no status
/// at all.
inline bool isAuthFailure(int httpStatus) { return httpStatus == 401 || httpStatus == 403; }

} // namespace vc::suno
