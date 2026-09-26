add_executable(integration_tests
    test_main.cpp
    test_ProjectMFramebuffer.cpp
)

set_target_properties(integration_tests PROPERTIES
    AUTOMOC ON
)

target_include_directories(integration_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

# Qt6::OpenGL is required by the projectM rendering-destination test, which owns
# a QWindow + QOpenGLContext to find out where projectM actually draws.
# QOpenGLFunctions_3_3_Core is generated from those headers.
target_link_libraries(integration_tests PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::OpenGL
    Qt6::Quick
    Qt6::Test
    project_lib
)

# The QML startup test runs headless; the GL tests need a real drawable, and the
# macOS "offscreen" QPA plugin cannot create an OpenGL context at all
# ("This plugin does not support createPlatformOpenGLContext!"). So the GL suite
# is a second ctest entry that runs the platform's native plugin - cocoa on
# macOS, whatever the session has elsewhere - and selects only the GL test
# functions. Under a plugin without GL both suites skip with a stated reason
# instead of passing vacuously.
add_test(NAME integration_tests COMMAND integration_tests)
set_tests_properties(integration_tests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

add_test(NAME integration_gl_tests
    COMMAND integration_tests
        engineDrawsIntoDefaultFramebuffer
        engineIgnoresAnFboBoundAsDrawTarget
        rendererRecordingPathDeliversPixels
        capturedFramesAreFlippedIntoTopDownRows
        recordingAtADifferentResolutionIsScaled
        encodedFileContainsTheVisualizerPicture
        liveWindowCapturesFrames
)
set_tests_properties(integration_gl_tests PROPERTIES
    TIMEOUT 120
)
