enable_testing()
find_package(Qt6 REQUIRED COMPONENTS Test)

# Test binaries are bare executables in the build tree: they are not bundles,
# macdeployqt never touches them, and the bundle's
# @executable_path/../Frameworks has no meaning for them. The root CMakeLists
# sets CMAKE_BUILD_WITH_INSTALL_RPATH ON, so INSTALL_RPATH *is* the build-tree
# rpath -- and with no entry here the binary carried no LC_RPATH at all.
#
# That was invisible until Qt came from qt.io: Homebrew's frameworks have
# absolute install names, so dyld resolved them without an rpath, while qt.io's
# carry @rpath/... and every test dies at launch with
#   Library not loaded: @rpath/QtTest.framework/... Reason: no LC_RPATH's found
# Appended rather than replaced, and harmless where Qt lives in a system
# directory: an extra rpath entry that resolves to the same libraries.
if(QT6_INSTALL_PREFIX)
    list(APPEND CMAKE_INSTALL_RPATH "${QT6_INSTALL_PREFIX}/lib")
endif()

add_subdirectory(unit)
add_subdirectory(integration)
