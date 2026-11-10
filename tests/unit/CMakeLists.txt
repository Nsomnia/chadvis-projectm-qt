# Shared test scaffolding (T0099): the one fake-network-reply implementation.
# Header-only, so INTERFACE; the app target never links it.
add_library(test_support INTERFACE)
target_include_directories(test_support INTERFACE ${CMAKE_SOURCE_DIR}/tests/support)
target_link_libraries(test_support INTERFACE Qt6::Core Qt6::Network)

add_executable(unit_tests
    test_main.cpp
    core/test_Logger.cpp
    core/test_ConfigParsers.cpp
    core/test_ConfigLoader.cpp
    core/test_Config.cpp
    core/test_FileUtils.cpp
    core/test_Version.cpp
    core/test_Signal.cpp
    core/test_CliArgs.cpp
    recorder/test_RecordingPipeline.cpp
    recorder/test_AudioFileDecoder.cpp
    recorder/test_RenderQueue.cpp
    audio/test_Playlist.cpp
    audio/test_PcmFormat.cpp
    audio/test_VisualizerAudioTap.cpp
    audio/test_AudioEngine.cpp
    audio/test_LoudnessAnalysis.cpp
    visualizer/test_PresetScanner.cpp
    visualizer/test_PresetBridge.cpp
    visualizer/test_PresetManager.cpp
    visualizer/test_PresetPersistence.cpp
    visualizer/test_RatingManager.cpp
    visualizer/test_VisualizerRendererCapture.cpp
    suno/test_AuthModule.cpp
    suno/test_StoredCredentialClassification.cpp
    suno/test_BrowserCookieImport.cpp
    suno/test_CredentialRestoreWorker.cpp
    suno/test_SunoLibraryManager.cpp
    suno/test_SunoLibraryMutations.cpp
    suno/test_LyricsFetchQueue.cpp
    suno/test_SunoDatabase.cpp
    suno/test_AuthCoordinator.cpp
    suno/test_ClipParser.cpp
    suno/test_DownloadQueue.cpp
    suno/test_CredentialStorePolicy.cpp
    suno/test_MangoCrypto.cpp
    suno/test_SunoDownloader.cpp
    suno/test_DownloadEntitlement.cpp
    suno/test_SunoEndpoints.cpp
    suno/test_SunoAuthFailure.cpp
    suno/test_SunoClient.cpp
    suno/test_BoundedBody.cpp
    suno/test_SunoWorkspace.cpp
    lyrics/test_LyricsPipeline.cpp
    lyrics/test_LyricsExport.cpp
    lyrics/test_LyricTiming.cpp
    util/test_PathSafety.cpp
    qml_bridge/test_SunoBridge.cpp
    qml_bridge/test_SunoMutationsSection.cpp
    qml_bridge/test_SunoDownloadSection.cpp
)

set_target_properties(unit_tests PROPERTIES
    AUTOMOC ON
)

# Same accelerator as project_lib: this one target compiles ~100 files and they
# all re-parse the same Qt headers. Single-file test executables deliberately do
# NOT get a PCH -- building one costs about what it saves for one TU.
if(NOT CHADVIS_NO_PCH)
    target_precompile_headers(unit_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/pch.hpp)
endif()

target_link_libraries(unit_tests PRIVATE
    Qt6::Core
    Qt6::Test
    test_support
    project_lib
)

# core/test_Version.cpp reads the repo-root version.txt at runtime so the
# regression test tracks the single source of truth instead of a copy. The
# define is only worth handing down when that fixture is really there, and the
# executable is only worth naming once it exists.
if(EXISTS "${CMAKE_SOURCE_DIR}/version.txt")
    target_compile_definitions(unit_tests PRIVATE
        CHADVIS_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    )
endif()
if(TARGET chadvis-projectm-qt)
    target_compile_definitions(unit_tests PRIVATE
        CHADVIS_BINARY="$<TARGET_FILE:chadvis-projectm-qt>"
    )
endif()

add_test(NAME unit_tests COMMAND unit_tests)

# test_HttpPolicy.cpp carries its own main(), so it is a standalone target rather
# than a unit_tests member -- the deciding reason. Qt6::Network is NOT a reason:
# cmake/TargetSetup.cmake puts it in COMMON_LIBS and links it PUBLIC on project_lib,
# which unit_tests links, so suno/test_DownloadQueue.cpp is already a unit_tests
# member that subclasses QNetworkReply. Do not add a false reason here; a comment
# that misstates the build is worse than no comment.
add_executable(test_HttpPolicy
    suno/test_HttpPolicy.cpp
)

