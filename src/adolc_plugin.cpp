// SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
// SPDX-FileCopyrightText: 2026 Mladen Banocic <mladen.banovic@dlr.de>
//
// SPDX-License-Identifier: MPL-2.0

// Native ADOL-C plugin entry point (see DESIGN.md's "Native adtl::adouble binding,
// generated, not hand-written" for why this replaced the previous SWIG-based one -
// preserved for reference on the swig-adtl-plugin branch, not deleted): links
// directly against ADOL-C's own public header (adolc/adtl.h) and library
// (libadolc), registering adtl::adouble (ADOL-C's own C++ type - not renamed, it's
// upstream) as a real, native sol2 usertype via generated/adouble.cpp
// (register_adouble - see codegen/config.yml). Registered under the PluginInfo
// name "adolc" and Lua-facing class name "adouble" - "adolc" (not "adtl", which
// this plugin's own registration used until it was renamed to match this repo's
// own name) so every existing recipe/script (examples/test_adolc.grr.yml
// included) refers to this plugin the same way the repo itself is named.
//
// This is also the "intermediate ADOL-C plugin" grunk-occt's own AD-enabled build
// (GRUNK_OCCT_ENABLE_AD) depends on: once this plugin registers adtl::adouble via
// grunk's *native* register_type<T>() mechanism, that registration is
// automatically shared (sol2 usertypes are keyed by C++ type identity, not by
// which plugin registered them) with any other native plugin loaded into the same
// grunk::state that also names adtl::adouble/Standard_Adouble - see grunk-occt's
// own DESIGN.md's "Reusing one shared Standard_Real/adtl::adouble registration,
// not two". Load order between this plugin and occt_plugin's own registration
// doesn't matter - only that this plugin is loaded before any AD-enabled OCCT Lua
// code actually *runs*.

#include <grunk/dynamic.hpp>
#include <grunk/plugin.hpp>

#include <adolc/adtl.h>

#include "../generated/adouble.hpp"

#include <string>

namespace {

void register_adolc(grunk::state& state, grunk::PluginInfo const& info)
{
    auto ns = state.begin_plugin(info);
    register_adouble(ns);

    // Hand-written escape hatch - the one thing generate.py has no concept of at
    // all, so it can't emit it: grunk's own recipe-serialization convention
    // (Serializer::ctor_syntax_to_new_feature_syntax) needs a type's own
    // __tostring to round-trip a bare adouble value held directly by a Feature
    // through recipe YAML. Ported directly from the previous SWIG-based plugin's
    // own identical convention (see the swig-adtl-plugin branch) -
    // "adolc.adouble.new(<value>)" - just far simpler to add here: a native sol2
    // usertype's __tostring needs no SWIG-__index bridging at all, unlike the
    // SWIG-Lua userdata the previous version had to work around by hand.
    ns.modify_type<adtl::adouble>("adouble").add_member_function(sol::meta_function::to_string,
        [](adtl::adouble const& self) -> std::string {
            return "adolc.adouble.new(" + grunk::to_string(self.getValue()) + ")";
        }
    );
}

} // anonymous namespace

GRUNK_PLUGIN_INFO("adolc", "2.7.2") // matches the wrapped ADOL-C release

GRUNK_PLUGIN_REGISTER(register_adolc)
