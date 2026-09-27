# Finds zstd (ADR-015): CMake ships no FindZstd module of its own, and the
# `kde-qt6-core24-sdk` snap carries the header and a dev symlink but no
# zstd-config.cmake. Mirrors FindSQLite3's shape so callers use it the same
# way: `find_package(Zstd REQUIRED)` then link `Zstd::Zstd`.
#
# Respects Zstd_INCLUDE_DIR / Zstd_LIBRARY when the caller sets them (see
# scripts/dev-build.sh, which points Zstd_LIBRARY at the core24 runtime farm
# so the RPATH never reaches core24's glibc, the same trick used for SQLite3).

find_path(Zstd_INCLUDE_DIR NAMES zstd.h)
find_library(Zstd_LIBRARY NAMES zstd libzstd)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Zstd
    REQUIRED_VARS Zstd_LIBRARY Zstd_INCLUDE_DIR
)

if(Zstd_FOUND AND NOT TARGET Zstd::Zstd)
    add_library(Zstd::Zstd UNKNOWN IMPORTED)
    set_target_properties(Zstd::Zstd PROPERTIES
        IMPORTED_LOCATION "${Zstd_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${Zstd_INCLUDE_DIR}"
    )
endif()

mark_as_advanced(Zstd_INCLUDE_DIR Zstd_LIBRARY)
