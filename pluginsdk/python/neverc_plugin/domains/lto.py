"""LTO request and provider-registration APIs."""

from ._base import interface

CORE = interface("LTO")
REGISTRAR = interface("LTO_REGISTRAR")
INTERFACES = (CORE, REGISTRAR)

__all__ = ["CORE", "INTERFACES", "REGISTRAR"]
