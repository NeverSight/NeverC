"""Core process/session/task services."""

from ._base import interface

CORE = interface("CORE")
query = CORE.query

__all__ = ["CORE", "query"]
