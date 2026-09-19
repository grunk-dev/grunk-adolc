// SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
//
// SPDX-License-Identifier: MPL-2.0

// Loads adtl_plugin.so via grunk::plugin::load_native and confirms it's reported as
// loaded, that its "adtl" namespace table is visible to plain Lua, and that a real
// adouble round-trips through it (construct, sin, tostring) - a real, if minimal,
// exercise of the whole plugin, not just its own identity. ADTL_PLUGIN_SO_PATH is set
// by CMakeLists.txt to the built path of the adtl_plugin shared library target.
//
// Requires ADOL-C + swig-adol-c to already be built and pointed at via ADTL_LIB_DIR
// at configure time (see README.md) - there is no way to run this test without them,
// same as grunk-occt's own test harness requires a built OCCT.

#include <grunk/dynamic.hpp>
#include <grunk/plugin.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <string>

TEST(Load, PluginLoadsAndReportsItself)
{
    grunk::state state;

    grunk::PluginInfo info = grunk::plugin::load_native(state, ADTL_PLUGIN_SO_PATH);

    EXPECT_EQ(info.name, "adtl");

    ASSERT_EQ(state.plugins().size(), 1u);
    EXPECT_EQ(state.plugins()[0].name, "adtl");

    // create_env()'s environment falls back to grunk's *registered-symbol* table
    // (original_env), not to Lua's base library (see grunk/dynamic/state.hpp's
    // create_env doc comment) - so plain Lua globals like `assert` aren't reachable
    // here. Check the namespace table directly via the environment's own indexing
    // instead of evaluating a Lua script that would need `assert`.
    auto env = state.create_env();
    sol::object adtl_ns = env["adtl"];
    EXPECT_EQ(adtl_ns.get_type(), sol::type::table);
}

TEST(Load, AdoubleRoundTripsThroughLua)
{
    grunk::state state;
    grunk::plugin::load_native(state, ADTL_PLUGIN_SO_PATH);

    auto env = state.create_env();
    auto res = env.eval(R"(
        x = adtl.adouble.new(2.0)
        x:setADValue(0, 1.0)
        y = adtl.sin(x)
        value = y:getValue()
        derivative = y:getADValue(0)
        text = tostring(x)
    )");
    ASSERT_TRUE(res.valid());

    // sin(2.0) / cos(2.0) - a plain closed-form check, independent of ADOL-C/grunk
    // entirely, same discipline as grunk-occt's own examples.
    EXPECT_NEAR(env.get<double>("value"), std::sin(2.0), 1e-9);
    EXPECT_NEAR(env.get<double>("derivative"), std::cos(2.0), 1e-9);
    EXPECT_EQ(env.get<std::string>("text"), "adtl.adouble.new(2.0)");
}
