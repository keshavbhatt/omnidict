# Layered libraries. Dependency direction is enforced by target_link_libraries:
#
#   app -> ui -> models -> services / platform -> core
#
# platform arrives with the milestone that needs it. Nothing links to `ui`
# except the executable (via `app`).

# More apps from Ktechpit (KtechpitApps, MIT, a squashed subtree of
# github.com/keshavbhatt/ktechpit-apps), used by ui and app.
add_subdirectory(modules/KtechpitApps)
add_subdirectory(core)
add_subdirectory(services)
add_subdirectory(models)
add_subdirectory(ui)
add_subdirectory(app)

qt_add_executable(omnidict WIN32
    app/main.cpp
)
if(WIN32)
    target_sources(omnidict PRIVATE ${PROJECT_SOURCE_DIR}/dist/windows/omnidict.rc)
endif()
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
