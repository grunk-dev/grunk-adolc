<!--
SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>

SPDX-License-Identifier: MPL-2.0
-->

# grunk-adolc

A [grunk](https://github.com/grunk-dev/grunk) plugin exposing
[ADOL-C](https://github.com/coin-or/ADOL-C)'s tapeless `adouble` algorithmic
differentiation (AD) type as `adtl.adouble` - so a value can flow through a grunk
recipe (dependency tracking, lazy evaluation, YAML serialization) and still carry
its AD derivative. See `DESIGN.md` for how this repo relates to `grunk-occt` (a
sibling repo, OCCT bindings for grunk) and the deferred, future `grunk-geoml`.

Extracted from grunk's own `examples/cpp/cad_autodiff` example (see `DESIGN.md`) -
this plugin has no OCCT/CAD dependency of its own; it's a thin, standalone shim
around a compiled Lua module wrapping ADOL-C, nothing more.

## Prerequisites

- CMake >= 3.15
- C++17 compiler
- Autotools (`autoreconf`/`automake`/`libtool`) - needed to build ADOL-C itself below;
  not needed for anything else in this repo
- [pixi](https://pixi.sh/)
- Access to `swig-adol-c` (DLR-internal GitLab - see below)

### 1. Build and install ADOL-C 2.7.2

```bash
pixi install
pixi run install_adolc
```

Downloads, builds, and installs ADOL-C 2.7.2 into `build/_deps/ADOL-C/adolc_base`
(`scripts/install_adolc.sh` - idempotent, safe to re-run). Needs
`autoreconf`/`automake`/`libtool` on `PATH`.

**Where ADOL-C actually installs its library** (`lib` vs. `lib64`) depends on your
distro's own autotools defaults - step 2 below detects this itself, but if you ever
need to know: `ls build/_deps/ADOL-C/adolc_base/lib*`.

<details>
<summary>What this does, if you'd rather run it by hand (or it fails partway)</summary>

```bash
mkdir -p build/_deps && cd build/_deps
git clone https://github.com/coin-or/ADOL-C.git
cd ADOL-C
git checkout releases/2.7.2

mkdir adolc_base
export ADOLC_PREFIX="$(pwd)/adolc_base"   # used by swig-adol-c's own cmake, step 2 below

autoreconf -fi
./configure --prefix="$ADOLC_PREFIX" --enable-atrig-erf --with-boost=no
make -j
make install

cd ../../..   # back to this repo's own root
```

</details>

### 2. Build the grunk/ADOL-C Lua interface (`adtl.so`)

`swig-adol-c` (a SWIG interface generating the `adtl` Lua module from ADOL-C) is
**DLR-internal** - `https://gitlab.dlr.de/dlr-sp/occt-differentiation/swig-adol-c`,
branch `lua_wrapper`. You need DLR network/credential access to clone it; this is
the reason this repo has no CI configured (see `DESIGN.md`).

```bash
pixi run build_swig_adolc
```

Clones `swig-adol-c` and builds it against step 1's ADOL-C install
(`scripts/build_swig_adolc.sh` - idempotent, safe to re-run), producing `adtl.so` in
`build/_deps/swig-adol-c/build` (the default `ADTL_LIB_DIR` `pixi run configure`
passes - override it, e.g. `pixi run configure Release <path>`, if yours ends up
elsewhere - see `pixi.toml`'s `configure` task).

This script also works around a real bug in `swig-adol-c`'s own `CMakeLists.txt`:
it resolves Lua's include directory via `find_package(Lua REQUIRED)` but never
actually adds it to any target's include path (only `${LUA_LIBRARIES}`, for
linking, is used) - so a plain `cmake`/`make` following its own README fails with
`lua.h: No such file or directory` regardless of `CMAKE_PREFIX_PATH`, confirmed by
reading its `CMakeLists.txt` directly. The script passes Lua's include directory
via `CMAKE_CXX_FLAGS` instead, sidestepping this without patching `swig-adol-c`
itself - see the script's own comment. Worth fixing upstream directly if you have
write access to that repo; harmless to leave this workaround in place either way.

<details>
<summary>What this does, if you'd rather run it by hand (or it fails partway)</summary>

```bash
export ADOLC_PREFIX="$(pwd)/build/_deps/ADOL-C/adolc_base"
export PIXI_ENV="$(pwd)/.pixi/envs/default"

mkdir -p build/_deps && cd build/_deps
git clone https://gitlab.dlr.de/dlr-sp/occt-differentiation/swig-adol-c.git
cd swig-adol-c
git checkout lua_wrapper

mkdir build && cd build
cmake -D ADOLC_INCLUDE_DIR="$ADOLC_PREFIX/include" \
      -D ADOLC_LIB_DIR="$ADOLC_PREFIX/lib64" \
      -D CMAKE_PREFIX_PATH="$PIXI_ENV" \
      -D CMAKE_CXX_FLAGS="-I$PIXI_ENV/include" \
      ..
make

cd ../../../..   # back to this repo's own root
```

Replace `lib64` above with `lib` if that's what ADOL-C's own install used - see
step 1.

</details>

**Runtime note:** `adtl_plugin.so` links directly against `adtl.so`, which in turn
depends on ADOL-C's own shared library (`libadolc.so`, in `$ADOLC_PREFIX/lib` or
`lib64`) at *runtime*, not just at link time. Unless `swig-adol-c`'s own build
already embeds an RPATH pointing there (not verified from this repo - inspect with
`ldd build/_deps/swig-adol-c/build/adtl.so` after step 2 if in doubt), every command
that actually loads the plugin needs `libadolc.so` on the runtime linker path -
skipping this fails the plugin's `dlopen` outright with a "cannot open shared object
file: libadolc.so" error, not a subtler, silent problem (unlike the stock-vs-AD-OCCT
SONAME collision hazard `cad_autodiff`'s own README documents for its "Stage 3" -
there is no second, unrelated ADOL-C build anywhere in this repo's own process for
the wrong one to be silently substituted with).

**If ADOL-C was installed exactly where step 1 above put it**
(`build/_deps/ADOL-C/adolc_base`, relative to this repo's own root), `pixi.toml`
already sets `LD_LIBRARY_PATH` for every `pixi run` task (`$PIXI_PROJECT_ROOT`-based,
see its own comment there) - nothing further to do, `pixi run test`/`run_examples`
below already work. Installed ADOL-C somewhere else, or running `run_example.py`/
`runUnitTests` directly rather than through `pixi run`? Export it yourself first:

```bash
export LD_LIBRARY_PATH="$ADOLC_PREFIX/lib64:$LD_LIBRARY_PATH"   # or lib, matching step 1
```

## Building and running

```bash
pixi run all
```

Or step by step:

```bash
pixi run install_adolc      # step 1 above, if not already done
pixi run build_swig_adolc   # step 2 above, if not already done
pixi run configure          # configure the CMake build (see pixi.toml for the ADTL_LIB_DIR arg)
pixi run build              # build adtl_plugin and the test harness
pixi run test               # run the gtest suite (tests/test_load.cpp)
pixi run run_examples       # run examples/run_example.py against the built plugin
```

## Examples

`examples/test_adolc.grr.yml` is a small, self-contained recipe (`uses: adtl:
2.7.2`, a recipe-local `modules:` block for its own `me.seed` helper - no external
plugin/module dependency beyond `adtl` itself) computing `w = adtl.sin((x * y) *
6)` with `x` seeded for forward-mode AD from a parameter. `examples/run_example.py`
loads the plugin, overrides the seed parameter, and checks both the resulting
value and derivative against the closed form (`sin(102*x_val)`,
`102*cos(102*x_val)`) - see its own doc comment.

## Dependencies

| Dependency | Source |
| -- | -- |
| ADOL-C | [GitHub](https://github.com/coin-or/ADOL-C), built from source |
| swig-adol-c | [DLR-internal GitLab](https://gitlab.dlr.de/dlr-sp/occt-differentiation/swig-adol-c), built from source |
| grunk | FetchContent from GitHub, pinned commit (see `CMakeLists.txt`) |
| libparametric, yaml-cpp, sol2, lua, taskflow | conda-forge |
