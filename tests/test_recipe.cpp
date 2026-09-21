// SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
//
// SPDX-License-Identifier: MPL-2.0

// Exercises adtl_plugin through a real grunk::recipe (parameters:/steps:/outputs:,
// dependency tracking, reactive re-evaluation on set_value) rather than
// test_load.cpp's raw create_env()/eval() harness, which never goes through the
// recipe/parametric layer at all. This is the class of test the migration plan
// deferred ("one true grunk::recipe-based end-to-end test comes later") and never
// actually added for adtl specifically until now - examples/run_example.py exercises
// the recipe layer too, but only via the Python bindings and only end-to-end, not as
// a fast, always-run C++ unit test.
//
// Also confirms a real, previously-open question is now closed: colon-call
// dispatch (w:getValue()) inside a recipe step used to fail for adtl.adouble
// specifically, back when it was registered via SWIG/register_external_type (see
// grunk-dev/grunk#286 and DESIGN.md's "Native adtl::adouble binding, generated,
// not hand-written") - AdoubleColonCallWorksInsideRecipe below is a deliberate
// regression guard the other direction from what it used to be: this repo's own
// switch to native register_type<adtl::adouble>() registration means
// usertype_proxy::add_constructors' own set_return_type_hint call now runs for
// every adouble-returning call, so the recipe layer's own DynamicFeature type-hint
// dispatch has everything it needs - #286 simply doesn't apply to this type
// anymore. QualifiedCallWorksInsideRecipeAndReactsToParameterChanges below still
// uses the qualified form anyway, since that test is really about the *separate*
// lazy-evaluation hazard (see its own comment), not about colon-call dispatch.

#include <grunk/dynamic.hpp>
#include <grunk/plugin.hpp>
#include <grunk/recipe.hpp>

#include <gtest/gtest.h>

#include <cmath>

namespace {

class RecipeTest : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        state_ = new grunk::state();
        grunk::plugin::load_native(*state_, ADTL_PLUGIN_SO_PATH);
    }

    static void TearDownTestSuite()
    {
        delete state_;
        state_ = nullptr;
    }

    static grunk::state* state_;
};

grunk::state* RecipeTest::state_ = nullptr;

} // namespace

// The qualified "Type.method(instance, args...)" form - not instance:method(...) -
// works for an adtl.adouble value produced inside a recipe step, and the result is
// itself a tracked, dependency-aware feature: overriding the seed parameter and
// re-reading the same output reflects the new value, exactly like a plain numeric
// feature would (see grunk's own tests/cpp/recipe_plugin.cpp, which establishes this
// same qualified-call pattern for a hand-registered C++ plugin type).
//
// Seeding the AD direction (x:setADValue(0, 1.0)) has to happen inside a recipe
// *module* (me.seed below - matching examples/test_adolc.grr.yml's own me.seed
// exactly), not as a bare qualified call in steps: directly. A steps: statement
// becomes a lazily-evaluated DAG node (grunk::ActionDynamic) whose own eval() writes
// back only its *return* value (see ActionDynamic::eval's res[0]) - it is never
// triggered at all unless something downstream actually reads that return value.
// adtl.adouble.setADValue(x, 0, 1.0)'s call node's own result is never assigned to
// anything and nothing depends on it, so under lazy evaluation it may simply never
// run - confirmed empirically: it compiles without error, but the seeded direction
// is silently lost (getADValue reads back 0). A module script sidesteps this
// entirely by running as a single, eagerly-executed Lua function body, not a graph
// of individually-lazy statements. This is the same open question grunk-occt's own
// test_geom.cpp OutParamMutatesInPlace test flags for OCCT's own out-parameters
// ("the module-wrapping pattern only matters once this kind of call needs to happen
// inside a tracked recipe... deferred") - this is that hazard, now confirmed
// reproducible rather than just theoretical.
TEST_F(RecipeTest, QualifiedCallWorksInsideRecipeAndReactsToParameterChanges)
{
    auto recipe = state_->create_recipe();
    recipe.insert_module_script("me", R"(
        function seed(a)
            local x = adtl.adouble.new(a)
            x:setADValue(0, 1.0)
            return x
        end
    )");
    recipe.eval(R"(
        x_val = grunk.feature(2.0)
        x = me.seed(x_val)
        w = adtl.sin(x)
        w_value = adtl.adouble.getValue(w)
        w_derivative = adtl.adouble.getADValue(w, 0)
    )");

    auto w_value = recipe.get_feature("w_value");
    auto w_derivative = recipe.get_feature("w_derivative");

    EXPECT_NEAR(w_value.value().as<double>(), std::sin(2.0), 1e-9);
    EXPECT_NEAR(w_derivative.value().as<double>(), std::cos(2.0), 1e-9);

    // Reactive re-evaluation: changing the seed parameter must flow through
    // adtl.sin/getValue/getADValue exactly as it would for any other feature chain.
    recipe.get_feature("x_val").set_value(1.0);
    EXPECT_NEAR(w_value.value().as<double>(), std::sin(1.0), 1e-9);
    EXPECT_NEAR(w_derivative.value().as<double>(), std::cos(1.0), 1e-9);
}

