"""Shared interface descriptors for thin domain modules."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any

from .. import abi
from ..ffi import Scope, TableView, from_context


@dataclass(frozen=True, slots=True)
class Interface:
    name: str
    table_type: type
    high: int
    low: int
    major: int
    minor: int

    def query(self, context_or_scope: Any, minimum_minor: int = 0) -> TableView:
        scope = (
            context_or_scope
            if isinstance(context_or_scope, Scope)
            else from_context(context_or_scope)
        )
        if self.name == "CORE":
            table = scope.core
            header = table.header
            if header.Major != self.major or header.Minor < minimum_minor:
                raise RuntimeError(
                    f"NeverC CORE version {header.Major}.{header.Minor} does not "
                    f"satisfy {self.major}.{minimum_minor}"
                )
            return table
        return scope.query(
            self.table_type,
            self.high,
            self.low,
            self.major,
            minimum_minor,
        )


def interface(name: str) -> Interface:
    try:
        spec = abi.INTERFACE_SPECS[name]
    except KeyError as error:
        raise KeyError(f"unknown NeverC interface: {name}") from error
    return Interface(
        name=name,
        table_type=getattr(abi, spec["table"]),
        high=spec["high"],
        low=spec["low"],
        major=spec["major"],
        minor=spec["minor"],
    )


__all__ = ["Interface", "interface"]
