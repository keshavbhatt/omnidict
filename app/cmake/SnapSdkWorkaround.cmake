# Dev builds against the kde-qt6-core24-sdk snap on a non-Ubuntu host: some
# imported targets export the SDK's generic /usr/include (Ubuntu glibc
# headers), which shadow the host toolchain's libc headers and break
# libstdc++. The Qt headers proper live under .../include/<arch>/qt6 and are
# unaffected, but SQLite and ICU keep their headers directly in that
# directory. So the directory is swapped for a small farm in the build tree
# that links only the headers this project needs from it.
# Harmless in snapcraft builds (the dir then equals the real sysroot's).
#
# Must be included AFTER every find_package() call.

if(CMAKE_PREFIX_PATH MATCHES "/snap/kde-qt6-core24-sdk/")
    set(_omnidict_farm "${CMAKE_BINARY_DIR}/sdk-include")
    set(_omnidict_farmed sqlite3.h sqlite3ext.h unicode)
    file(MAKE_DIRECTORY "${_omnidict_farm}")

    get_property(_omnidict_imported DIRECTORY PROPERTY IMPORTED_TARGETS)
    foreach(_tgt IN LISTS _omnidict_imported)
        get_target_property(_incs ${_tgt} INTERFACE_INCLUDE_DIRECTORIES)
        if(_incs)
            set(_filtered "")
            foreach(_dir IN LISTS _incs)
                if(_dir MATCHES "/snap/kde-qt6-core24-sdk/[^/]+/usr/include$")
                    foreach(_entry IN LISTS _omnidict_farmed)
                        if(EXISTS "${_dir}/${_entry}" AND NOT EXISTS "${_omnidict_farm}/${_entry}")
                            file(CREATE_LINK "${_dir}/${_entry}" "${_omnidict_farm}/${_entry}" SYMBOLIC)
                        endif()
                    endforeach()
                    list(APPEND _filtered "${_omnidict_farm}")
                else()
                    list(APPEND _filtered "${_dir}")
                endif()
            endforeach()
            list(REMOVE_DUPLICATES _filtered)
            set_property(TARGET ${_tgt} PROPERTY INTERFACE_INCLUDE_DIRECTORIES "${_filtered}")
        endif()
    endforeach()
    unset(_omnidict_imported)
    unset(_omnidict_farm)
    unset(_omnidict_farmed)
    unset(_filtered)
    unset(_incs)
endif()
