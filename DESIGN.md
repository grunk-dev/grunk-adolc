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

## Native `adtl::adouble` binding, generated, not hand-written

Superseding both "Origin" above (this plugin's own registration mechanism, not just
its build) and the whole SWIG-era discussion below (kept, not deleted, for
historical reference - see "What changed" at the end of this section).

**Why**: the real, driving reason is a long-term goal, not something this repo
needed on its own - AD-enabled workflows where a differentiated `geoml` plugin and
`grunk-occt`'s own AD-enabled `occt_plugin` need to pass `adouble` values to each
other directly. Two independently-registered native sol2 usertypes for "the same"
underlying value can never do that; the SWIG-based `register_external_type`
binding this repo used until now made it worse, not better - a SWIG-Lua userdata
and a native sol2 usertype are different C-level representations entirely, so
bridging *that* would have meant real, fragile, hand-rolled SWIG-runtime interop
code (`SWIG_NewPointerObj`/`SWIG_TypeQuery`) in every native consumer.

**What actually works instead**: sol2 usertypes are keyed by C++ type identity,
shared automatically across every plugin loaded into the same `lua_State` - so one
native `register_type<adtl::adouble>()` call, done here, is automatically visible
to any other native plugin (`grunk-occt`'s own AD-enabled build) that also names
`adtl::adouble`/`Standard_Adouble` (the same C++ type, in adOCCT), with **no
bridging code needed at all**. See `grunk-occt`'s own `DESIGN.md`'s "Reusing one
shared Standard_Real/adtl::adouble registration, not two" for that side of it.

**Generated, not hand-written, via the same generator `grunk-occt` uses**
(`codegen/generate.py` - a deliberate, temporary duplicate; see its own top
comment for why and what the real fix is), pointed at ADOL-C's own public
`adtl.h` (see `codegen/config.yml`) instead of an OCCT header. This needed real
new generator capability (operator overloads, friend free functions - see
`grunk-occt`'s own `codegen/README.md` for the mechanism) and surfaced several
real, previously-latent generator bugs along the way, all fixed in `grunk-occt`'s
copy first and synced here after (see its own `DESIGN.md` for the fixes
themselves - the "reversed operand always exists" false assumption, a template-
specialization check that false-positived on any namespaced class, a
namespace-reopened-twice duplicate-registration bug, and a bare-name-vs-namespace-
scope naming mismatch): none of them were anticipated in advance, all four were
found by actually generating against ADOL-C's real header and fixing what broke,
not by reasoning about the generator's own code in the abstract.

The result (`generated/adouble.cpp`) covers ADOL-C's entire practically-relevant
API surface automatically: the constructor, `getValue`/`setValue`/`getADValue`/
`setADValue`, every arithmetic/comparison operator (`+ - * / == < <=`, both
member and reversed-literal forms, e.g. `x + 5` and `5 + x`), and every math
free function (`sin`, `cos`, `exp`, `pow`, ...) - registered under their own bare
name (`adtl.sin`, not `adtl.adtl_sin`), a distinct naming rule from `grunk-occt`'s
own "static namespace class" convention (`occt.TopoDS_Face`) this needed adding,
since ADOL-C's `adtl` is a genuine C++ namespace, not a class imitating one. Only
`src/adtl_plugin.cpp`'s own `__tostring` registration (grunk's recipe-
serialization convention - see its own comment) remains hand-written: the
generator has no concept of that convention to emit it from, and - unlike the
previous SWIG-based version's own `__tostring` bridging - needs no special
handling at all for a native usertype.

**Real, load-bearing side effect, not just a nice cleanup**: dropping SWIG means
`grunk-occt`'s own AD-enabled build no longer needs to register `Standard_Real`
itself at all - see its `occt_ad_support.hpp`/`register_occt()`'s own defensive
`state.plugins()` check for `"adtl"`, which now throws a clear error instead of
`occt_plugin::to_real()` silently taking its non-AD branch for every call, if
this plugin isn't loaded first.

**What changed, concretely**: `src/adtl_plugin.cpp` was rewritten (from a thin
shim around a precompiled `adtl.so` to a genuine native plugin, linking ADOL-C's
own public headers/library directly); `CMakeLists.txt` dropped `swig-adol-c`/
`ADTL_LIB_DIR` entirely in favor of `pkg_check_modules(ADOLC ...)` (matching
`grunk-occt`'s own `occt_adolc`'s identical approach) plus the new
`codegen/`/`generated/` machinery; `scripts/build_swig_adolc.sh` was deleted (no
longer needed - `scripts/install_adolc.sh` alone is now sufficient). **The
previous SWIG-based version is preserved, not lost**, on the `swig-adtl-plugin`
branch (pushed to `origin`) - kept for reference, e.g. if the Python-bindings-
consistency angle "Relationship to grunk-occt..." below once valued ever becomes
a hard requirement rather than a soft one.

## Relationship to grunk-occt and the future grunk-geoml

Three sibling repos, one per concern, deliberately kept separate rather than one
combined "grunk CAD+AD" project:

- **`grunk-occt`** - OCCT bindings for grunk. No AD dependency at all in its
  default build; its own generated code carries a `GRUNK_OCCT_WITH_ADOLC`-gated
  dual-emission form for every `Standard_Real` parameter (see its own DESIGN.md's
  "Standard_Real / AD-portability rule"), activated by its own
  `GRUNK_OCCT_ENABLE_AD` build option - see its own DESIGN.md for the AD
  validation milestone this plugin is now a real, load-bearing dependency of (not
  just conceptually related), per "Native adtl::adouble binding" above.
- **`grunk-adolc`** (this repo) - the canonical native ADOL-C plugin: a real,
  compile-time dependency on ADOL-C's own public headers/library, zero OCCT/CAD
  dependency of its own. Reusable by *any* grunk-based project wanting plain AD,
  not just a CAD one - genuinely general-purpose, which is exactly why it doesn't
  live inside `grunk-occt` itself, and why its own native `adtl::adouble`
  registration is the one `grunk-occt`'s AD-enabled build depends on rather than
  duplicating.
- **`grunk-geoml`** (deferred, not started) - will mirror
  `cad_autodiff`'s own stages 2/3: a plain `geoml`-based plugin, and an AD-enabled
  counterpart built against `adOCCT` + `geoml`'s own `feature/autodiff` branch,
  demonstrating `grunk-adolc` and a differentiated CAD plugin working together in
  one recipe - `cad_autodiff`'s README (`patches/geoml-feature-autodiff-occt79.patch`,
  the whole "Stage 3" section) documents everything that was actually involved in
  getting that combination to build and produce a correct closed-form-verified
  derivative; that's the starting point when this is picked up, not something to
  re-derive from scratch.

