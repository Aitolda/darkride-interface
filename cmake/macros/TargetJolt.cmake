#
#  TargetJolt.cmake
#  cmake/macros
#
#  Jolt Physics (MIT, https://github.com/jrouwe/JoltPhysics), compiled inside this build tree from a pinned
#  release tarball. It is deliberately not a vcpkg port: adding a port changes the vcpkg hash and rebuilds
#  every dependency beside the shared install the other checkouts use.
#
#  The tarball is taken from $TIVOLI_DOWNLOADS/jolt-v<version>.tar.gz or ../downloads/jolt-v<version>.tar.gz
#  beside this checkout when either exists, and from GitHub otherwise. Its SHA-256 is checked in every case.
#
#  Distributed under the Apache License, Version 2.0.
#  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
#
macro(TARGET_JOLT)
    if (NOT TARGET Jolt)
        include(FetchContent)
        set(JOLT_VERSION "5.6.0")
        set(JOLT_SHA256 "6e069ee0172478cc78182047aac87e5310ba14a67a53348ae14cc37801fd3f8e")
        set(JOLT_TARBALL "jolt-v${JOLT_VERSION}.tar.gz")
        # one URL only: CMake refuses a list that mixes a local path with web addresses
        set(JOLT_URL "https://github.com/jrouwe/JoltPhysics/archive/refs/tags/v${JOLT_VERSION}.tar.gz")
        foreach(JOLT_DIR "${CMAKE_SOURCE_DIR}/../downloads" "$ENV{TIVOLI_DOWNLOADS}")
            if (NOT "${JOLT_DIR}" STREQUAL "" AND EXISTS "${JOLT_DIR}/${JOLT_TARBALL}")
                get_filename_component(JOLT_URL "${JOLT_DIR}/${JOLT_TARBALL}" ABSOLUTE)
            endif()
        endforeach()
        message(STATUS "Jolt ${JOLT_VERSION} from ${JOLT_URL}")

        FetchContent_Declare(joltphysics
            URL "${JOLT_URL}"
            URL_HASH SHA256=${JOLT_SHA256}
            SOURCE_SUBDIR Jolt   # has no CMakeLists.txt, so only the sources are fetched; Jolt.cmake is included below
        )
        FetchContent_MakeAvailable(joltphysics)

        # The options Jolt/Jolt.cmake reads. They must be the same in every target that includes Jolt headers,
        # which Jolt enforces at RegisterTypes() through JPH_VERSION_ID; the Jolt target carries them as
        # PUBLIC compile definitions, so linking it is enough.
        set(PHYSICS_REPO_ROOT "${joltphysics_SOURCE_DIR}")
        set(CROSS_PLATFORM_DETERMINISTIC ON)      # the same answer on the Mac, the Linux servers and every client
        set(DOUBLE_PRECISION OFF)                 # frames keep the big numbers out of the simulation (PHYSICS.md 4.9)
        set(OBJECT_LAYER_BITS 16)
        set(GENERATE_DEBUG_SYMBOLS ON)
        set(OVERRIDE_CXX_FLAGS OFF)               # never let Jolt rewrite this tree's flags
        set(CPP_EXCEPTIONS_ENABLED ON)            # match the rest of the tree
        set(CPP_RTTI_ENABLED ON)
        set(ENABLE_ALL_WARNINGS OFF)
        set(INTERPROCEDURAL_OPTIMIZATION OFF)
        set(FLOATING_POINT_EXCEPTIONS_ENABLED OFF)
        set(USE_ASSERTS OFF)
        set(ENABLE_OBJECT_STREAM OFF)             # Jolt's own serialisation; entities are the source of truth here
        set(ENABLE_INSTALL OFF)
        set(DEBUG_RENDERER_IN_DISTRIBUTION ON)    # the Bullet wireframe menu items keep working, in every config
        set(PROFILER_IN_DEBUG_AND_RELEASE OFF)
        set(PROFILER_IN_DISTRIBUTION OFF)
        set(JPH_BUILD_SHARED_LIBS OFF)
        set(JPH_USE_DX12 OFF)
        set(JPH_USE_VK OFF)
        set(JPH_USE_MTL OFF)
        set(JPH_USE_CPU_COMPUTE OFF)
        include("${joltphysics_SOURCE_DIR}/Jolt/Jolt.cmake")
        set_target_properties(Jolt PROPERTIES FOLDER "hidden/externals")
    endif()
    target_link_libraries(${TARGET_NAME} Jolt)
endmacro()
