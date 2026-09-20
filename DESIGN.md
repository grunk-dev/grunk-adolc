<!--
SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>

SPDX-License-Identifier: MPL-2.0
-->

# Design decisions

This file tracks decisions made setting up this repo that aren't obvious from reading
the code, so they don't need re-litigating later - see `grunk-occt/DESIGN.md` for the
same convention in the sibling repo this one was extracted alongside.

## Origin

Extracted from grunk's own `examples/cpp/cad_autodiff` example (branch
`cad-autodiff-example-split`, commit `91c5c69`) - specifically its "stage 1" (AD
only, no CAD): `plugins/adtl/adtl_plugin.cpp` (moved to `src/adtl_plugin.cpp`,
comments updated to drop "stage 1"/geoml-adjacent framing since this repo only ever
has the one stage) and `test_adolc.grr.yml` (moved to `examples/`, unchanged - it
already has zero geoml/OCCT dependency, `uses: adtl: 2.7.2` only).

**What was deliberately left behind, and why**: `occt_sol_traits.hpp` (byte-identical
to `grunk-occt`'s own copy already - it's the generic `Handle(T)` sol2 trait, not
adtl-specific, and adtl has no `Handle(T)` types to need it for);
`geoml_registration.hpp`/`geoml_plugin.cpp`/`geoml_adolc/`/the patch file (all
genuinely geoml/adOCCT-specific - see "Relationship to grunk-occt and the future
grunk-geoml" below); the original example's combined `CMakeLists.txt`/`main.cpp`
(mixed all three stages, not a straight copy - this repo's own `CMakeLists.txt`/
`examples/run_example.py` are written fresh, scoped to stage 1 only, following
`grunk-occt`'s own established scaffold conventions instead of the original
example's `main()`-runs-everything shape).

`src/adtl_plugin.cpp` itself needed no functional changes beyond comments - its own
`#include` list (`grunk/grunk.hpp`, `sol/types.hpp`) already confirmed it never
touched OCCT/geoml at all.

## Relationship to grunk-occt and the future grunk-geoml

Three sibling repos, one per concern, deliberately kept separate rather than one
combined "grunk CAD+AD" project:

- **`grunk-occt`** - OCCT bindings for grunk. No AD dependency at all today; its own
  generated code already carries a `GRUNK_OCCT_WITH_ADOLC`-gated dual-emission form
  for every `Standard_Real` parameter (see its own DESIGN.md's "Standard_Real /
  AD-portability rule"), unexercised until Step 7 of its migration plan actually
  builds against an AD-enabled OCCT (`adOCCT`).
- **`grunk-adolc`** (this repo) - a standalone ADOL-C plugin, zero OCCT/CAD
  dependency. Reusable by *any* grunk-based project wanting plain AD, not just a
  CAD one - genuinely general-purpose, which is exactly why it doesn't live inside
  `grunk-occt` itself.
- **`grunk-geoml`** (deferred, not started) - will mirror
  `cad_autodiff`'s own stages 2/3: a plain `geoml`-based plugin, and an AD-enabled
  counterpart built against `adOCCT` + `geoml`'s own `feature/autodiff` branch,
  demonstrating `grunk-adolc` and a differentiated CAD plugin working together in
  one recipe - `cad_autodiff`'s README (`patches/geoml-feature-autodiff-occt79.patch`,
  the whole "Stage 3" section) documents everything that was actually involved in
  getting that combination to build and produce a correct closed-form-verified
  derivative; that's the starting point when this is picked up, not something to
  re-derive from scratch.

`grunk-occt`'s own Step 7 (AD validation milestone) and `grunk-geoml` are related but
distinct: Step 7 validates *grunk-occt's own* generated registration code against an
AD-enabled OCCT build directly (no geoml involved at all), while `grunk-geoml` is
about a *different* CAD library (geoml, itself layered on OCCT) combined with
`grunk-adolc`. Building this repo doesn't, by itself, complete grunk-occt's Step 7 -
that still needs its own AD-enabled OCCT build and validation, independent of
whether `grunk-adolc`/`grunk-geoml` exist.

## Platform scope

Linux-only (`pixi.toml`'s `platforms = ["linux-64"]`), unlike `grunk-occt`'s 3-platform
policy. This mirrors the original migration plan's own accepted exception for
AD-enabled work ("may stay Linux-only/best-effort given how involved adOCCT/ADOL-C's
own build already is") - `swig-adol-c`, this plugin's own load-bearing dependency,
has only ever been built/verified on Linux, and there's no reason to speculatively
widen platform scope for a dependency that's never been tried anywhere else.

## No CI configured (yet)

`swig-adol-c` (see README.md) is hosted on a **DLR-internal GitLab instance** - a
public CI runner (GitHub Actions or similar) has no network path to clone it without
provisioning credentials (a deploy key/PAT as a repo secret) that don't exist yet.
Rather than commit a CI workflow that would just fail on every push (worse than no
CI - a permanently-red check nobody can act on), this is left undone and flagged
here rather than silently skipped. Setting up CI for this repo is a real, separate
follow-up task, gated on deciding how (or whether) to give a public runner
DLR-internal network/credential access - not attempted as part of this extraction.

## Build instructions are copy-paste-executable, not prose placeholders

`cad_autodiff`'s own README (the source this repo's build instructions were adapted
from) writes `ADOLC_PREFIX`/`PIXI_ENV` as prose placeholders the reader is expected
to mentally substitute (e.g. "`-DADOLC_LIB_DIR=ADOLC_PREFIX/lib64`" verbatim, not a
real shell variable). Rewritten here as real, exported shell variables
(`$ADOLC_PREFIX`, `$PIXI_ENV`) set once and referenced consistently across both
build steps, meant to be run as one continuous shell session - since there's no CI
to catch a transcription mistake in a manually-substituted path (see "No CI
configured" above), the instructions themselves need to be more resilient than they
would in a repo where a broken README would at least fail loudly in CI first.

Two real gaps found and fixed while doing this, neither present as such in the
original (a straight copy would have carried them forward):
- **`lib` vs `lib64`**: ADOL-C's own `make install` puts `libadolc.so` in whichever
  one its distro's autotools defaults pick - not reliably one or the other. The
  README now has the reader check with `ls "$ADOLC_PREFIX"/lib*` before continuing,
  rather than assuming `lib64` (what the original hardcoded) is universally correct.
- **Runtime linking was undocumented entirely.** `adtl_plugin.so` → `adtl.so` →
  `libadolc.so` is a real runtime `dlopen` dependency chain, not just a link-time
  one - without `libadolc.so` on the runtime linker path, loading the plugin fails
  outright. `cad_autodiff`'s own README only documents this class of hazard for its
  "Stage 3" (adOCCT), never for the (structurally identical) adtl.so/libadolc.so
  case stage 1 already had - most likely because `main()` there runs all three
  stages in one process and only the stage 3 author noticed. Fixed two ways: `pixi.
  toml`'s own `LD_LIBRARY_PATH` (via `$PIXI_PROJECT_ROOT`, so it works from any cwd)
  covers the recommended install path automatically for every `pixi run` task;
  README.md documents the manual `export` fallback for anyone who installed ADOL-C
  somewhere else, or runs `run_example.py`/`runUnitTests` directly rather than
  through `pixi run`.

## Bootstrap steps automated as pixi tasks (`scripts/install_adolc.sh`, `scripts/build_swig_adolc.sh`)

Found while actually working through the README by hand: building and installing ADOL-C, and
cloning/building `swig-adol-c`, don't need to stay manual copy-paste shell blocks - both are now
`pixi run install_adolc` / `pixi run build_swig_adolc` (real shell scripts under `scripts/`, wired
into `pixi.toml`, both idempotent - safe to re-run, skip the actual build if their output already
exists). `README.md` still documents the manual, step-by-step version of each in a collapsed
`<details>` block - not because the scripts are unreliable, but because working through what a
script does by hand is the fastest way to debug it when something upstream (ADOL-C's own autotools
build, `swig-adol-c`'s own CMake) breaks in a way the script can't anticipate.

Running these by hand is also how the Lua-include bug below was actually found and confirmed, not
guessed at from reading `swig-adol-c`'s `CMakeLists.txt` alone.

## `swig-adol-c`'s own `CMakeLists.txt` never adds Lua's include directory to any target

A real upstream bug, confirmed against `swig-adol-c`'s actual `CMakeLists.txt` (not this repo's
code, and not a mistake in this repo's own README/instructions): it calls `find_package(Lua
REQUIRED)`, which correctly resolves `LUA_INCLUDE_DIR`, but only ever consumes
`${LUA_LIBRARIES}` afterward (in `target_link_libraries(adtl adolc ${LUA_LIBRARIES})`) -
`LUA_INCLUDE_DIR` is never passed to `include_directories()`/`target_include_directories()`
anywhere in that file. The build fails compiling the SWIG-generated wrapper itself
(`adoubleLUA_wrap.cxx:772:10: fatal error: lua.h: No such file or directory`) regardless of
`CMAKE_PREFIX_PATH` - the include path for the actual C++ compile step was simply never wired up in
the first place, so there is no `CMAKE_PREFIX_PATH`/`find_package` invocation on our side that
could have avoided it.

`scripts/build_swig_adolc.sh` works around it by passing Lua's include directory explicitly via
`-D CMAKE_CXX_FLAGS="-I$CONDA_PREFIX/include"` at configure time - verified end-to-end (clean
clone through a successful `adtl.so` build) rather than reasoned about from the `CMakeLists.txt`
alone. This is a workaround, not a fix: it does not touch `swig-adol-c`'s own source, so it needs
to keep being applied at every configure until (if ever) `swig-adol-c` adds the missing
`include_directories(${LUA_INCLUDE_DIR})` call itself upstream - worth reporting/fixing directly if
you have write access to that repo.

## `tests/test_recipe.cpp`: adtl_plugin through a real `grunk::recipe`

`tests/test_load.cpp` only ever drives `adtl_plugin` through `create_env()`'s raw, undecorated Lua
- never through `grunk::recipe`'s dependency-tracked `steps:`/`outputs:` layer, the same "add a
recipe-level test later" gap `grunk-occt`'s own migration plan flagged and has now also closed (see
its `DESIGN.md`'s "Test-harness shape"). Working through this directly (not just reasoning about it)
surfaced two real, distinct gaps - both are hazards for *any* SWIG/`register_external_type`-bridged
plugin, not anything specific to ADOL-C:

### `adtl.adouble` colon-call dispatch fails inside a recipe (but not in a plain `env.eval`)

`w:getValue()` on a value born from `adtl.sin(...)` inside `recipe.eval(...)` throws `"Feature: no
static type information available for \"getValue\""`, even though the identical call succeeds in
`test_load.cpp`'s own `create_env()`. Root cause, confirmed by reading grunk's own source (not
guessed at): `DynamicFeature::__index`'s colon-call dispatch (`grunk/dynamic/state.hpp`) only
resolves a method if the feature carries a static type hint, stamped exclusively by
`usertype_proxy::add_constructors`/`add_member_function` (`grunk/dynamic/usertype_proxy.hpp`'s own
`set_return_type_hint` call) for a native `register_type<T>()`-registered type.
`register_external_type`'s own `external_type_proxy` (`grunk/dynamic/external_type_proxy.hpp`) -
which is what bridges `adtl.adouble`, a SWIG-Lua type with no compile-time C++ type sol2 can see -
builds its constructor/methods via the same `create_function_meta` helper but never calls
`set_return_type_hint` anywhere in that file. This is the *same symptom* as
[grunk-dev/grunk#285](https://github.com/grunk-dev/grunk/issues/285) (`add_bases<...>()` not walked
by colon-call dispatch, filed for `grunk-occt`), but a genuinely different root cause - filed
separately as [grunk-dev/grunk#286](https://github.com/grunk-dev/grunk/issues/286).

Workaround, used throughout `examples/test_adolc.grr.yml` and `tests/test_recipe.cpp`: the
qualified `Type.method(instance, args...)` form (`adtl.adouble.getValue(w)`) instead of
`w:getValue()` - the same form grunk's own `tests/cpp/recipe_plugin.cpp` already establishes for a
hand-registered C++ plugin type (`cppplugin.Length.get_x(l)`), so this isn't a novel pattern, just
one this repo hadn't needed until now.

### Mutating calls inside `steps:` may silently never run under lazy evaluation

A more subtle finding, worth its own heading: even the *qualified*-call workaround above does not
fix a **mutating** method called for its side effect alone. `adtl.adouble.setADValue(x, 0, 1.0)`
placed directly in `steps:` compiles and runs with no error, but the seeded AD direction is silently
lost - `getADValue` on anything downstream reads back `0`, as if the call never happened.

Root cause (`grunk/dynamic/ActionDynamic.hpp`'s `eval()`): every `steps:` statement becomes its own
lazily-evaluated DAG node (`ActionDynamic`), and `eval()` only ever writes back the call's own
*return* value (`output->set_value(res[0])`) to that node's own feature - there is no concept of "this
call also mutated one of its input arguments in place." Combined with grunk's lazy, pull-based
evaluation (a node only runs when something downstream actually reads its value), a call whose
return value nothing ever reads and which nothing depends on - exactly `setADValue`'s own case, since
its meaningful effect is the mutation, not its return value - may simply never be scheduled to run at
all.

Fixed the same way `examples/test_adolc.grr.yml`'s own `me.seed` module already did, independently,
before this was understood this precisely: do the mutating call inside a `Recipe::insert_module_script`
module. A module function's *body* runs as a single, eagerly-executed plain Lua call (not a graph of
individually-lazy statements) - only the module call *as a whole* is tracked, via its final return
value - so the mutation reliably happens before that return value is ever read.

This is not an ADOL-C-specific or even `register_external_type`-specific hazard: it applies equally
to any mutating/out-parameter-style call placed directly in `steps:`, regardless of how the type
being mutated is registered. `grunk-occt`'s own `DESIGN.md` ("Non-const-reference (\"out\")
parameters are supported, deliberately") had already flagged this exact concern for OCCT's own
out-params as a theoretical, untested-so-far risk before this repo's `tests/test_recipe.cpp`
independently reproduced it for a completely unrelated type - now cross-referenced from both sides.

## Adopted `GRUNK_PLUGIN_INFO` from the start

`src/adtl_plugin.cpp` uses `GRUNK_PLUGIN_INFO("adtl", "2.7.2")` rather than the
hand-written `grunk_plugin_info()` form the original `cad_autodiff` copy still used -
this repo's own grunk pin already includes that macro
([grunk-dev/grunk#284](https://github.com/grunk-dev/grunk/pull/284)), so there was no
reason to carry the older, more verbose form forward into a brand-new repo.