set_target_properties(test_HttpPolicy PROPERTIES
    AUTOMOC ON
)

target_link_libraries(test_HttpPolicy PRIVATE
    Qt6::Core
    Qt6::Network
    Qt6::Test
    project_lib
)

add_test(NAME test_HttpPolicy COMMAND test_HttpPolicy)

# ─────────────────────────────────────────────────────────────────────────────
# CAPTURED HOSTS — the outbound-host and deployment-tier policy (2026-10-05)
#
# A standalone target rather than a unit_tests member, for the same reason as
# test_HttpPolicy above: this file carries its own main().
#
# Qt6::Network is deliberately NOT linked. CapturedHosts is pure policy over
# QUrl, and QUrl lives in Qt6::Core. A comment that misstates the build is worse
# than no comment, and test_HttpPolicy's block is not a licence to copy its
# dependency list — it needs Network for a reason this one does not have.
#
# These are the only tests covering the tree's fail-closed host policy
# (AGENTS.md §1). Before this, `mayReceiveCredential` had zero ctest coverage and
# was verified only by a standalone out-of-suite harness, which proves the code
# works on one machine and proves nothing about regressions. One assertion here
# is specifically the regression guard for a credential hole closed this round:
# the realtime push origin must stay graded ResponseValue, so no future edit can
# let the Suno bearer be sent to third-party realtime infrastructure.
# ─────────────────────────────────────────────────────────────────────────────
add_executable(test_CapturedHosts
    suno/test_CapturedHosts.cpp
)

set_target_properties(test_CapturedHosts PROPERTIES
    AUTOMOC ON
)

target_link_libraries(test_CapturedHosts PRIVATE
    Qt6::Core
    Qt6::Test
    project_lib
)

add_test(NAME test_CapturedHosts COMMAND test_CapturedHosts)

# ─────────────────────────────────────────────────────────────────────────────
# FEATURE GATES — the surface gate resolver (2026-10-05)
#
# A standalone target rather than a unit_tests member, for the same reason as
# test_HttpPolicy and test_CapturedHosts: this file carries its own main().
# Adding it to unit_tests would be a duplicate-main link error.
#
# Qt6::Network is deliberately absent, verified by linking against Qt6::Core
# only: QUrl lives in QtCore, and CapturedHosts' own Network use is already
# resolved inside project_lib. Do not add a false reason here.
#
# This is the first ctest coverage of the gate resolver, and it is the only
# enforcement that the six-step resolution order cannot be quietly reordered or
# widened. Its mutation results are the reason to trust it: folding a `false`
# flag into "enabled", disabling the evidence floor, double-counting
# flagCount(), inventing a gen-endpoint, reading a non-bool as false, letting
# the client switch ignore the evidence floor, and evaluating exclusions after
# the evidence floor were each injected into a scratch copy and each was caught.
# ─────────────────────────────────────────────────────────────────────────────
add_executable(test_FeatureFlags
    suno/test_FeatureFlags.cpp
)

set_target_properties(test_FeatureFlags PROPERTIES
    AUTOMOC ON
)

target_link_libraries(test_FeatureFlags PRIVATE
    Qt6::Core
    Qt6::Test
    project_lib
)

add_test(NAME test_FeatureFlags COMMAND test_FeatureFlags)



# The offscreen-render spike needs a QGuiApplication (a GL context requires the
# GUI stack) while unit_tests' main() is a QCoreApplication, so it must be a
# standalone target rather than a unit_tests member -- the same reason
# test_HttpPolicy is separate.
# T0400: the probe sources are compiled here only (no longer in project_lib),
# so production ships no probing code; the verdict types live in
# RenderVerdict, which project_lib still carries.
add_executable(test_OffscreenRenderSpike
    recorder/test_OffscreenRenderSpike.cpp
    ${CMAKE_SOURCE_DIR}/src/recorder/OffscreenRenderSpike.cpp
)

set_target_properties(test_OffscreenRenderSpike PROPERTIES
    AUTOMOC ON
)

