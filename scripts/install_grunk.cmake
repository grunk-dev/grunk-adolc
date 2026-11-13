# SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
#
# SPDX-License-Identifier: MPL-2.0

# Clones, builds, and installs grunk into .deps/grunk - run via `cmake -P
# scripts/install_grunk.cmake` (see pixi.toml's own install_grunk task). A plain
# CMake script (`cmake -P`), not a shell script - matches grunk-occt's own
# identical script (adopted from there for consistency, and so this repo is ready
# if its own currently Linux-only platform scope (see pixi.toml's own comment) ever
# widens to macOS/Windows without needing a rewrite - bash isn't available on
# Windows by default, but cmake itself is already a hard dependency everywhere this
# script runs. Idempotent: skips the whole build if that prefix already looks
# populated, so re-running this after a partial failure elsewhere doesn't rebuild
# grunk from scratch every time - matches scripts/install_adolc.sh's own pattern
# (see its own comment for why .deps/, not build/_deps/).
#
# CMakeLists.txt used to FetchContent+add_subdirectory grunk's source directly,
# statically linking its compiled libraries into adolc_plugin - replaced with this
# bootstrap + find_package(grunk REQUIRED) so adolc_plugin instead dynamically links
# grunk's own SHARED libraries (grunk's own CMakeLists.txt builds grunk_recipe/
# grunk_plugin as SHARED - see its own top-level CMakeLists.txt comment). A plugin
# that statically embeds its own private copy of grunk's dispatch code, loaded into
# the same process as anything else that does the same (this project's own Python
# bindings, or a sibling plugin like grunk-occt's), is a real, confirmed hazard -
# see DESIGN.md's "A related hazard, found outside the AD case" (grunk-occt's own
# copy of that finding). Every consumer of grunk pointed at the *same* install
# (this one, or another - see grunk_DIR/CMAKE_PREFIX_PATH) shares one physical set
# of shared libraries instead.

cmake_minimum_required(VERSION 3.15)

# Bumped 2026-11-13 from 583da89f0184ef6333000c45573da65a9dd551d0 (23 commits
# behind) to 6bf642412c7d501101585a41370050386551f759 (grunk's own main, post PR
# grunk-dev/grunk#315 merge) - catches this repo up to the same pin grunk-occt
# and grunk-geoml already carry. Notable fixes picked up along the way (see
# grunk-geoml's own scripts/install_grunk.cmake for the full per-bump history):
# SOL_ALL_SAFETIES_ON=1 (checked sol2 getters everywhere - a mismatched
# usertype argument now throws instead of silently corrupting memory),
# with_std_vector's own cast_to_element by-value fix, classify_param's
# REJECTED sentinel, a mutable-argument-in-parametric-env safeguard, a
# DynamicFeature::as() dangling-pointer fix, Handle(Derived)->Handle(Base)
# smart-pointer inheritance, and sol::meta_function::call registration for
# Feature-wrapped operator(). Verified via a clean standalone build+test
# against this new pin (no per-repo workaround needed here, unlike grunk-occt's
# own geom_extras.hpp fix for SOL_ALL_SAFETIES_ON - this repo's own
# adolc_plugin registration code had nothing that tripped over it).
set(GRUNK_REV "6bf642412c7d501101585a41370050386551f759")

get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(GRUNK_PREFIX "${PROJECT_ROOT}/.deps/grunk")
set(GRUNK_CHECKOUT "${PROJECT_ROOT}/build/_deps/grunk")

if(EXISTS "${GRUNK_PREFIX}/lib/cmake/grunk/grunkConfig.cmake")
    message(STATUS "grunk already installed at ${GRUNK_PREFIX} - skipping (delete this directory to force a rebuild).")
    return()
endif()

# A small helper so every step below fails loudly (and stops the script) the same
# way, instead of silently continuing past a broken clone/configure/build/install.
function(run_or_die)
    # ${ARGV} already starts with COMMAND (every call site below spells it out for
    # readability, matching execute_process's own keyword syntax) - do not prepend
    # a second COMMAND here, or execute_process sees "COMMAND COMMAND ..." and
    # fails with "given COMMAND argument with no value".
    execute_process(${ARGV} RESULT_VARIABLE _result)
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR "Command failed (exit ${_result}): ${ARGV}")
    endif()
endfunction()

if(NOT EXISTS "${GRUNK_CHECKOUT}/.git")
    file(MAKE_DIRECTORY "${PROJECT_ROOT}/build/_deps")
    run_or_die(COMMAND git clone https://github.com/grunk-dev/grunk.git grunk
        WORKING_DIRECTORY "${PROJECT_ROOT}/build/_deps")
endif()

run_or_die(COMMAND git fetch origin "${GRUNK_REV}" WORKING_DIRECTORY "${GRUNK_CHECKOUT}")
run_or_die(COMMAND git checkout "${GRUNK_REV}" WORKING_DIRECTORY "${GRUNK_CHECKOUT}")

# GRUNK_PYTHON=OFF - this bootstrap only ever needs to satisfy adolc_plugin's own
# C++ find_package(grunk REQUIRED); grunk's own Python bindings are a separate,
# pip-managed dependency (see pixi.toml's [pypi-dependencies]).
run_or_die(COMMAND "${CMAKE_COMMAND}" -G Ninja -S . -B build
        -D CMAKE_BUILD_TYPE=Release
        -D CMAKE_INSTALL_PREFIX=${GRUNK_PREFIX}
        -D GRUNK_WITH_TASKFLOW=OFF
        -D GRUNK_WITH_DYNAMIC=ON
        -D GRUNK_WITH_RECIPE=ON
        -D GRUNK_WITH_PLUGIN=ON
        -D GRUNK_TESTS=OFF
        -D GRUNK_PYTHON=OFF
    WORKING_DIRECTORY "${GRUNK_CHECKOUT}")
run_or_die(COMMAND "${CMAKE_COMMAND}" --build build --parallel WORKING_DIRECTORY "${GRUNK_CHECKOUT}")
run_or_die(COMMAND "${CMAKE_COMMAND}" --install build WORKING_DIRECTORY "${GRUNK_CHECKOUT}")

message(STATUS "grunk installed to ${GRUNK_PREFIX}")
