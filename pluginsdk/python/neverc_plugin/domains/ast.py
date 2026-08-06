"""AST inspection/mutation and parser extension services."""

from ._base import interface

AST = interface("AST")
PARSER = interface("PARSER")
INTERFACES = (AST, PARSER)

__all__ = ["AST", "INTERFACES", "PARSER"]