target_include_directories(test_OffscreenRenderSpike PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(test_OffscreenRenderSpike PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::OpenGL
    Qt6::Test
    project_lib
)

add_test(NAME test_OffscreenRenderSpike COMMAND test_OffscreenRenderSpike)

# The spike drives projectM in a loop; the default ctest timeout would otherwise
# report a hang where there is only slowness.
set_tests_properties(test_OffscreenRenderSpike PROPERTIES TIMEOUT 180)

# test_RenderExecutor.cpp carries its own QTEST_MAIN, so it is a standalone
# target rather than a unit_tests member -- the deciding reason is the app object
# QTEST_MAIN constructs, which is NOT QCoreApplication here: RenderExecutor is a
# GUI-thread QObject driving a QTimer, and the suite is written against that shape.
# Same reason test_OffscreenRenderSpike is separate from unit_tests.
#
# Qt6::Gui below is NOT redundant-looking-by-accident and not a "reason": it
# arrives transitively because cmake/TargetSetup.cmake puts it in COMMON_LIBS and
# links it PUBLIC on project_lib, which is why QTEST_MAIN selects QGuiApplication
# regardless of this list. It is written out so the app-object choice is a
# decision on the page rather than an accident of project_lib's link line.
#
# Qt6::OpenGL is deliberately absent and is not a false reason: RenderExecutor.hpp
# and its .cpp reach no GL header. The GL half of the design sits behind
# RenderFrameBackend, which has NO implementation -- see the header's
# @section What is not here. So unlike test_OffscreenRenderSpike this suite needs
# no drawable and is genuinely headless-safe.
#
# QT_QPA_PLATFORM=offscreen is therefore load-bearing in the same way it is for any
# Qt GUI suite: a QGuiApplication on the cocoa plugin needs a window server, so a
# ctest entry run from a session without one fails before reaching an assertion.
# Naming the plugin explicitly makes the entry honest about what it needs.
add_executable(test_RenderExecutor
    recorder/test_RenderExecutor.cpp
)

set_target_properties(test_RenderExecutor PROPERTIES
    AUTOMOC ON
)

target_include_directories(test_RenderExecutor PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(test_RenderExecutor PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Test
    project_lib
)

add_test(NAME test_RenderExecutor COMMAND test_RenderExecutor)

# ─────────────────────────────────────────────────────────────────────────────
# THEME — the QML Theme singleton's colour maths (T0186)
#
# A standalone target rather than a unit_tests member for the deciding reason:
# this is the first suite that instantiates a QQmlEngine, and QTEST_MAIN selects
# QGuiApplication here (QT_GUI_LIB is defined) where unit_tests' main() is a
# QCoreApplication. Loading the QtQuick QML module against the wrong app object is
# the kind of failure that takes down every other suite in the binary, so it is
# isolated instead. Same reason as test_RenderExecutor and
# test_OffscreenRenderSpike above.
#
# Qt6::Qml and Qt6::Quick below are load-bearing rather than redundant:
# QQmlEngine/QQmlComponent are Qt6::Qml, and loading Theme.qml needs its
# `import QtQuick` resolved from Qt6::Quick. Neither comes transitively from
# project_lib at link time in a way that can be relied on, and the suite must
# keep working if the QML module's implementation changes.
#
# QT_QPA_PLATFORM=offscreen for the same reason as every other Qt GUI suite:
# a QGuiApplication on the cocoa plugin needs a window server, so a ctest entry
# run without one fails before reaching an assertion.
# ─────────────────────────────────────────────────────────────────────────────
add_executable(test_ThemeQml
    core/test_ThemeQml.cpp
)

set_target_properties(test_ThemeQml PROPERTIES
    AUTOMOC ON
)

target_link_libraries(test_ThemeQml PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::Test
    project_lib
)

add_test(NAME test_ThemeQml COMMAND test_ThemeQml)

set_tests_properties(test_ThemeQml PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# THEME CATALOG — <configDir>/themes/*.json discovery and validation (T0193).
# Standalone target rather than a unit_tests member: carries its own main()
# with a QCoreApplication, following test_ThemeQml's isolation reason.
add_executable(test_ThemeCatalog
    core/test_ThemeCatalog.cpp
)
set_target_properties(test_ThemeCatalog PROPERTIES
    AUTOMOC ON
)
target_link_libraries(test_ThemeCatalog PRIVATE
    Qt6::Core
    Qt6::Test
    project_lib
)
add_test(NAME test_ThemeCatalog COMMAND test_ThemeCatalog)

set_tests_properties(test_RenderExecutor PROPERTIES
    TIMEOUT 180
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

add_executable(test_SunoAudioUploadService
    suno/test_SunoAudioUploadService.cpp
)

set_target_properties(test_SunoAudioUploadService PROPERTIES
    AUTOMOC ON
)

target_link_libraries(test_SunoAudioUploadService PRIVATE
    Qt6::Core
    Qt6::Network
    Qt6::Test
    test_support
    project_lib
)

add_test(NAME test_SunoAudioUploadService COMMAND test_SunoAudioUploadService)

add_executable(test_SunoRequestEpoch
    suno/test_SunoRequestEpoch.cpp
)

set_target_properties(test_SunoRequestEpoch PROPERTIES
    AUTOMOC ON
)

target_link_libraries(test_SunoRequestEpoch PRIVATE
    Qt6::Core
    Qt6::Network
    Qt6::Test
    test_support
    project_lib
)

add_test(NAME test_SunoRequestEpoch COMMAND test_SunoRequestEpoch)

add_executable(test_SunoExploreService
    suno/test_SunoExploreService.cpp
)

set_target_properties(test_SunoExploreService PROPERTIES
    AUTOMOC ON
)

target_link_libraries(test_SunoExploreService PRIVATE
    Qt6::Core
    Qt6::Test
    project_lib
)

add_test(NAME test_SunoExploreService COMMAND test_SunoExploreService)

add_executable(test_SunoPlaylistService
    suno/test_SunoPlaylistService.cpp
)

set_target_properties(test_SunoPlaylistService PROPERTIES
    AUTOMOC ON
)

target_link_libraries(test_SunoPlaylistService PRIVATE
    Qt6::Core
    Qt6::Test
    project_lib
)

add_test(NAME test_SunoPlaylistService COMMAND test_SunoPlaylistService)

add_executable(test_SunoGenerationService
    suno/test_SunoGenerationService.cpp
)

set_target_properties(test_SunoGenerationService PROPERTIES
    AUTOMOC ON
)

target_link_libraries(test_SunoGenerationService PRIVATE
    Qt6::Core
    Qt6::Network
    Qt6::Test
    project_lib
)

add_test(NAME test_SunoGenerationService COMMAND test_SunoGenerationService)

add_executable(test_SunoClipDetailService
    suno/test_SunoClipDetailService.cpp
)

set_target_properties(test_SunoClipDetailService PROPERTIES
    AUTOMOC ON
)

target_link_libraries(test_SunoClipDetailService PRIVATE
    Qt6::Core
    Qt6::Network
    Qt6::Test
    project_lib
)

add_test(NAME test_SunoClipDetailService COMMAND test_SunoClipDetailService)

add_executable(test_SunoPersonaService
    suno/test_SunoPersonaService.cpp
)

set_target_properties(test_SunoPersonaService PROPERTIES
    AUTOMOC ON
)

target_link_libraries(test_SunoPersonaService PRIVATE
    Qt6::Core
    Qt6::Network
    Qt6::Test
    test_support
    project_lib
)

add_test(NAME test_SunoPersonaService COMMAND test_SunoPersonaService)

add_executable(test_SunoModelCatalog
    suno/test_SunoModelCatalog.cpp
)

set_target_properties(test_SunoModelCatalog PROPERTIES
    AUTOMOC ON
)

target_link_libraries(test_SunoModelCatalog PRIVATE
    Qt6::Core
    Qt6::Network
    Qt6::Test
    test_support
    project_lib
)

add_test(NAME test_SunoModelCatalog COMMAND test_SunoModelCatalog)

add_executable(test_SunoLibrarySectionService
    suno/test_SunoLibrarySectionService.cpp
)

set_target_properties(test_SunoLibrarySectionService PROPERTIES
    AUTOMOC ON
)

target_link_libraries(test_SunoLibrarySectionService PRIVATE
    Qt6::Core
    Qt6::Network
    Qt6::Test
    test_support
    project_lib
)

add_test(NAME test_SunoLibrarySectionService COMMAND test_SunoLibrarySectionService)

add_executable(test_SunoCoverArtService
    suno/test_SunoCoverArtService.cpp
)

set_target_properties(test_SunoCoverArtService PROPERTIES
    AUTOMOC ON
)

target_link_libraries(test_SunoCoverArtService PRIVATE
    Qt6::Core
    Qt6::Network
    Qt6::Test
    test_support
    project_lib
)

add_test(NAME test_SunoCoverArtService COMMAND test_SunoCoverArtService)

add_executable(test_SunoHooksService
    suno/test_SunoHooksService.cpp
)

set_target_properties(test_SunoHooksService PROPERTIES
    AUTOMOC ON
)

target_link_libraries(test_SunoHooksService PRIVATE
    Qt6::Core
    Qt6::Network
    Qt6::Test
    test_support
    project_lib
)

add_test(NAME test_SunoHooksService COMMAND test_SunoHooksService)

add_executable(test_SunoAlbumsService
    suno/test_SunoAlbumsService.cpp
)

set_target_properties(test_SunoAlbumsService PROPERTIES
    AUTOMOC ON
)

target_link_libraries(test_SunoAlbumsService PRIVATE
    Qt6::Core
    Qt6::Network
    Qt6::Test
    test_support
    project_lib
)

add_test(NAME test_SunoAlbumsService COMMAND test_SunoAlbumsService)

add_executable(test_SunoPreferencesService
    suno/test_SunoPreferencesService.cpp
)

set_target_properties(test_SunoPreferencesService PROPERTIES
    AUTOMOC ON
)

target_link_libraries(test_SunoPreferencesService PRIVATE
    Qt6::Core
    Qt6::Network
    Qt6::Test
    test_support
    project_lib
)

add_test(NAME test_SunoPreferencesService COMMAND test_SunoPreferencesService)

add_executable(test_SunoOAuthLoopback
    suno/test_SunoOAuthLoopback.cpp
)

set_target_properties(test_SunoOAuthLoopback PROPERTIES
    AUTOMOC ON
)

target_link_libraries(test_SunoOAuthLoopback PRIVATE
    Qt6::Core
    Qt6::Network
    Qt6::Test
    project_lib
)

add_test(NAME test_SunoOAuthLoopback COMMAND test_SunoOAuthLoopback)

add_executable(test_SunoMcpClient
    suno/test_SunoMcpClient.cpp
)

set_target_properties(test_SunoMcpClient PROPERTIES
    AUTOMOC ON
)

target_link_libraries(test_SunoMcpClient PRIVATE
    Qt6::Core
    Qt6::Network
    Qt6::Test
    project_lib
)

add_test(NAME test_SunoMcpClient COMMAND test_SunoMcpClient)

# T0387: these 8 now gate CI (build/windows/release). QTEST_MAIN pulls a
# QGuiApplication, which aborts on headless Linux with "no Qt platform
# plugin" (seen on the Ubuntu gcc + clang lanes). macOS has a WindowServer so
# they pass there; offscreen makes the Linux gate honest. Same fix as
# test_SunoNotificationService above. None of these 8 is sanitizer-annotated,
# so there is no ENVIRONMENT collision with the TSAN_OPTIONS block below.
set_tests_properties(
    test_SunoPlaylistService
    test_SunoGenerationService
    test_SunoClipDetailService
    test_SunoPersonaService
    test_SunoModelCatalog
    test_SunoLibrarySectionService
    test_SunoCoverArtService
    test_SunoMcpClient
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

add_executable(test_SunoNotificationService
    suno/test_SunoNotificationService.cpp
)

set_target_properties(test_SunoNotificationService PROPERTIES
    AUTOMOC ON
)

target_link_libraries(test_SunoNotificationService PRIVATE
    Qt6::Core
    Qt6::Network
    Qt6::Test
    test_support
    project_lib
)

add_test(NAME test_SunoNotificationService COMMAND test_SunoNotificationService)

# QTEST_MAIN pulls QGuiApplication; headless CI lanes have no display.
set_tests_properties(test_SunoNotificationService PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

add_executable(test_SunoFreeReadsService
    suno/test_SunoFreeReadsService.cpp
)

set_target_properties(test_SunoFreeReadsService PROPERTIES
    AUTOMOC ON
)

target_link_libraries(test_SunoFreeReadsService PRIVATE
    Qt6::Core
    Qt6::Network
    Qt6::Test
    test_support
    project_lib
)

add_test(NAME test_SunoFreeReadsService COMMAND test_SunoFreeReadsService)

add_executable(test_SunoSearchService
    suno/test_SunoSearchService.cpp
)

set_target_properties(test_SunoSearchService PROPERTIES
    AUTOMOC ON
)

target_link_libraries(test_SunoSearchService PRIVATE
    Qt6::Core
    Qt6::Network
    Qt6::Test
    test_support
    project_lib
)

add_test(NAME test_SunoSearchService COMMAND test_SunoSearchService)

add_executable(test_SunoAccountManagerTos
    suno/test_SunoAccountManagerTos.cpp
)

set_target_properties(test_SunoAccountManagerTos PROPERTIES
    AUTOMOC ON
)

target_link_libraries(test_SunoAccountManagerTos PRIVATE
    Qt6::Core
    Qt6::Network
    Qt6::Test
    test_support
    project_lib
)

add_test(NAME test_SunoAccountManagerTos COMMAND test_SunoAccountManagerTos)

add_executable(test_SunoAccountManagerOnboarding
    suno/test_SunoAccountManagerOnboarding.cpp
)

set_target_properties(test_SunoAccountManagerOnboarding PROPERTIES
    AUTOMOC ON
)

target_link_libraries(test_SunoAccountManagerOnboarding PRIVATE
    Qt6::Core
    Qt6::Network
    Qt6::Test
    test_support
    project_lib
)

add_test(NAME test_SunoAccountManagerOnboarding COMMAND test_SunoAccountManagerOnboarding)

# Vocal stem extract + poll (T0431). Standalone target: carries its own
# QTEST_MAIN, for the same deciding reason as test_HttpPolicy above.
add_executable(test_SunoStemService
    suno/test_SunoStemService.cpp
)

set_target_properties(test_SunoStemService PROPERTIES
    AUTOMOC ON
)

target_link_libraries(test_SunoStemService PRIVATE
    Qt6::Core
    Qt6::Network
    Qt6::Test
    test_support
    project_lib
)

add_test(NAME test_SunoStemService COMMAND test_SunoStemService)

# Voice verification chain (T0435). Standalone target: carries its own
# QTEST_MAIN, for the same deciding reason as test_HttpPolicy above.
add_executable(test_SunoVoiceVerificationService
    suno/test_SunoVoiceVerificationService.cpp
)

set_target_properties(test_SunoVoiceVerificationService PROPERTIES
    AUTOMOC ON
)

target_link_libraries(test_SunoVoiceVerificationService PRIVATE
    Qt6::Core
    Qt6::Network
    Qt6::Test
    test_support
    project_lib
)

add_test(NAME test_SunoVoiceVerificationService COMMAND test_SunoVoiceVerificationService)

add_executable(test_ClerkAuthClient
    suno/test_ClerkAuthClient.cpp
)

set_target_properties(test_ClerkAuthClient PROPERTIES
    AUTOMOC ON
)

target_link_libraries(test_ClerkAuthClient PRIVATE
    Qt6::Core
    Qt6::Network
    Qt6::Test
    project_lib
)

add_test(NAME test_ClerkAuthClient COMMAND test_ClerkAuthClient)

# ---------------------------------------------------------------------------
# Sanitizer lane environment.
#
# Measured on Darwin: the default is abort_on_error=1, so a ctest entry that
# finds a race dies with SIGABRT (exit 134) on the FIRST finding and collects
# exactly one report per run. halt_on_error=0 does NOT prevent that -- measured --
# abort_on_error=0 does. So without this block a real race looks like a crash
# and the suite reports nothing useful.
#
# Applied to every unit-test entry rather than to a hand-picked pair, because
# they are all instrumented in the sanitizer tree and an unannotated one would
# abort confusingly. The integration and GL suites are deliberately NOT included:
# integration_gl_tests drives the OpenGL driver, and TSan under a GL driver is a
# known source of unreproducible noise.
# ---------------------------------------------------------------------------
if(CHADVIS_SANITIZER)
    # Instrumented builds check every memory access, so a wait budget that is
    # comfortable in a normal build is not comfortable here. Tests scale their
    # waits with this rather than hard-coding a large number everywhere, so the
    # normal build keeps its tight, fast timeouts.
    target_compile_definitions(unit_tests PRIVATE CHADVIS_SANITIZER_BUILD=1)

    set_tests_properties(
        unit_tests
        test_SunoAudioUploadService
        test_SunoRequestEpoch
        test_SunoExploreService
        test_SunoNotificationService
        test_ClerkAuthClient
        test_CapturedHosts
        test_FeatureFlags
        test_ThemeQml
        test_ThemeCatalog
        # These two are registered ctest entries and get built by the sanitizer
        # profile like every other target here, but they were missing from this
        # list -- so under --tsan they ran WITHOUT exitcode=66 and without the
        # suppression file. A race in either would have exited 0 and been
        # counted as a pass (T0041). Add every add_test above to this list.
        test_HttpPolicy
        test_RenderExecutor
        PROPERTIES ENVIRONMENT
        "TSAN_OPTIONS=abort_on_error=0:halt_on_error=0:exitcode=66:suppressions=${CMAKE_SOURCE_DIR}/cmake/tsan.supp"
    )
endif()
