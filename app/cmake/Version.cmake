# Derives version metadata for the build.
#
# OMNIDICT_VERSION        -> "0.1.0" (from project(VERSION))
# OMNIDICT_GIT_REVISION   -> short hash or "unknown"
#
# These are handed to the code via a generated header (see src/app/CMakeLists.txt),
# never via string literals in sources.

set(OMNIDICT_VERSION "${PROJECT_VERSION}")

# Monorepo: the CMake root is app/, the git root is one level up, so ask git
# from the project directory instead of looking for a .git entry here.
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${PROJECT_SOURCE_DIR}" rev-parse --short HEAD
        OUTPUT_VARIABLE OMNIDICT_GIT_REVISION
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
endif()
if(NOT OMNIDICT_GIT_REVISION)
    set(OMNIDICT_GIT_REVISION "unknown")
endif()

message(STATUS "omnidict ${OMNIDICT_VERSION} (${OMNIDICT_GIT_REVISION})")
