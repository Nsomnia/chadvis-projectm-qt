#pragma once
// SunoEndpoints.hpp - Centralized Suno API endpoint map
// One source of truth for every URL we hit

#include <QString>
#include <string_view>

namespace vc::suno {

/// Convert a std::string_view endpoint constant to QString without the
/// fromUtf8/data()/size() boilerplate repeated at every call site.
inline QString qstr(std::string_view sv) {
    return QString::fromUtf8(sv.data(), static_cast<qsizetype>(sv.size()));
}

} // namespace vc::suno

// Top-level block: writing vc::suno::endpoints INSIDE namespace vc::suno
// would create vc::suno::vc::suno::endpoints and leave the outer namespace
// unclosed, poisoning every include that follows.
namespace vc::suno::endpoints {

// ── Base URLs ──────────────────────────────────────────────
constexpr std::string_view API_BASE       = "https://studio-api-prod.suno.com/api";
constexpr std::string_view MODAL_BASE     = "https://suno-ai--orpheus-prod-web.modal.run";
constexpr std::string_view CDN_BASE       = "https://cdn1.suno.ai";
constexpr std::string_view WEB_BASE       = "https://suno.com";

// NOTE: Clerk auth endpoints/versions live in suno/auth/ClerkAuthClient.hpp
// (AUTH_BASE, LEGACY_BASE, CLERK_API_VERSION, CLERK_JS_VERSION).

// ── Studio API ─────────────────────────────────────────────
constexpr std::string_view LIBRARY        = "/feed/v3";
constexpr std::string_view GENERATE       = "/generate/v2-web/";
constexpr std::string_view ALIGNED_LYRICS = "/gen/{}/aligned_lyrics/v2";
constexpr std::string_view CONVERT_WAV    = "/gen/{}/convert_wav/";
constexpr std::string_view WAV_FILE       = "/gen/{}/wav_file/";

// ── B-Side / Orchestrator ──────────────────────────────────
constexpr std::string_view ORCHESTRATOR_CHAT    = "/api/v1/orchestrator/chat";
constexpr std::string_view ORCHESTRATOR_HISTORY = "/api/v1/orchestrator/history";

// ── CDN ────────────────────────────────────────────────────
constexpr std::string_view CDN_CLIP_MP3   = "/{}.mp3";

} // namespace vc::suno::endpoints
