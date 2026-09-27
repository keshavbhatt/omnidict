find_package(Qt6 ${OMNIDICT_QT_MIN_VERSION} REQUIRED COMPONENTS Test)

set(OMNIDICT_TEST_ENV
    "QT_QPA_PLATFORM=offscreen"
    "QT_LOGGING_RULES=omnidict.*.debug=false"
    "QT_FORCE_STDERR_LOGGING=1"
)

set(OMNIDICT_SNAP_RUNTIME "" CACHE PATH "kf6-core24 runtime snap root for running tests (dev builds)")
set(OMNIDICT_SNAP_RUNTIME_LIB_PATH "" CACHE STRING "LD_LIBRARY_PATH for the snap runtime (dev builds)")
if(OMNIDICT_SNAP_RUNTIME)
    list(APPEND OMNIDICT_TEST_ENV
        "LD_LIBRARY_PATH=${OMNIDICT_SNAP_RUNTIME_LIB_PATH}"
        "QT_PLUGIN_PATH=${OMNIDICT_SNAP_RUNTIME}/usr/lib/x86_64-linux-gnu/qt6/plugins"
    )
endif()

# Data shared with the pipeline lives at the repository root, one level above
# the CMake root.
get_filename_component(OMNIDICT_REPO_ROOT "${PROJECT_SOURCE_DIR}/.." ABSOLUTE)
set(OMNIDICT_FIXTURE_BUNDLE "${OMNIDICT_REPO_ROOT}/pipeline/out/sample-en/dict.sqlite"
    CACHE FILEPATH "Bundle built by the pipeline from tests/fixtures/sample-en.jsonl (make fixture)")

function(omnidict_add_test name source link_target)
    qt_add_executable(${name} ${source})
    set_target_properties(${name} PROPERTIES QT_QML_MODULE_NO_IMPORT_SCAN TRUE)
    omnidict_set_warnings(${name})
    target_link_libraries(${name} PRIVATE ${link_target} Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "${OMNIDICT_TEST_ENV}")
    if(ARGC GREATER 3 AND ARGV3 STREQUAL "GUI")
        set_tests_properties(${name} PROPERTIES LABELS "gui" TIMEOUT 120)
    else()
        set_tests_properties(${name} PROPERTIES LABELS "unit" TIMEOUT 30)
    endif()
endfunction()

# core
omnidict_add_test(tst_changelog tst_changelog.cpp omnidict_core)
target_compile_definitions(tst_changelog PRIVATE
    OMNIDICT_CHANGELOG_PATH="${OMNIDICT_REPO_ROOT}/CHANGELOG.md"
    OMNIDICT_VERSION="${PROJECT_VERSION}")
omnidict_add_test(tst_normalize tst_normalize.cpp omnidict_core)
target_compile_definitions(tst_normalize PRIVATE
    OMNIDICT_NORMALIZE_VECTORS="${OMNIDICT_REPO_ROOT}/tests/normalize_vectors.json")
omnidict_add_test(tst_bundle tst_bundle.cpp omnidict_core)
# The test rewrites a copy of the fixture to check that unreadable bundles are
# refused, which the read-only wrapper in core cannot do.
target_link_libraries(tst_bundle PRIVATE SQLite::SQLite3)
target_compile_definitions(tst_bundle PRIVATE
    OMNIDICT_FIXTURE_BUNDLE="${OMNIDICT_FIXTURE_BUNDLE}")
omnidict_add_test(tst_search_engine tst_search_engine.cpp omnidict_core)
target_link_libraries(tst_search_engine PRIVATE SQLite::SQLite3)
target_compile_definitions(tst_search_engine PRIVATE
    OMNIDICT_FIXTURE_BUNDLE="${OMNIDICT_FIXTURE_BUNDLE}")
omnidict_add_test(tst_suggester tst_suggester.cpp omnidict_core)
target_compile_definitions(tst_suggester PRIVATE
    OMNIDICT_FIXTURE_BUNDLE="${OMNIDICT_FIXTURE_BUNDLE}"
    OMNIDICT_SUGGEST_CASES="${OMNIDICT_REPO_ROOT}/tests/suggest_cases.json")
omnidict_add_test(tst_settings tst_settings.cpp omnidict_core)
omnidict_add_test(tst_user_data tst_user_data.cpp omnidict_core)
omnidict_add_test(tst_entry_renderer tst_entry_renderer.cpp omnidict_core)
target_compile_definitions(tst_entry_renderer PRIVATE
    OMNIDICT_GOLDEN_ENTRIES="${OMNIDICT_REPO_ROOT}/tests/golden/entries")
omnidict_add_test(tst_catalog tst_catalog.cpp omnidict_core)
omnidict_add_test(tst_installer tst_installer.cpp omnidict_core)
# The test builds its own .odict files with the zstd C API and edits a copy
# of the fixture's meta table directly, the way tst_bundle does.
target_link_libraries(tst_installer PRIVATE SQLite::SQLite3 Zstd::Zstd)
target_compile_definitions(tst_installer PRIVATE
    OMNIDICT_FIXTURE_BUNDLE="${OMNIDICT_FIXTURE_BUNDLE}")
# services
omnidict_add_test(tst_dictionary_manager tst_dictionary_manager.cpp omnidict_services)
# The test builds its own .odict with the zstd C API and serves it from a
# QTcpServer-based HTTP server (127.0.0.1 only, no internet).
target_link_libraries(tst_dictionary_manager PRIVATE Zstd::Zstd)
target_compile_definitions(tst_dictionary_manager PRIVATE
    OMNIDICT_FIXTURE_BUNDLE="${OMNIDICT_FIXTURE_BUNDLE}")
set_tests_properties(tst_dictionary_manager PROPERTIES TIMEOUT 60)
# ui
omnidict_add_test(tst_smoke tst_smoke.cpp omnidict_ui GUI)
target_compile_definitions(tst_smoke PRIVATE
    OMNIDICT_FIXTURE_BUNDLE="${OMNIDICT_FIXTURE_BUNDLE}")
omnidict_add_test(tst_real_bundles tst_real_bundles.cpp omnidict_core)
set_tests_properties(tst_real_bundles PROPERTIES LABELS "bundles" TIMEOUT 120)
target_compile_definitions(tst_real_bundles PRIVATE
    OMNIDICT_KNOWN_HEADWORDS="${OMNIDICT_REPO_ROOT}/tests/known_headwords.json"
    OMNIDICT_BUNDLE_DIR="${OMNIDICT_REPO_ROOT}/pipeline/out")
