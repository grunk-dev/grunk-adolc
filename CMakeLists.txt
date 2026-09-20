# SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
#
# SPDX-License-Identifier: MPL-2.0

cmake_minimum_required(VERSION 3.15)
project(grunk-adolc VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# adtl_plugin (below) links the static grunk libraries into a shared object, which
# requires their objects to be position-independent - set this before grunk's own
# add_subdirectory call so it applies there too.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

list(APPEND CMAKE_PREFIX_PATH "$ENV{CONDA_PREFIX}")

option(GRUNK_ADOLC_TESTS "Build the test harness" ON)

# --- grunk (FetchContent, pinned to a specific commit - see grunk-occt's own
# CMakeLists.txt/DESIGN.md for why: grunk's plugin interface and 0.5.0 rewrite are
# only on `main`, not yet tagged). Pinned to the *same* commit grunk-occt currently
# uses, for no reason stronger than keeping the two sibling repos on one known-good
# grunk revision by default - they don't share any code and could be bumped
# independently if a reason ever comes up. Bump deliberately, in its own PR, gated by
# this repo's own test suite - never as a side effect of an unrelated change.
include(FetchContent)

FetchContent_Declare(grunk
    GIT_REPOSITORY https://github.com/grunk-dev/grunk.git
    GIT_TAG c16f519888eae62564145b1ba4c025c1e6dcc6c2
)

FetchContent_GetProperties(grunk)
if(NOT grunk_POPULATED)
    if(POLICY CMP0169)
        cmake_policy(SET CMP0169 OLD)
    endif()
    FetchContent_Populate(grunk)

    if(EXISTS "${grunk_SOURCE_DIR}/cmake/grunkConfig.cmake.in")
        file(MAKE_DIRECTORY "${CMAKE_SOURCE_DIR}/cmake")
        file(COPY "${grunk_SOURCE_DIR}/cmake/grunkConfig.cmake.in"
             DESTINATION "${CMAKE_SOURCE_DIR}/cmake"
             NO_SOURCE_PERMISSIONS)
    endif()

    # Taskflow fails to compile against this project's own pinned grunk commit under
    # at least GCC 13/recent Xcode clang (C++20 `concept`/`iter_value_t` errors - see
    # grunk-occt's own DESIGN.md for the same finding) - disabled since adtl_plugin
    # never needs Taskflow's multithreaded grunk::core execution either.
    set(GRUNK_WITH_TASKFLOW OFF CACHE BOOL "" FORCE)
    set(GRUNK_WITH_DYNAMIC ON CACHE BOOL "" FORCE)
    set(GRUNK_WITH_RECIPE ON CACHE BOOL "" FORCE)
    set(GRUNK_TESTS OFF CACHE BOOL "" FORCE)
    set(GRUNK_PYTHON OFF CACHE BOOL "" FORCE)

    include_directories(${grunk_BINARY_DIR})
    add_subdirectory(${grunk_SOURCE_DIR} ${grunk_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()

# adtl.so is a SWIG-generated Lua module wrapping ADOL-C's tapeless adouble type,
# built manually from
# https://gitlab.dlr.de/dlr-sp/occt-differentiation/swig-adol-c.git (branch
# lua_wrapper, DLR-internal - see README.md for build instructions and why this can't
# be a FetchContent/conda-forge dependency). adtl_plugin (below) links directly
# against it and re-exposes it through grunk::plugin's fixed native ABI
# (src/adtl_plugin.cpp) - ADOL-C itself is never named in this project (no header, no
# direct library link): it's confined entirely behind adtl.so and, from there, behind
# adtl_plugin.so.
set(ADTL_LIB_DIR "${CMAKE_SOURCE_DIR}/build/_deps/swig-adol-c/build" CACHE PATH "Directory containing the built adtl Lua module (adtl.so)")

# swig_add_library sets PREFIX "" on the adtl target, so the file is literally named
# "adtl.so" (or "adtl.dylib"/"adtl.dll") with no "lib" prefix - find_library's
# implicit lib/suffix conventions won't match that, so look it up as a plain file
# instead.
find_file(ADTL_LIBRARY
    NAMES adtl.so adtl.dylib adtl.dll
    PATHS "${ADTL_LIB_DIR}"
    NO_DEFAULT_PATH
)

if(NOT ADTL_LIBRARY)
    message(FATAL_ERROR "adtl Lua module (adtl.so) not found, define ADTL_LIB_DIR. See README.md for build instructions.")
endif()

message(STATUS "Found adtl Lua module: ${ADTL_LIBRARY}")

add_library(adtl_plugin SHARED src/adtl_plugin.cpp)
target_link_libraries(adtl_plugin PRIVATE grunk::plugin ${ADTL_LIBRARY})

# --- test harness: loads adtl_plugin via grunk::plugin::load_native and drives it
# through raw Lua via the sol2 state directly - matches grunk-occt's own test-harness
# shape (see its DESIGN.md).
if(GRUNK_ADOLC_TESTS)
    enable_testing()
    find_package(GTest REQUIRED)
    include(GoogleTest)

    add_executable(runUnitTests
        tests/test_load.cpp
        tests/test_recipe.cpp
    )
    target_link_libraries(runUnitTests PRIVATE grunk::plugin grunk::recipe GTest::gtest_main)
    target_compile_definitions(runUnitTests PRIVATE
        ADTL_PLUGIN_SO_PATH="$<TARGET_FILE:adtl_plugin>"
    )
    add_dependencies(runUnitTests adtl_plugin)
    gtest_discover_tests(runUnitTests)
endif()
