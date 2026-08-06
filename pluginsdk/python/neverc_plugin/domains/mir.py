"""Machine IR core, analyses, passes, and provider APIs."""

from ._base import interface

CORE = interface("MIR")
ANALYSIS = interface("MIR_ANALYSIS")
PASS = interface("MIR_PASS")
PROVIDER = interface("MIR_PROVIDER")
INTERFACES = (CORE, ANALYSIS, PASS, PROVIDER)

__all__ = ["ANALYSIS", "CORE", "INTERFACES", "PASS", "PROVIDER"]
