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

## Adopted `GRUNK_PLUGIN_INFO` from the start

`src/adtl_plugin.cpp` uses `GRUNK_PLUGIN_INFO("adtl", "2.7.2")` rather than the
hand-written `grunk_plugin_info()` form the original `cad_autodiff` copy still used -
this repo's own grunk pin already includes that macro
([grunk-dev/grunk#284](https://github.com/grunk-dev/grunk/pull/284)), so there was no
reason to carry the older, more verbose form forward into a brand-new repo.
