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

# adtl_plugin is itself a shared object, so its own sources need to be
# position-independent - grunk's own libraries are SHARED now too (found via
# find_package below) and no longer need this set on their behalf.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

list(APPEND CMAKE_PREFIX_PATH "$ENV{CONDA_PREFIX}")

# adtl_plugin dynamically links grunk's own SHARED libraries (grunk_recipe/
# grunk_plugin) rather than embedding a statically-linked private copy - see
# scripts/install_grunk.cmake's own comment for why this matters (one physical copy of
# grunk's dispatch code shared with every other consumer loaded into the same
# process, not one duplicated per plugin). CMAKE_INSTALL_RPATH_USE_LINK_PATH bakes
# in the absolute install location of every library actually linked - including
# wherever find_package(grunk) below resolves to (.deps/grunk by default, see
# pixi.toml's install_grunk task) - into adtl_plugin's own installed RPATH, so it
# finds grunk's shared libraries at runtime without needing LD_LIBRARY_PATH. No
# effect on Windows - RPATH doesn't exist there; see grunk's own CMakeLists.txt for
# the platform-DLL-co-location alternative used there instead (not relevant to this
# Linux-only-for-AD repo's own non-AD build either, since this repo currently only
# ships on linux-64 - see pixi.toml's own platforms).
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
if (DEFINED ENV{CONDA_PREFIX})
    list(APPEND CMAKE_INSTALL_RPATH "$ENV{CONDA_PREFIX}/${CMAKE_INSTALL_LIBDIR}")
endif()
list(APPEND CMAKE_INSTALL_RPATH "$ORIGIN" "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")

option(GRUNK_ADOLC_TESTS "Build the test harness" ON)

# Generated-code policy (mirrors grunk-occt's own - see its DESIGN.md): bindings
# produced by the libclang-based generator (codegen/generate.py, a temporary
# duplicate of grunk-occt's own - see its own top comment) are checked into
# generated/ as the source of truth for normal builds, not regenerated on every
# build. Run via `pixi run generate_code`, which activates the pixi environment
# (python3 + libclang + pyyaml) before invoking this target.
option(GRUNK_ADOLC_GENERATE_CODE "Regenerate the native adouble binding using the codegen tool (requires clang/libclang)" OFF)

# --- grunk: a pre-built, pre-installed dependency (see scripts/install_grunk.cmake,
# wired into pixi.toml's own `install_grunk` task, `configure`'s own depends-on) -
# not vendored via FetchContent anymore. FetchContent+add_subdirectory built grunk
# from source fresh *inside this project's own build*, meaning every consumer of
# grunk (this plugin, grunk-occt's own plugin, grunk's own Python bindings) ended up
# with its own independently compiled copy - each internally consistent, but not
# the *same* copy as any other consumer loaded into the same process. find_package
# instead resolves against one shared install (grunk_DIR, or CMAKE_PREFIX_PATH - see
# .deps/grunk, this repo's own default via pixi.toml's activation.env) that every
# consumer pointed at it can share. Bump the pinned commit deliberately, in its own
# PR, gated by this repo's own test suite - never as a side effect of an unrelated
# change (see scripts/install_grunk.cmake's own GRUNK_REV, which must move together
# with this comment whenever the pin changes - there is no single source of truth
# for it the way one CMake variable shared between the two files would be, since
# install_grunk.cmake runs as its own `cmake -P` script, in its own separate
# process, before this configure even starts - see pixi.toml's own `configure`
# task, whose `depends-on` runs it first).
find_package(grunk REQUIRED)

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

# Windows only (this repo is Linux-only today - see pixi.toml's own platforms -
# but kept ready for if that scope ever widens, same as the LIBRARY/RUNTIME/ARCHIVE
# split below already is): Windows has no RPATH/RUNPATH mechanism, so a DLL loaded
# via an absolute path (grunk::plugin::load_native dlopens adtl_plugin this way)
# needs grunk's own DLLs (grunk's libraries are SHARED now - see find_package
# (grunk)'s own comment above) copied next to itself, since it searches its own
# directory first for its own dependencies.
if (WIN32)
    add_custom_command(TARGET adtl_plugin POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:adtl_plugin>
            $<TARGET_FILE_DIR:adtl_plugin>
        COMMAND_EXPAND_LISTS
    )
endif()

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

    # Windows only - see adtl_plugin's own, more detailed comment above. `pixi run
    # test` runs `ctest --test-dir build` directly against the build tree, before
    # any install step, so runUnitTests.exe needs grunk's DLLs copied next to
    # itself the same way.
    if (WIN32)
        add_custom_command(TARGET runUnitTests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_RUNTIME_DLLS:runUnitTests>
                $<TARGET_FILE_DIR:runUnitTests>
            COMMAND_EXPAND_LISTS
        )
    endif()

    gtest_discover_tests(runUnitTests)
endif()
