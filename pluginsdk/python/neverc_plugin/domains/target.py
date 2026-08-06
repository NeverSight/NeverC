"""Target descriptions, ABI lowering, and calling conventions."""

from ._base import interface

CALLING_CONVENTION = interface("CALLING_CONVENTION")
TARGET = interface("TARGET")
TARGET_ABI = interface("TARGET_ABI")
INTERFACES = (CALLING_CONVENTION, TARGET, TARGET_ABI)

__all__ = ["CALLING_CONVENTION", "INTERFACES", "TARGET", "TARGET_ABI"]
