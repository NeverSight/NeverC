"""LLVM IR core, builder, analysis, pass, generation, and optimization APIs."""

from ._base import interface

ANALYSIS = interface("IR_ANALYSIS")
BUILDER = interface("IR_BUILDER")
CORE = interface("IR_CORE")
GEN = interface("IR_GEN")
OPTIMIZATION = interface("IR_OPTIMIZATION")
PASS = interface("IR_PASS")
INTERFACES = (ANALYSIS, BUILDER, CORE, GEN, OPTIMIZATION, PASS)

__all__ = [
    "ANALYSIS", "BUILDER", "CORE", "GEN", "INTERFACES", "OPTIMIZATION",
    "PASS",
]
