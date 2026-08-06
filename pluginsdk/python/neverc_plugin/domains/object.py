"""Object graph, format registration, and object-phase APIs."""

from ._base import interface

CORE = interface("OBJECT")
FORMAT = interface("OBJECT_FORMAT")
PHASE = interface("OBJECT_PHASE")
INTERFACES = (CORE, FORMAT, PHASE)

__all__ = ["CORE", "FORMAT", "INTERFACES", "PHASE"]
