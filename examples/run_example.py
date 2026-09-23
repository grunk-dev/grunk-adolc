#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
#
# SPDX-License-Identifier: MPL-2.0

"""Minimal example script (see README.md) demonstrating how to: load the adolc
plugin, read the example recipe, override its seeded parameter, and read back both
the primal value and the forward-mode derivative of its output - verified against a
closed-form value, the same discipline grunk-occt's own examples use.

test_adolc.grr.yml computes `w = adolc.sin((x * y) * 6)`, where `x` is seeded with
AD direction 0 (see its own `me.seed` module) from the parameter `x_val`, and `y` is
the constant `adolc.adouble.new(17)`. In closed form: `w = sin(102 * x_val)`,
`dw/d(x_val) = 102 * cos(102 * x_val)`. The recipe's own steps: block computes and
exposes `w_value`/`w_derivative` as outputs: already (see its own comment) - this
script only overrides the input parameter and reads the two outputs back.

Uses grunk's Python bindings (`pip install grunk`, or `pixi run install_python` from
the grunk repo) rather than C++, since most grunk users script recipes from Python -
see grunk-occt's own examples/run_example.py for the same choice and reasoning.

Usage:
    python run_example.py /path/to/libadolc_plugin.so
"""

import math
import sys
from pathlib import Path

import grunk

EXAMPLES_DIR = Path(__file__).resolve().parent


def main() -> None:
    if len(sys.argv) != 2:
        sys.exit(f"usage: {sys.argv[0]} /path/to/libadolc_plugin.so")
    adolc_plugin_path = sys.argv[1]

    state = grunk.state()
    grunk.plugin.load_native(state, adolc_plugin_path)

    recipe = state.read(str(EXAMPLES_DIR / "test_adolc.grr.yml"))

    x_val = 3.0
    recipe.get_feature("x_val").set_value(x_val)
    print(f"overrode x_val = {x_val}")

    # test_adolc.grr.yml's own steps: already compute w_value/w_derivative (via the
    # qualified adolc.adouble.getValue(w)/getADValue(w, 0) form - see its own comment
    # and DESIGN.md for why not w:getValue()) and declare all three as outputs:, so
    # reading them back here is just get_output(...) - no extra recipe.eval() needed.
    value = recipe.get_output("w_value").value().as_float()
    derivative = recipe.get_output("w_derivative").value().as_float()

    expected_value = math.sin(102.0 * x_val)
    expected_derivative = 102.0 * math.cos(102.0 * x_val)

    print(f"w = {value} (expected {expected_value})")
    print(f"dw/d(x_val) = {derivative} (expected {expected_derivative})")

    if not math.isclose(value, expected_value, abs_tol=1e-9):
        sys.exit(f"value mismatch: got {value}, expected {expected_value}")
    if not math.isclose(derivative, expected_derivative, abs_tol=1e-9):
        sys.exit(f"derivative mismatch: got {derivative}, expected {expected_derivative}")

    print("OK: value and derivative match the closed form.")


if __name__ == "__main__":
    main()
