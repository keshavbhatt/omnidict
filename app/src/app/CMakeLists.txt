# app: process entry point helpers and command line handling.
# Version metadata is generated from CMake, never hand-written.

configure_file(version.h.in "${CMAKE_CURRENT_BINARY_DIR}/generated/app/version.h" @ONLY)

qt_add_library(omnidict_app STATIC
    debug_hooks.cpp
    debug_hooks.h
    logging.cpp
    logging.h
    lookup_command.cpp
    lookup_command.h
    single_instance.cpp
    single_instance.h
)
omnidict_set_warnings(omnidict_app)

target_include_directories(omnidict_app PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
)
target_link_libraries(omnidict_app
    PUBLIC
        omnidict_ui
        Qt6::Network
)
