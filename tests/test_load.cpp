// SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
//
// SPDX-License-Identifier: MPL-2.0

// Loads adolc_plugin.so via grunk::plugin::load_native and confirms it's reported as
// loaded, that its "adolc" namespace table is visible to plain Lua, and that a real
// adouble round-trips through it (construct, sin, tostring) - a real, if minimal,
// exercise of the whole plugin, not just its own identity. ADOLC_PLUGIN_SO_PATH is set
// by CMakeLists.txt to the built path of the adolc_plugin shared library target.
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

    grunk::PluginInfo info = grunk::plugin::load_native(state, ADOLC_PLUGIN_SO_PATH);

    EXPECT_EQ(info.name, "adolc");

    ASSERT_EQ(state.plugins().size(), 1u);
    EXPECT_EQ(state.plugins()[0].name, "adolc");

    // create_env()'s environment falls back to grunk's *registered-symbol* table
    // (original_env), not to Lua's base library (see grunk/dynamic/state.hpp's
    // create_env doc comment) - so plain Lua globals like `assert` aren't reachable
    // here. Check the namespace table directly via the environment's own indexing
    // instead of evaluating a Lua script that would need `assert`.
    auto env = state.create_env();
    sol::object adolc_ns = env["adolc"];
    EXPECT_EQ(adolc_ns.get_type(), sol::type::table);
}

TEST(Load, AdoubleRoundTripsThroughLua)
{
    grunk::state state;
    grunk::plugin::load_native(state, ADOLC_PLUGIN_SO_PATH);

    // x:__tostring() rather than the Lua base-library global tostring(x) - this
    // env (like PluginLoadsAndReportsItself's own env above) has no base library
    // (see create_env()'s own doc comment), so the global `tostring` doesn't exist
    // here at all; adolc_plugin.cpp's own __index override makes the metamethod
    // itself reachable directly under this key instead (see its doc comment).
    auto env = state.create_env();
    auto res = env.eval(R"(
        x = adolc.adouble.new(2.0)
        x:setADValue(0, 1.0)
        y = adolc.sin(x)
        value = y:getValue()
        derivative = y:getADValue(0)
        text = x:__tostring()
    )");
    ASSERT_TRUE(res.valid());

    // sin(2.0) / cos(2.0) - a plain closed-form check, independent of ADOL-C/grunk
    // entirely, same discipline as grunk-occt's own examples.
    EXPECT_NEAR(env.get<double>("value"), std::sin(2.0), 1e-9);
    EXPECT_NEAR(env.get<double>("derivative"), std::cos(2.0), 1e-9);
    EXPECT_EQ(env.get<std::string>("text"), "adolc.adouble.new(2.0)");
}
