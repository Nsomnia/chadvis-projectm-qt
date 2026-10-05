add_executable(unit_tests
    test_main.cpp
    core/test_Logger.cpp
    core/test_ConfigParsers.cpp
    core/test_ConfigLoader.cpp
    core/test_FileUtils.cpp
    core/test_Version.cpp
    core/test_Signal.cpp
    recorder/test_RecordingPipeline.cpp
    recorder/test_AudioFileDecoder.cpp
    recorder/test_RenderQueue.cpp
    audio/test_Playlist.cpp
    audio/test_PcmFormat.cpp
    audio/test_LoudnessAnalysis.cpp
    visualizer/test_PresetScanner.cpp
    suno/test_AuthModule.cpp
    suno/test_StoredCredentialClassification.cpp
    suno/test_CredentialRestoreWorker.cpp
    suno/test_SunoLibraryManager.cpp
    suno/test_AuthCoordinator.cpp
    suno/test_LoopbackListener.cpp
    suno/test_OAuthLoginService.cpp
    suno/test_ClipParser.cpp
    suno/test_DownloadQueue.cpp
    suno/test_CredentialStorePolicy.cpp
    suno/test_SunoDownloader.cpp
    suno/test_SunoEndpoints.cpp
    suno/test_BoundedBody.cpp
    suno/test_SunoWorkspace.cpp
    lyrics/test_LyricsPipeline.cpp
    lyrics/test_LyricsExport.cpp
    lyrics/test_LyricTiming.cpp
    util/test_PathSafety.cpp
)

set_target_properties(unit_tests PROPERTIES
    AUTOMOC ON
)

target_link_libraries(unit_tests PRIVATE
    Qt6::Core
    Qt6::Test
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

# The offscreen-render spike needs a QGuiApplication (a GL context requires the
# GUI stack) while unit_tests' main() is a QCoreApplication, so it must be a
# standalone target rather than a unit_tests member -- the same reason
# test_HttpPolicy is separate.
add_executable(test_OffscreenRenderSpike
    recorder/test_OffscreenRenderSpike.cpp
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
    project_lib
)

add_test(NAME test_SunoNotificationService COMMAND test_SunoNotificationService)

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
        PROPERTIES ENVIRONMENT
        "TSAN_OPTIONS=abort_on_error=0:halt_on_error=0:exitcode=66:suppressions=${CMAKE_SOURCE_DIR}/cmake/tsan.supp"
    )
endif()
