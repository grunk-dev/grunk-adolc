#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
#
# SPDX-License-Identifier: MPL-2.0

# Clones, builds, and installs grunk into .deps/grunk - run via `pixi run
# install_grunk`. Idempotent: skips the whole build if that prefix already looks
# populated, so re-running this after a partial failure elsewhere doesn't rebuild
# grunk from scratch every time - matches scripts/install_adolc.sh's own pattern
# exactly (see its own comment for why .deps/, not build/_deps/).
#
# CMakeLists.txt used to FetchContent+add_subdirectory grunk's source directly,
# statically linking its compiled libraries into adtl_plugin - replaced with this
# bootstrap + find_package(grunk REQUIRED) so adtl_plugin instead dynamically links
# grunk's own SHARED libraries (grunk's own CMakeLists.txt builds grunk_recipe/
# grunk_plugin as SHARED - see its own top-level CMakeLists.txt comment). A plugin
# that statically embeds its own private copy of grunk's dispatch code, loaded into
# the same process as anything else that does the same (this project's own Python
# bindings, or a sibling plugin like grunk-occt's), is a real, confirmed hazard -
# see DESIGN.md's "A related hazard, found outside the AD case" (grunk-occt's own
# copy of that finding). Every consumer of grunk pointed at the *same* install
# (this one, or another - see grunk_DIR below) shares one physical set of shared
# libraries instead.
set -euo pipefail

ROOT="${PIXI_PROJECT_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
GRUNK_PREFIX="$ROOT/.deps/grunk"
GRUNK_REV="a372159c47d3722e138634ad9997c8037c6fd422"

if [ -f "$GRUNK_PREFIX/lib/cmake/grunk/grunkConfig.cmake" ]; then
    echo "grunk already installed at $GRUNK_PREFIX - skipping (delete this directory to force a rebuild)."
    exit 0
fi

mkdir -p "$ROOT/build/_deps"
cd "$ROOT/build/_deps"

if [ ! -d grunk ]; then
    git clone https://github.com/grunk-dev/grunk.git
fi
cd grunk
git fetch origin "$GRUNK_REV"
git checkout "$GRUNK_REV"

# GRUNK_WITH_TASKFLOW=OFF - see this repo's own CMakeLists.txt's previous comment
# on the same setting: Taskflow fails to compile against this pinned commit under
# at least GCC 13/recent Xcode clang, and adtl_plugin never needs it anyway.
# GRUNK_PYTHON=OFF - this bootstrap only ever needs to satisfy adtl_plugin's own
# C++ find_package(grunk REQUIRED); grunk's own Python bindings are a separate,
# pip-managed dependency (see pixi.toml's [pypi-dependencies]).
cmake -G Ninja -S . -B build \
    -D CMAKE_BUILD_TYPE=Release \
    -D CMAKE_INSTALL_PREFIX="$GRUNK_PREFIX" \
    -D GRUNK_WITH_TASKFLOW=OFF \
    -D GRUNK_WITH_DYNAMIC=ON \
    -D GRUNK_WITH_RECIPE=ON \
    -D GRUNK_WITH_PLUGIN=ON \
    -D GRUNK_TESTS=OFF \
    -D GRUNK_PYTHON=OFF
cmake --build build --parallel
cmake --install build

echo "grunk installed to $GRUNK_PREFIX"
ls "$GRUNK_PREFIX"/lib/libgrunk* 2>/dev/null
