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
// Also the actual reproduction of the recipe-level dispatch gap documented in
// DESIGN.md ("Recipe-level colon-call dispatch on adtl.adouble values"): w:getValue()
// fails inside a recipe step even though the identical call succeeds in
// test_load.cpp's own create_env() (no recipe/dependency-tracking involved there) -
// see AdoubleColonCallFailsInsideRecipe below, and QualifiedCallWorksInsideRecipe for
// the working alternative examples/test_adolc.grr.yml itself now uses.

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

// The negative-path counterpart to the test above: instance:method(...) is the
// idiom every plain create_env()/eval() call in test_load.cpp uses successfully, but
// inside a recipe step it fails for adtl.adouble specifically, because
// register_external_type's auto-discovered methods never stamp a
// DynamicFeature return-type hint (only register_type<T>'s do - see
// grunk/dynamic/usertype_proxy.hpp's set_return_type_hint call, which
// grunk/dynamic/external_type_proxy.hpp has no equivalent of). Documented here as a
// deliberate regression guard: if a future grunk version starts stamping a hint for
// external_type_proxy too, colon-call would start working and this assertion would
// start failing loudly - which is exactly the "worth knowing about" signal wanted
// here, not a bug in this repo.
TEST_F(RecipeTest, AdoubleColonCallFailsInsideRecipe)
{
    auto recipe = state_->create_recipe();
    EXPECT_THROW(
        recipe.eval(R"(
            x = adtl.adouble.new(2.0)
            w = adtl.sin(x)
            w_value = w:getValue()
        )"),
        std::exception
    );
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
