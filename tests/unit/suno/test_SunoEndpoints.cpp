#include <QtTest>
#include "suno/SunoEndpoints.hpp"
#include <cstdlib>

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

using namespace vc::suno::endpoints;

namespace {

struct NamedEndpoint {
    const char* name;
    std::string_view value;
};

// Keep this table explicit: adding an endpoint without classifying it here
// makes the URL-shape and tier regression tests drift out of coverage.
constexpr std::array kAllEndpoints{
        NamedEndpoint{"API_BASE", API_BASE},
        NamedEndpoint{"MODAL_BASE", MODAL_BASE},
        NamedEndpoint{"CDN_BASE", CDN_BASE},
        NamedEndpoint{"CDN_CLOUDFRONT_BASE", CDN_CLOUDFRONT_BASE},
        NamedEndpoint{"WEB_BASE", WEB_BASE},
        NamedEndpoint{"GENERATE", GENERATE},
        NamedEndpoint{"CAPTCHA_CHECK", CAPTCHA_CHECK},
        NamedEndpoint{"COWRITE_LYRICS", COWRITE_LYRICS},
        NamedEndpoint{"ALIGNED_LYRICS", ALIGNED_LYRICS},
        NamedEndpoint{"DOWNBEATS_STREAMING", DOWNBEATS_STREAMING},
        NamedEndpoint{"WAVEFORM_AGGREGATES", WAVEFORM_AGGREGATES},
        NamedEndpoint{"CONVERT_WAV", CONVERT_WAV},
        NamedEndpoint{"WAV_FILE", WAV_FILE},
        NamedEndpoint{"LYRICS_PROJECTS", LYRICS_PROJECTS},
        NamedEndpoint{"LYRICS_PROJECTS_FLUSH", LYRICS_PROJECTS_FLUSH},
        NamedEndpoint{"PROMPTS_V2", PROMPTS_V2},
        NamedEndpoint{"PROMPTS_SUGGESTIONS", PROMPTS_SUGGESTIONS},
        NamedEndpoint{"PROMPTS_UPSAMPLE", PROMPTS_UPSAMPLE},
        NamedEndpoint{"LYRICISTS", LYRICISTS},
        NamedEndpoint{"LIBRARY_FEED", LIBRARY_FEED},
        NamedEndpoint{"UNIFIED_FEED", UNIFIED_FEED},
        NamedEndpoint{"UNIFIED_HOMEPAGE", UNIFIED_HOMEPAGE},
        NamedEndpoint{"UNIFIED_EXPLORE", UNIFIED_EXPLORE},
        NamedEndpoint{"SOCIAL_FOLLOWING_FEED", SOCIAL_FOLLOWING_FEED},
        NamedEndpoint{"NOTIFICATION_V2_READ", NOTIFICATION_V2_READ},
        NamedEndpoint{"UPLOADS_AUDIO", UPLOADS_AUDIO},
        NamedEndpoint{"UPLOADS_AUDIO_UPLOAD_FINISH", UPLOADS_AUDIO_UPLOAD_FINISH},
        NamedEndpoint{"SESSION", SESSION},
        NamedEndpoint{"SESSION_CATALOG", SESSION_CATALOG},
        NamedEndpoint{"PLAYLIST_ME", PLAYLIST_ME},
        NamedEndpoint{"BILLING_INFO", BILLING_INFO},
        NamedEndpoint{"BILLING_ELIGIBLE_DISCOUNTS", BILLING_ELIGIBLE_DISCOUNTS},
        NamedEndpoint{"BILLING_USAGE_PLANS", BILLING_USAGE_PLANS},
        NamedEndpoint{"BILLING_USAGE_PLAN_COMPARISON", BILLING_USAGE_PLAN_COMPARISON},
        NamedEndpoint{"BILLING_USAGE_PLAN_FAQ", BILLING_USAGE_PLAN_FAQ},
        NamedEndpoint{"BILLING_USAGE_PLAN_DESCRIPTIONS", BILLING_USAGE_PLAN_DESCRIPTIONS},
        NamedEndpoint{"BILLING_AUTO_RELOAD_NUDGE_CHECK", BILLING_AUTO_RELOAD_NUDGE_CHECK},
        NamedEndpoint{"BILLING_CONVERSION_TRACKING", BILLING_CONVERSION_TRACKING},
        NamedEndpoint{"USER_CONFIG", USER_CONFIG},
        NamedEndpoint{"USER_TOS_ACCEPTANCE", USER_TOS_ACCEPTANCE},
        NamedEndpoint{"USER_GET_SESSION_ID", USER_GET_SESSION_ID},
        NamedEndpoint{"USER_METADATA", USER_METADATA},
        NamedEndpoint{"PROFILES_INFO", PROFILES_INFO},
        NamedEndpoint{"PROFILES_PINNED_CLIPS", PROFILES_PINNED_CLIPS},
        NamedEndpoint{"PROJECT_DEFAULT", PROJECT_DEFAULT},
        NamedEndpoint{"PROJECT_ME", PROJECT_ME},
        NamedEndpoint{"PROJECT_BY_ID", PROJECT_BY_ID},
        NamedEndpoint{"PROJECT_PINNED_CLIPS", PROJECT_PINNED_CLIPS},
        NamedEndpoint{"CLIP_ATTRIBUTION", CLIP_ATTRIBUTION},
        NamedEndpoint{"CLIP_PARENT", CLIP_PARENT},
        NamedEndpoint{"CLIP_REMIXES", CLIP_REMIXES},
        NamedEndpoint{"CLIP_REMIXES_COUNT", CLIP_REMIXES_COUNT},
        NamedEndpoint{"CLIP_GET_SIMILAR", CLIP_GET_SIMILAR},
        NamedEndpoint{"CLIP_GET_SONGS_BY_IDS", CLIP_GET_SONGS_BY_IDS},
        NamedEndpoint{"GEN_COMMENTS", GEN_COMMENTS},
        NamedEndpoint{"VIDEO_GENERATE_STATUS", VIDEO_GENERATE_STATUS},
        NamedEndpoint{"VIDEO_GEN_PENDING_BATCHES", VIDEO_GEN_PENDING_BATCHES},
        NamedEndpoint{"MANGO_RIGHTS", MANGO_RIGHTS},
        NamedEndpoint{"NOTIFICATION_V2", NOTIFICATION_V2},
        NamedEndpoint{"NOTIFICATION_BADGE_COUNT", NOTIFICATION_BADGE_COUNT},
        NamedEndpoint{"CUSTOM_MODEL_PENDING", CUSTOM_MODEL_PENDING},
        NamedEndpoint{"PERSONALIZATION_MEMORY", PERSONALIZATION_MEMORY},
        NamedEndpoint{"PERSONALIZATION_SETTINGS", PERSONALIZATION_SETTINGS},
        NamedEndpoint{"PROMPTS", PROMPTS},
        NamedEndpoint{"CONTESTS", CONTESTS},
        NamedEndpoint{"MUSIC_PLAYER_PLAYBAR_STATE", MUSIC_PLAYER_PLAYBAR_STATE},
        NamedEndpoint{"MODALS", MODALS},
        NamedEndpoint{"STATSIG_EXPERIMENT", STATSIG_EXPERIMENT},
        NamedEndpoint{"CMS_NUDGES_PUBLISH", CMS_NUDGES_PUBLISH},
        NamedEndpoint{"CMS_NUDGES_SHARE", CMS_NUDGES_SHARE},
        NamedEndpoint{"SHARE_STATS", SHARE_STATS},
        NamedEndpoint{"REALTIME_DISCOVER", REALTIME_DISCOVER},
        NamedEndpoint{"ORCHESTRATOR_CHAT", ORCHESTRATOR_CHAT},
        NamedEndpoint{"ORCHESTRATOR_HISTORY", ORCHESTRATOR_HISTORY},
        NamedEndpoint{"CDN_CLIP_MP3", CDN_CLIP_MP3},
        NamedEndpoint{"CDN_CLIP_M4A", CDN_CLIP_M4A},
};

constexpr std::array kStudioApiPaths{
        GENERATE, CAPTCHA_CHECK, COWRITE_LYRICS,
        ALIGNED_LYRICS, DOWNBEATS_STREAMING, WAVEFORM_AGGREGATES,
        CONVERT_WAV, WAV_FILE,
        LYRICS_PROJECTS, LYRICS_PROJECTS_FLUSH, PROMPTS_V2,
        PROMPTS_SUGGESTIONS, PROMPTS_UPSAMPLE, LYRICISTS,
        LIBRARY_FEED, UNIFIED_FEED, UNIFIED_HOMEPAGE, UNIFIED_EXPLORE,
        SOCIAL_FOLLOWING_FEED, NOTIFICATION_V2_READ, UPLOADS_AUDIO,
        UPLOADS_AUDIO_UPLOAD_FINISH, SESSION,
        SESSION_CATALOG, PLAYLIST_ME,
        BILLING_INFO, BILLING_ELIGIBLE_DISCOUNTS, BILLING_USAGE_PLANS,
        BILLING_USAGE_PLAN_COMPARISON, BILLING_USAGE_PLAN_FAQ,
        BILLING_USAGE_PLAN_DESCRIPTIONS, BILLING_AUTO_RELOAD_NUDGE_CHECK,
        BILLING_CONVERSION_TRACKING,
        USER_CONFIG, USER_TOS_ACCEPTANCE, USER_GET_SESSION_ID, USER_METADATA,
        PROFILES_INFO, PROFILES_PINNED_CLIPS,
        PROJECT_DEFAULT, PROJECT_ME, PROJECT_BY_ID, PROJECT_PINNED_CLIPS,
        CLIP_ATTRIBUTION, CLIP_PARENT, CLIP_REMIXES, CLIP_REMIXES_COUNT,
        CLIP_GET_SIMILAR, CLIP_GET_SONGS_BY_IDS, GEN_COMMENTS,
        VIDEO_GENERATE_STATUS, VIDEO_GEN_PENDING_BATCHES, MANGO_RIGHTS,
        NOTIFICATION_V2, NOTIFICATION_BADGE_COUNT, CUSTOM_MODEL_PENDING,
        PERSONALIZATION_MEMORY, PERSONALIZATION_SETTINGS, PROMPTS, CONTESTS,
        MUSIC_PLAYER_PLAYBAR_STATE, MODALS, STATSIG_EXPERIMENT,
        CMS_NUDGES_PUBLISH, CMS_NUDGES_SHARE, SHARE_STATS, REALTIME_DISCOVER,
};

std::size_t occurrences(std::string_view text, std::string_view needle) {
    std::size_t count = 0;
    std::size_t offset = 0;
    while ((offset = text.find(needle, offset)) != std::string_view::npos) {
        ++count;
        offset += needle.size();
    }
    return count;
}

} // namespace

