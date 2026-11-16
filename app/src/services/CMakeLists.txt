# services: long-lived objects that own resources for the UI, such as the
# dictionary library on its worker thread. No widgets.

qt_add_library(omnidict_services STATIC
    autostart.cpp
    autostart.h
    dictionary_manager.cpp
    dictionary_manager.h
    global_shortcuts.cpp
    global_shortcuts.h
    hotkey_backend.cpp
    hotkey_backend.h
    logging.cpp
    logging.h
    lookup_service.cpp
    lookup_service.h
    pro_status.h
    selection.cpp
    selection.h
)
omnidict_set_warnings(omnidict_services)

target_include_directories(omnidict_services PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(omnidict_services
    PUBLIC
        omnidict_core
        Qt6::Network
        Qt6::DBus
)

# Windows: start at login through the MSIX startup task, with C++/WinRT from the Windows SDK
# (ADR-022); the quick lookup key through RegisterHotKey (user32, linked by default); the
# selected word through UI Automation (COM, mocks/windows.html note 5).
if(WIN32)
    target_sources(omnidict_services PRIVATE autostart_windows.cpp selection_windows.cpp)
    target_link_libraries(omnidict_services PRIVATE WindowsApp ole32 oleaut32 uiautomationcore)
endif()

# Omnidict Pro through the Ktechpit licensing module (ADR-024): the Microsoft Store build only.
# Its code is under its own licence (LICENSING.md) and must not be in builds anyone else makes,
# so every other build leaves it out and has no Pro at all.
if(OMNIDICT_LICENSING)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../modules/AccountAndLicense"
                     "${CMAKE_CURRENT_BINARY_DIR}/AccountAndLicense")
    target_sources(omnidict_services PRIVATE
        licensing/license_config.h
        licensing/license_service.cpp
        licensing/license_service.h
    )
    target_link_libraries(omnidict_services PUBLIC account_license)
    target_compile_definitions(omnidict_services PUBLIC OMNIDICT_HAS_LICENSING)
endif()