`grunk-occt`'s own AD validation work and `grunk-geoml` are related but distinct:
`grunk-occt` validates *its own* generated registration code against an AD-enabled
OCCT build directly (no geoml involved at all), while `grunk-geoml` is about a
*different* CAD library (geoml, itself layered on OCCT) combined with
`grunk-adolc`. Building this repo doesn't, by itself, validate grunk-occt's own AD
build - that still needs its own AD-enabled OCCT build and validation, independent
of whether `grunk-adolc`/`grunk-geoml` exist.

## Platform scope

Still Linux-only (`pixi.toml`'s `platforms = ["linux-64"]`), unlike `grunk-occt`'s
3-platform policy - but the *original* reason no longer applies: this plugin no
longer depends on `swig-adol-c` (never built/verified anywhere but Linux) at all,
since "Native adtl::adouble binding" above dropped it entirely. ADOL-C itself
builds cross-platform in principle. Widening this is a real, reasonable,
currently out-of-scope follow-up - gated on someone actually verifying ADOL-C's
own autotools-based build on macOS/Windows, not on anything specific to this repo
anymore.

## CI

The *original* reason CI wasn't configured (`swig-adol-c`, DLR-internal, no
public-runner network path without provisioning credentials that don't exist) no
longer applied once "Native adtl::adouble binding" above dropped that dependency -
ADOL-C itself is public (`github.com/coin-or/ADOL-C`), so building it from source
in CI is straightforward, not blocked on anything DLR-internal. `.github/workflows/
ci.yml` (added later, mirroring grunk-occt's own `build-and-test-ad` job almost
verbatim - single Linux job, autotools installed explicitly rather than relying on
the runner image's own undocumented contents, `.deps/adolc` cached across runs
keyed on `scripts/install_adolc.sh`'s own hash) runs `install_adolc` → `configure`
→ `build` → `test` → `run_examples` → `install` on every PR. Verified locally
before being added, not just written and assumed correct: a full clean rebuild
(`rm -rf build`, `pixi run all`) plus `pixi run install` both succeed, matching
what the workflow itself does step by step.

## Build instructions are copy-paste-executable, not prose placeholders

`cad_autodiff`'s own README (the source this repo's build instructions were adapted
from) writes `ADOLC_PREFIX`/`PIXI_ENV` as prose placeholders the reader is expected
to mentally substitute (e.g. "`-DADOLC_LIB_DIR=ADOLC_PREFIX/lib64`" verbatim, not a
real shell variable). Rewritten here as real, exported shell variables
(`$ADOLC_PREFIX`, `$PIXI_ENV`) set once and referenced consistently across both
build steps, meant to be run as one continuous shell session - written back when
there was no CI yet to catch a transcription mistake in a manually-substituted
path (see "CI" above, added later) - the instructions still read this way since
they remain more resilient regardless, not because the original reasoning still
fully applies.

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

## Bootstrap steps automated as a pixi task (`scripts/install_adolc.sh`)

Found while actually working through the README by hand: building and installing ADOL-C doesn't
need to stay a manual copy-paste shell block - it's now `pixi run install_adolc` (a real shell
script under `scripts/`, wired into `pixi.toml`, idempotent - safe to re-run, skips the actual
build if its output already exists). `README.md` still documents the manual, step-by-step version
in a collapsed `<details>` block - not because the script is unreliable, but because working
through what it does by hand is the fastest way to debug it if something upstream (ADOL-C's own
autotools build) breaks in a way the script can't anticipate.

