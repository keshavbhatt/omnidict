# ui: widgets. Built in code (no .ui files); nothing links to this except app.

qt_add_library(omnidict_ui STATIC
    about_dialog.cpp
    about_dialog.h
    entry_view.cpp
    entry_view.h
    logging.cpp
    logging.h
    main_window.cpp
    main_window.h
    results_delegate.cpp
    results_delegate.h
)
omnidict_set_warnings(omnidict_ui)

target_include_directories(omnidict_ui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(omnidict_ui
    PUBLIC
        omnidict_models
        omnidict_services
        Qt6::Widgets
)
