macro(TARGET_ASSIMP)
    set(ASSIMP_EXTRA_NAMES)
    if (WIN32)
        # was assimp-vc142-mt for me, but could be different for others
        foreach(VC_VERSION RANGE 100 399)
            list(APPEND ASSIMP_EXTRA_NAMES assimp-vc${VC_VERSION}-mt)
        endforeach()
    endif()

    find_library(ASSIMP_LIBRARY_RELEASE assimp ${ASSIMP_EXTRA_NAMES} PATHS ${VCPKG_INSTALL_ROOT}/lib NO_DEFAULT_PATH)
    find_library(ASSIMP_LIBRARY_DEBUG assimp ${ASSIMP_EXTRA_NAMES} PATHS ${VCPKG_INSTALL_ROOT}/debug/lib NO_DEFAULT_PATH)
    select_library_configurations(ASSIMP)

    target_link_libraries(${TARGET_NAME} ${ASSIMP_LIBRARIES})

    if (UNIX AND NOT APPLE AND NOT ANDROID)
        target_link_libraries(${TARGET_NAME} Xxf86vm)
        # assimp's build uses the system minizip when libminizip-dev is present (Ubuntu 24.04), leaving
        # the unz* symbols undefined in the static libassimp.a; link it explicitly when it exists.
        find_library(MINIZIP_LIBRARY minizip)
        if (MINIZIP_LIBRARY)
            target_link_libraries(${TARGET_NAME} ${MINIZIP_LIBRARY})
        endif()
    endif()
endmacro()
