#pragma once
// SunoEndpoints.hpp - Centralized Suno API endpoint map
// One source of truth for every URL we hit

#include <string_view>

namespace vc::suno::endpoints {

// ── Base URLs ──────────────────────────────────────────────
constexpr std::string_view API_BASE       = "https://studio-api-prod.suno.com/api";
constexpr std::string_view CLERK_BASE     = "https://clerk.suno.com/v1";
constexpr std::string_view MODAL_BASE     = "https://suno-ai--orpheus-prod-web.modal.run";
constexpr std::string_view CDN_BASE       = "https://cdn1.suno.ai";
constexpr std::string_view WEB_BASE       = "https://suno.com";

// ── Clerk Auth ─────────────────────────────────────────────
constexpr std::string_view CLERK_VERSION  = "5.117.0";
constexpr std::string_view CLERK_CLIENT   = "/client?_is_native=true&_clerk_js_version=";
constexpr std::string_view CLERK_SESSION  = "/client/sessions/";

// ── Studio API ─────────────────────────────────────────────
constexpr std::string_view LIBRARY        = "/feed/v3";
constexpr std::string_view GENERATE       = "/generate/v2-web/";
constexpr std::string_view ALIGNED_LYRICS = "/gen/{}/aligned_lyrics/v2";
constexpr std::string_view CONVERT_WAV    = "/gen/{}/convert_wav/";
constexpr std::string_view WAV_FILE       = "/gen/{}/wav_file/";

// ── B-Side / Orchestrator ──────────────────────────────────
constexpr std::string_view ORCHESTRATOR_CHAT    = "/api/v1/orchestrator/chat";
constexpr std::string_view ORCHESTRATOR_HISTORY = "/api/v1/orchestrator/history";

// ── Web Auth ───────────────────────────────────────────────
constexpr std::string_view SIGN_IN        = "/sign-in";
constexpr std::string_view LOGIN          = "/login";

// ── CDN ────────────────────────────────────────────────────
constexpr std::string_view CDN_CLIP_MP3   = "/{}.mp3";

} // namespace vc::suno::endpoints
