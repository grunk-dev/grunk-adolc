# SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
#
# SPDX-License-Identifier: MPL-2.0

cmake_minimum_required(VERSION 3.15)
project(grunk-adolc VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Step 8 (packaging polish, see DESIGN.md's "Packaging" - mirrors grunk-occt's own
# identical addition) - standard CMake per-platform install subdirectory names
# (lib/, bin/, ...) for the install() call further down.
include(GNUInstallDirs)

# adtl_plugin (below) links the static grunk libraries into a shared object, which
# requires their objects to be position-independent - set this before grunk's own
# add_subdirectory call so it applies there too.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

list(APPEND CMAKE_PREFIX_PATH "$ENV{CONDA_PREFIX}")

option(GRUNK_ADOLC_TESTS "Build the test harness" ON)

# Generated-code policy (mirrors grunk-occt's own - see its DESIGN.md): bindings
# produced by the libclang-based generator (codegen/generate.py, a temporary
# duplicate of grunk-occt's own - see its own top comment) are checked into
# generated/ as the source of truth for normal builds, not regenerated on every
# build. Run via `pixi run generate_code`, which activates the pixi environment
# (python3 + libclang + pyyaml) before invoking this target.
option(GRUNK_ADOLC_GENERATE_CODE "Regenerate the native adouble binding using the codegen tool (requires clang/libclang)" OFF)

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
    GIT_TAG a372159c47d3722e138634ad9997c8037c6fd422
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

# ADOL-C itself (see README.md's "Building" section / scripts/install_adolc.sh) -
# found via pkg-config, matching grunk-occt's own occt_adolc/ (its AD-enabled
# build) doing the exact same thing against the exact same ADOL-C install, so
# both plugins are guaranteed ABI-compatible as long as both are pointed at the
# same .deps/adolc prefix (see DESIGN.md's own note on why this matters: sharing
# one native adtl::adouble/Standard_Real registration across plugins requires
# them to genuinely be the same compiled type, not just the same source).
find_package(PkgConfig REQUIRED)
pkg_check_modules(ADOLC REQUIRED IMPORTED_TARGET GLOBAL adolc)
list(GET ADOLC_INCLUDE_DIRS 0 ADOLC_INCLUDE_DIR)

# --- code generator (see codegen/README.md - grunk-occt's own copy, not
# duplicated here): opt-in, not wired as a dependency of adtl_plugin below -
# regenerated output needs review/diffing before being trusted, not silent
# regeneration on every build (same policy as grunk-occt's own). Run via
# `pixi run generate_code`, which activates the pixi environment (python3 +
# libclang + pyyaml) before invoking this target.
if(GRUNK_ADOLC_GENERATE_CODE)
    add_custom_target(generate_code
        COMMAND python3 ${CMAKE_SOURCE_DIR}/codegen/generate.py
                --config ${CMAKE_SOURCE_DIR}/codegen/config.yml
                --occt-include-dir ${ADOLC_INCLUDE_DIR}
                --output-dir ${CMAKE_SOURCE_DIR}/generated
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Regenerating the native adouble binding (codegen/generate.py)"
        VERBATIM
    )
endif()

add_library(adtl_plugin SHARED src/adtl_plugin.cpp generated/adouble.cpp)
target_link_libraries(adtl_plugin PRIVATE grunk::plugin PkgConfig::ADOLC)

# Step 8 (packaging polish, see DESIGN.md's "Packaging" - mirrors grunk-occt's own
# identical addition): adtl_plugin is a runtime-loaded grunk plugin, never linked
# against by a consuming application (grunk::plugin::load_native dlopens it by
# path) - so there is no CMake target/package-config to export here, just the
# built shared library itself. LIBRARY/RUNTIME/ARCHIVE cover every platform's own
# convention for a SHARED target, even though this repo is currently Linux-only
# (see pixi.toml's own platforms - LIBRARY is the one that actually applies
# today, RUNTIME/ARCHIVE are here so this doesn't need revisiting if that
# platform scope ever widens).
install(TARGETS adtl_plugin
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

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
