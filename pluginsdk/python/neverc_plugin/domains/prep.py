"""Preprocessor tokens, streams, events, and providers."""

from ._base import interface

PREP = interface("PREP")
query = PREP.query

__all__ = ["PREP", "query"]
