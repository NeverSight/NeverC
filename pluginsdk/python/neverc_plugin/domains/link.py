"""Link graph, phase, and linker-provider registration APIs."""

from ._base import interface

CORE = interface("LINK")
PHASE = interface("LINK_PHASE")
REGISTRAR = interface("LINK_REGISTRAR")
INTERFACES = (CORE, PHASE, REGISTRAR)

__all__ = ["CORE", "INTERFACES", "PHASE", "REGISTRAR"]
