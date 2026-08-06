"""All 36 official NeverC interface families grouped by compiler domain."""

from . import (
    ast,
    core,
    driver,
    dyncode,
    ir,
    link,
    lto,
    mc,
    mir,
    object,
    prep,
    sema,
    source,
    target,
)

ALL_INTERFACES = (
    core.CORE,
    source.IO,
    source.SOURCE_LOCATION,
    driver.DRIVER,
    prep.PREP,
    ast.AST,
    ast.PARSER,
    sema.SEMA,
    *ir.INTERFACES,
    *target.INTERFACES,
    *mir.INTERFACES,
    *mc.INTERFACES,
    *object.INTERFACES,
    *link.INTERFACES,
    *lto.INTERFACES,
    *dyncode.INTERFACES,
)

BY_NAME = {item.name: item for item in ALL_INTERFACES}

__all__ = [
    "ALL_INTERFACES", "BY_NAME", "ast", "core", "driver", "dyncode", "ir",
    "link", "lto", "mc", "mir", "object", "prep", "sema", "source", "target",
]