(A second script, `scripts/build_swig_adolc.sh`, existed here for the same reason against
`swig-adol-c` - deleted along with that whole dependency, see "Native adtl::adouble binding,
generated, not hand-written" above; still present, for reference, on the `swig-adtl-plugin`
branch, along with the real upstream `swig-adol-c` `CMakeLists.txt` bug it had to work around - a
`find_package(Lua REQUIRED)` call whose resolved `LUA_INCLUDE_DIR` was never actually passed to
any target, confirmed by reading that file directly, not guessed at. Moot now that this repo no
longer builds against `swig-adol-c` at all, but worth reporting/fixing upstream directly if you
have write access to that repo and still use it elsewhere.)

## `tests/test_recipe.cpp`: adtl_plugin through a real `grunk::recipe`

`tests/test_load.cpp` only ever drives `adtl_plugin` through `create_env()`'s raw, undecorated Lua
- never through `grunk::recipe`'s dependency-tracked `steps:`/`outputs:` layer, the same gap
`grunk-occt` closed with its own `tests/test_recipe.cpp` (see its `DESIGN.md`'s "Test-harness
shape"). Working through this directly (not just reasoning about it) surfaced two real, distinct
gaps:

### `adtl.adouble` colon-call dispatch inside a recipe - fixed by switching to native registration

**Resolved, not just worked around**, since "Native adtl::adouble binding" above - kept here for
the history, since the root cause and the fix are worth understanding together. Originally:
`w:getValue()` on a value born from `adtl.sin(...)` inside `recipe.eval(...)` threw `"Feature: no
static type information available for \"getValue\""`, even though the identical call succeeded in
`test_load.cpp`'s own `create_env()`. Root cause, confirmed by reading grunk's own source (not
guessed at): `DynamicFeature::__index`'s colon-call dispatch (`grunk/dynamic/state.hpp`) only
resolves a method if the feature carries a static type hint, stamped exclusively by
`usertype_proxy::add_constructors`/`add_member_function` (`grunk/dynamic/usertype_proxy.hpp`'s own
`set_return_type_hint` call) for a native `register_type<T>()`-registered type. The *previous*
SWIG-based binding's own `register_external_type`/`external_type_proxy` mechanism
(`grunk/dynamic/external_type_proxy.hpp`) built its constructor/methods via the same
`create_function_meta` helper but never called `set_return_type_hint` anywhere in that file - the
*same symptom* as [grunk-dev/grunk#285](https://github.com/grunk-dev/grunk/issues/285)
(`add_bases<...>()` not walked by colon-call dispatch, filed for `grunk-occt`), but a genuinely
different root cause, filed separately as
[grunk-dev/grunk#286](https://github.com/grunk-dev/grunk/issues/286).

The qualified `Type.method(instance, args...)` form (`adtl.adouble.getValue(w)`) was the
workaround at the time (still used in a few places, e.g.
`QualifiedCallWorksInsideRecipeAndReactsToParameterChanges`, for reasons unrelated to this specific
gap - see the next section). Once this repo switched to native `register_type<adtl::adouble>()`
registration, `usertype_proxy::add_constructors`' own `set_return_type_hint` call runs for every
`adouble`-returning call, so `w:getValue()` and `w:getADValue(0)` now resolve directly - see
`AdoubleColonCallWorksInsideRecipe`, a deliberate regression guard the *other* direction from what
it originally checked. `#286` remains open in grunk itself (a real gap for anything still using
`register_external_type`), it just no longer applies to this type.

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

## Packaging

Mirrors grunk-occt's own identical "Packaging" entry (see its `DESIGN.md`) - `adtl_plugin`
is a runtime-loaded grunk plugin, dlopened by path via `grunk::plugin::load_native`, never linked
against by a consuming application, so `install()` here is just `GNUInstallDirs` +
`install(TARGETS adtl_plugin LIBRARY/RUNTIME/ARCHIVE DESTINATION ...)` - no CMake package config,
since there's no linkable target to export one for. Verified for real: `pixi run install` installs
`build/install/lib/libadtl_plugin.so`, and the full test suite still passes afterward.

**Now reachable from grunk-occt's own `GRUNK_OCCT_ENABLE_AD` build**: grunk-occt's
`FetchContent_Declare(grunk-adolc ...)` pin has since been bumped (to this repo's `233516a`,
which includes this `install()` addition) once this change was committed and pushed here - see
grunk-occt's own `DESIGN.md`, "Packaging", for that side of the story (verified there via a full
clean reconfigure + install + test run, not just a pin-value edit).

## grunk: pre-built dependency, not vendored

This repo used to pull grunk's own *source* in via `FetchContent_Declare(grunk ...)` +
`add_subdirectory`, compiling a private copy of `grunk::core`/`grunk::dynamic`/`grunk::recipe`/
`grunk::plugin` directly into `adtl_plugin`'s own build. That's gone: `CMakeLists.txt` now does a
plain `find_package(grunk REQUIRED)` against a pre-built, pre-installed grunk (`scripts/
install_grunk.cmake`, run automatically via `pixi run configure`'s own `depends-on`, installing
into `.deps/grunk`). Adopted directly from grunk-occt's own identical change, made for the same
reason - see its own `DESIGN.md`, "A related hazard, found outside the AD case" and "grunk:
pre-built dependency, not vendored", for the full story of what problem this solves (two
independently vendored, independently compiled copies of grunk loaded into the same process - this
project's own Python bindings and a native plugin like `adtl_plugin`, say - risk a real ABI/
behavioral mismatch between them; one shared, identically-built grunk everywhere avoids that by
construction).

**Why a `cmake -P` script, not a shell script**: unlike this repo's own `scripts/install_adolc.sh`
(bash, fine since this repo is Linux-only today - see "Platform scope" above), `install_grunk` is
written as a plain CMake script specifically so it's *not* a new obstacle if this repo's own
platform scope ever widens the way "Platform scope" already flags as a reasonable, currently
out-of-scope follow-up - cmake is already a hard dependency on every platform, bash is not
guaranteed on Windows. Adopted from grunk-occt's own identical script (which does need this today,
since its own `default` environment already targets Windows) rather than writing a Linux-only
version now and a cross-platform one later.

