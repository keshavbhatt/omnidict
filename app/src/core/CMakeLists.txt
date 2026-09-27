# core: dictionary bundles, lookup, normalization. No widgets, no network.
# Links Qt6::Core only, so everything here is testable without a display.

qt_add_library(omnidict_core STATIC
    bundle.cpp
    bundle.h
    changelog.cpp
    changelog.h
    entry.h
    entry_renderer.cpp
    entry_renderer.h
    library.cpp
    library.h
    logging.cpp
    logging.h
    normalize.cpp
    normalize.h
    result.h
    search_engine.cpp
    search_engine.h
    sqlite_db.cpp
    sqlite_db.h
    suggester.cpp
    suggester.h
    user_data.cpp
    user_data.h
)
omnidict_set_warnings(omnidict_core)

target_include_directories(omnidict_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(omnidict_core
    PUBLIC
        Qt6::Core
    PRIVATE
        SQLite::SQLite3
        ICU::uc
)
