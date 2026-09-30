# services: long-lived objects that own resources for the UI, such as the
# dictionary library on its worker thread. No widgets.

qt_add_library(omnidict_services STATIC
    dictionary_manager.cpp
    dictionary_manager.h
    global_shortcuts.cpp
    global_shortcuts.h
    logging.cpp
    logging.h
    lookup_service.cpp
    lookup_service.h
)
omnidict_set_warnings(omnidict_services)

target_include_directories(omnidict_services PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(omnidict_services
    PUBLIC
        omnidict_core
        Qt6::Network
        Qt6::DBus
)