**RPATH** (`CMakeLists.txt`'s own `CMAKE_INSTALL_RPATH_USE_LINK_PATH` + `$ORIGIN`/an explicit
`$ENV{CONDA_PREFIX}/lib` entry, mirroring grunk's own identical treatment of itself) is what lets
`adtl_plugin.so` find grunk's shared libraries at runtime without `LD_LIBRARY_PATH` - verified
directly via `ldd`/`readelf` with `LD_LIBRARY_PATH` explicitly unset, on both the build-tree and
`cmake --install`ed artifacts. A `WIN32`-only `POST_BUILD` step (copying `$<TARGET_RUNTIME_DLLS:
adtl_plugin>`/`$<TARGET_RUNTIME_DLLS:runUnitTests>` next to each target, since Windows has no
RPATH equivalent) is also in place, ready for if the platform scope ever widens - not independently
verified on an actual Windows machine in this environment, directly modeled on the identical fix
grunk-occt found necessary for its own targets (and grunk itself, for its own `runUnitTests.exe`/
plugin-fixture `.dll`s).

**CI**: `.github/workflows/ci.yml`'s existing ADOL-C cache step now covers `.deps/grunk` alongside
`.deps/adolc`, keyed on both install scripts together, with its own "Install grunk" step before
"Install ADOL-C".
