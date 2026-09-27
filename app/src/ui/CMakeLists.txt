# ui: widgets. Built in code (no .ui files); nothing links to this except app.

qt_add_library(omnidict_ui STATIC
    about_dialog.cpp
    about_dialog.h
    bug_report_dialog.cpp
    bug_report_dialog.h
    diagnostics.cpp
    diagnostics.h
    dictionaries_dialog.cpp
    dictionaries_dialog.h
    empty_state.cpp
    empty_state.h
    entry_view.cpp
    entry_view.h
    first_run_panel.cpp
    first_run_panel.h
    icons.cpp
    icons.h
    logging.cpp
    logging.h
    main_window.cpp
    main_window.h
    results_delegate.cpp
    results_delegate.h
    search_field.cpp
    search_field.h
    settings_dialog.cpp
    settings_dialog.h
    shortcuts_dialog.cpp
    shortcuts_dialog.h
    style.cpp
    style.h
    switch_button.cpp
    switch_button.h
    theme_applier.cpp
    theme_applier.h
    toast.cpp
    toast.h
    whats_new_dialog.cpp
    whats_new_dialog.h
)
omnidict_set_warnings(omnidict_ui)

target_include_directories(omnidict_ui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(omnidict_ui
    PUBLIC
        omnidict_models
        omnidict_services
        Qt6::Widgets
    PRIVATE
        Qt6::Svg
)

# UI glyphs (Lucide, tinted at runtime by ui::icons) and the app icon.
set(OMNIDICT_ICON_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../resources/icons")
file(GLOB OMNIDICT_UI_ICONS "${OMNIDICT_ICON_DIR}/ui/*.svg")
qt_add_resources(omnidict_ui "icons"
    PREFIX "/icons"
    BASE "${OMNIDICT_ICON_DIR}"
    FILES
        "${OMNIDICT_ICON_DIR}/hicolor/16x16/apps/com.ktechpit.omnidict.png"
        "${OMNIDICT_ICON_DIR}/hicolor/24x24/apps/com.ktechpit.omnidict.png"
        "${OMNIDICT_ICON_DIR}/hicolor/32x32/apps/com.ktechpit.omnidict.png"
        "${OMNIDICT_ICON_DIR}/hicolor/48x48/apps/com.ktechpit.omnidict.png"
        "${OMNIDICT_ICON_DIR}/hicolor/64x64/apps/com.ktechpit.omnidict.png"
        "${OMNIDICT_ICON_DIR}/hicolor/128x128/apps/com.ktechpit.omnidict.png"
        "${OMNIDICT_ICON_DIR}/hicolor/256x256/apps/com.ktechpit.omnidict.png"
        ${OMNIDICT_UI_ICONS}
)

# The changelog feeds the What's new sheet (:/text/CHANGELOG.md).
get_filename_component(OMNIDICT_REPO_DIR "${PROJECT_SOURCE_DIR}/.." ABSOLUTE)
qt_add_resources(omnidict_ui "text"
    PREFIX "/text"
    BASE "${OMNIDICT_REPO_DIR}"
    FILES "${OMNIDICT_REPO_DIR}/CHANGELOG.md"
)
