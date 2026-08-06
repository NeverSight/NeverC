"""Assembly provider, MC graph/mutation, emission, and provider APIs."""

from ._base import interface

ASSEMBLY_PROVIDER = interface("ASSEMBLY_PROVIDER")
CORE = interface("MC")
EMISSION = interface("MC_EMISSION")
PROVIDER = interface("MC_PROVIDER")
INTERFACES = (ASSEMBLY_PROVIDER, CORE, EMISSION, PROVIDER)

__all__ = ["ASSEMBLY_PROVIDER", "CORE", "EMISSION", "INTERFACES", "PROVIDER"]
