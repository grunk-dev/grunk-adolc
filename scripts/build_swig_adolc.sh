#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
#
# SPDX-License-Identifier: MPL-2.0

# Clones and builds swig-adol-c (DLR-internal, see README.md) against the ADOL-C
# install produced by `pixi run install_adolc`, producing adtl.so - run via
# `pixi run build_swig_adolc`. Idempotent: skips the build if adtl.so already
# exists at the expected path.
#
# Bakes in a workaround for a real bug in swig-adol-c's own CMakeLists.txt (not
# anything in this repo, and not a README/instructions mistake): it calls
# find_package(Lua REQUIRED), correctly resolving LUA_INCLUDE_DIR, but then only
# ever uses ${LUA_LIBRARIES} (for target_link_libraries) - LUA_INCLUDE_DIR is never
# passed to include_directories()/target_include_directories() anywhere in that
# file. The SWIG-generated wrapper's own `#include "lua.h"` then fails to compile
# ("lua.h: No such file or directory") no matter what CMAKE_PREFIX_PATH is set to,
# since the include path was simply never wired up on the C++ compile side. Passing
# Lua's include directory via CMAKE_CXX_FLAGS directly here sidesteps this without
# patching swig-adol-c's own source - confirmed working against its actual
# CMakeLists.txt. Worth reporting/fixing upstream directly if you have write
# access to that repo; that fix, once it lands, would make this workaround
# redundant but harmless to leave in place.
set -euo pipefail

ROOT="${PIXI_PROJECT_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
ADOLC_PREFIX="$ROOT/build/_deps/ADOL-C/adolc_base"
BUILD_DIR="$ROOT/build/_deps/swig-adol-c/build"

if [ -f "$BUILD_DIR/adtl.so" ]; then
    echo "adtl.so already built at $BUILD_DIR - skipping (delete this directory to force a rebuild)."
    exit 0
fi

if [ ! -f "$ADOLC_PREFIX/include/adolc/adtl.h" ]; then
    echo "ADOL-C not found at $ADOLC_PREFIX - run 'pixi run install_adolc' first." >&2
    exit 1
fi

if [ -z "${CONDA_PREFIX:-}" ]; then
    echo "CONDA_PREFIX is not set - run this via 'pixi run build_swig_adolc', not directly." >&2
    exit 1
fi

ADOLC_LIBDIR_NAME="lib"
[ -d "$ADOLC_PREFIX/lib64" ] && ADOLC_LIBDIR_NAME="lib64"

mkdir -p "$ROOT/build/_deps"
cd "$ROOT/build/_deps"
if [ ! -d swig-adol-c ]; then
    git clone https://gitlab.dlr.de/dlr-sp/occt-differentiation/swig-adol-c.git
fi
cd swig-adol-c
git checkout lua_wrapper

mkdir -p build && cd build
cmake -D ADOLC_INCLUDE_DIR="$ADOLC_PREFIX/include" \
      -D ADOLC_LIB_DIR="$ADOLC_PREFIX/$ADOLC_LIBDIR_NAME" \
      -D CMAKE_PREFIX_PATH="$CONDA_PREFIX" \
      -D CMAKE_CXX_FLAGS="-I$CONDA_PREFIX/include" \
      ..
make

echo "adtl.so built at $BUILD_DIR/adtl.so"