// instance:method(...) is the idiom every plain create_env()/eval() call in
// test_load.cpp already used successfully; this confirms it *also* works inside a
// tracked recipe step now, which it didn't back when adtl.adouble was registered
// via SWIG/register_external_type (see this file's own top comment and
// grunk-dev/grunk#286) - a deliberate regression guard the other way round from
// what this test used to check: if a future change to this repo's own
// registration ever regresses back to something that doesn't stamp a
// DynamicFeature return-type hint, colon-call dispatch would start failing again,
// and this is exactly the test that would catch it.
TEST_F(RecipeTest, AdoubleColonCallWorksInsideRecipe)
{
    auto recipe = state_->create_recipe();
    // x:setADValue(0, 1.0) has to happen inside a module, not as a bare colon-call
    // in steps: directly - the same lazy-evaluation hazard
    // QualifiedCallWorksInsideRecipeAndReactsToParameterChanges's own comment
    // documents (a mutating call's own DAG node never runs if nothing reads its
    // result). Unrelated to what *this* test is actually about (colon-call
    // dispatch on the *read* side - w:getValue()/w:getADValue(0) below, called
    // directly in steps: with no module needed, since reading doesn't have this
    // problem).
    recipe.insert_module_script("me", R"(
        function seed(a)
            local x = adtl.adouble.new(a)
            x:setADValue(0, 1.0)
            return x
        end
    )");
    recipe.eval(R"(
        x = me.seed(2.0)
        w = adtl.sin(x)
        w_value = w:getValue()
        w_derivative = w:getADValue(0)
    )");

    EXPECT_NEAR(recipe.get_feature("w_value").value().as<double>(), std::sin(2.0), 1e-9);
    EXPECT_NEAR(recipe.get_feature("w_derivative").value().as<double>(), std::cos(2.0), 1e-9);
}

// outputs: is the recipe-level mechanism for marking specific features as this
// recipe's public results (see grunk/recipe/Recipe.hpp's insert_output/get_output) -
// examples/test_adolc.grr.yml declares w/w_value/w_derivative this way; this test
// exercises the same mechanism directly against a recipe built via eval() rather
// than read from a file, keeping it independent of that YAML file's own content.
TEST_F(RecipeTest, ExplicitOutputIsReadableViaGetOutput)
{
    auto recipe = state_->create_recipe();
    recipe.eval(R"(
        x = adtl.adouble.new(3.0)
        w = adtl.sin(x)
        w_value = adtl.adouble.getValue(w)
    )");
    recipe.insert_output("result", "w_value");

    EXPECT_NEAR(recipe.get_output("result").value().as<double>(), std::sin(3.0), 1e-9);
}
