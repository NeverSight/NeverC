"""I/O, VFS, source buffers, and source locations."""

from ._base import interface

IO = interface("IO")
SOURCE_LOCATION = interface("SOURCE_LOCATION")
INTERFACES = (IO, SOURCE_LOCATION)

__all__ = ["INTERFACES", "IO", "SOURCE_LOCATION"]
