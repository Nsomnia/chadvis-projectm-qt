# Install.cmake - install rules and CPack configuration.
#
# The macOS branch is the one that matters for shipping and it is a genuinely
# different shape from the other two platforms: with MACOSX_BUNDLE set in
# cmake/TargetSetup.cmake, the executable IS the bundle, and install(TARGETS)
# exposes it under the BUNDLE artifact kind rather than RUNTIME (there is no
# MACOSX_BUNDLE artifact kind -- naming one is a configure-time error, not a
# silent no-op, and it is an easy one to write).
#
# macdeployqt and the codesign step have already run by this point: both are
# POST_BUILD steps on the target, and install(TARGETS) copies the build-tree
# bundle verbatim, including the Contents/Frameworks and Contents/PlugIns trees
# they produced. There is no second packaging pass and nothing to re-run.
if(APPLE)
    install(TARGETS chadvis-projectm-qt BUNDLE DESTINATION .)
else()
    install(TARGETS chadvis-projectm-qt RUNTIME DESTINATION bin)
endif()

# The shipped config template. Only Linux reads it at runtime -- src/core/
# ConfigLoader.cpp probes /usr/share/chadvis-projectm-qt/config/default.toml under
# #ifdef __linux__ only, and generates its own config everywhere else -- so on
# macOS this lands in the .dmg as reference material rather than as something the
# application consults.
install(DIRECTORY config/ DESTINATION share/chadvis-projectm-qt/config)

# Freedesktop metadata is meaningless outside Linux/BSD desktops.
if(UNIX AND NOT APPLE)
    install(FILES resources/chadvis-projectm-qt.desktop
        DESTINATION share/applications)
    install(FILES resources/icons/chadvis-projectm-qt.svg
        DESTINATION share/icons/hicolor/scalable/apps)
endif()

# ---------------------------------------------------------------------------
# CPack - portable archives everywhere; native bundle where it makes sense.
# ---------------------------------------------------------------------------

set(CPACK_PACKAGE_NAME "chadvis-projectm-qt")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Qt6 projectM v4 visualizer with modern C++23")

set(CPACK_GENERATOR "TGZ;ZIP")
if(APPLE)
    list(APPEND CPACK_GENERATOR "DragNDrop")
    # The DragNDrop generator mounts a real filesystem image and drops a symlink
    # to /Applications on it, so the volume name is what a user sees in Finder
    # while dragging. Left unset it is the package name, which is fine but says
    # nothing about what is inside.
    set(CPACK_DMG_VOLUME_NAME "ChadVis ${PROJECT_VERSION}")
elseif(WIN32)
    list(APPEND CPACK_GENERATOR "NSIS")
endif()

include(CPack)
