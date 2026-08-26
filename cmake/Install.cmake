# Install.cmake - install rules and CPack configuration.

install(TARGETS chadvis-projectm-qt RUNTIME DESTINATION bin)
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
elseif(WIN32)
    list(APPEND CPACK_GENERATOR "NSIS")
endif()

include(CPack)
