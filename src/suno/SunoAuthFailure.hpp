#pragma once
// Purpose: Single classifier for Suno API authentication failures (HTTP 401 /
//          "Unauthorized" strings) so every consumer funnels through one check.
// This class does NOT perform token refreshes or network I/O; callers decide
// what to do after classification.

#include <QString>

namespace vc::suno {

/// True when an HTTP status / error message indicates an expired or invalid
/// Suno session. Pass -1 for httpStatus when only a message is available.
inline bool isAuthFailure(int httpStatus, const QString& errorMessage) {
    if (httpStatus == 401) return true;
    return errorMessage.contains("Unauthorized") || errorMessage.contains("401");
}

} // namespace vc::suno
