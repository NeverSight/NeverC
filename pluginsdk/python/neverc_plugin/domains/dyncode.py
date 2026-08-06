"""Dynamic-code extraction, target, phase, and registrar APIs."""

from ._base import interface

CORE = interface("DYNCODE")
PHASE = interface("DYNCODE_PHASE")
REGISTRAR = interface("DYNCODE_REGISTRAR")
INTERFACES = (CORE, PHASE, REGISTRAR)

__all__ = ["CORE", "INTERFACES", "PHASE", "REGISTRAR"]