class TestSunoEndpoints : public QObject {
    Q_OBJECT

private slots:
    void knownWorkingPathsStayStable() {
        QCOMPARE(std::string(LIBRARY_FEED), std::string("/feed/v3"));
        QCOMPARE(std::string(GENERATE), std::string("/generate/v2-web/"));
        QVERIFY(std::string(ALIGNED_LYRICS).ends_with('/'));
        QVERIFY(std::string(API_BASE).ends_with("/api"));
        QCOMPARE(std::string(AUDIO_UPLOAD_STORAGE_HOST),
                 std::string("suno-uploads.s3.amazonaws.com"));
    }

    void studioApiPathsDoNotDuplicateApiPrefix() {
        for (const auto endpoint : kStudioApiPaths) {
            QVERIFY2(endpoint.starts_with('/'),
                     qPrintable(QStringLiteral("studio-api path is not root-relative: %1")
                                        .arg(QString::fromUtf8(
                                                endpoint.data(),
                                                static_cast<qsizetype>(endpoint.size())))));
            QVERIFY2(!endpoint.starts_with("/api"),
                     qPrintable(QStringLiteral("studio-api path starts with /api: %1")
                                        .arg(QString::fromUtf8(
                                                endpoint.data(),
                                                static_cast<qsizetype>(endpoint.size())))));
        }
    }

