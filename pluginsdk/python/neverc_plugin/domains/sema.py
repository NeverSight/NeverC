"""Semantic analysis, types, lookup, conversion, and extension services."""

from ._base import interface

SEMA = interface("SEMA")
query = SEMA.query

__all__ = ["SEMA", "query"]
