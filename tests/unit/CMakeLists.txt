add_executable(unit_tests
    test_main.cpp
    core/test_Logger.cpp
    core/test_ConfigParsers.cpp
    core/test_FileUtils.cpp
    core/test_Version.cpp
    recorder/test_RecordingPipeline.cpp
    audio/test_Playlist.cpp
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
    suno/test_SunoEndpoints.cpp
    lyrics/test_LyricsPipeline.cpp
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

add_executable(test_AudioAnalyzer
    audio/test_AudioAnalyzer.cpp
)

set_target_properties(test_AudioAnalyzer PROPERTIES
    AUTOMOC ON
)

target_link_libraries(test_AudioAnalyzer PRIVATE
    Qt6::Core
    Qt6::Test
    project_lib
)

add_test(NAME test_AudioAnalyzer COMMAND test_AudioAnalyzer)