    void everyConstantHasAnAllowedUrlShape() {
        for (const auto& [name, value] : kAllEndpoints) {
            const bool allowed = value.empty() || value.starts_with("https://")
                                || value.starts_with('/');
            QVERIFY2(allowed,
                     qPrintable(QStringLiteral("%1 has invalid URL shape: %2")
                                        .arg(QString::fromLatin1(name),
                                             QString::fromUtf8(value.data(),
                                                               static_cast<qsizetype>(value.size())))));
        }
    }

    void feedCompositionContainsOneApiSegment() {
        const std::string url = std::string(API_BASE) + std::string(LIBRARY_FEED);
        QCOMPARE(occurrences(url, "/api"), 1);
        QCOMPARE(QString::fromStdString(url),
                 QStringLiteral("https://studio-api-prod.suno.com/api/feed/v3"));
    }
};

#include "test_SunoEndpoints.moc"

int runTestSunoEndpoints(int argc, char** argv) {
    static const int result = [&] {
        TestSunoEndpoints tc;
        const int testResult = QTest::qExec(&tc, argc, argv);
        if (testResult != 0) {
            std::abort();
        }
        return testResult;
    }();
    return result;
}

// The aggregate unit-test main predates this lane and is outside the allowed
// write scope. Run this suite during static initialization so the requested
// endpoint guard still executes in the existing unit_tests binary; abort on
// failure so it cannot be masked by the aggregate target's return status.
namespace {
const int kSunoEndpointsTestResult = [] {
    char executable[] = "unit_tests";
    char* argv[] = {executable};
    return runTestSunoEndpoints(1, argv);
}();
} // namespace
