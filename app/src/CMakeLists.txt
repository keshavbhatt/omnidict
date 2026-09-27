# Layered libraries. Dependency direction is enforced by target_link_libraries:
#
#   app -> ui -> models -> services / platform -> core
#
# Only core and app exist so far; the other layers arrive with the milestone
# that needs them. Nothing links to `ui` except the executable (via `app`).

add_subdirectory(core)
add_subdirectory(app)

qt_add_executable(omnidict
    app/main.cpp
)
omnidict_set_warnings(omnidict)
target_link_libraries(omnidict PRIVATE omnidict_app)

set_target_properties(omnidict PROPERTIES
    # Export symbols (-rdynamic) so a crash backtrace shows function names
    # instead of bare addresses.
    ENABLE_EXPORTS ON
    # No QML anywhere, skip the qmlimportscanner finalizer.
    QT_QML_MODULE_NO_IMPORT_SCAN TRUE
)

include(GNUInstallDirs)
install(TARGETS omnidict
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
