"""Stable NeverC driver phase constants."""

from ._base import interface

from ..phases import (
    DRIVER_BUILD_ACTIONS as BUILD_ACTIONS,
    DRIVER_BUILD_JOBS as BUILD_JOBS,
    DRIVER_EXECUTE_JOB as EXECUTE_JOB,
    DRIVER_PARSED_ARGUMENTS as PARSED_ARGUMENTS,
    DRIVER_RAW_ARGUMENTS as RAW_ARGUMENTS,
    DRIVER_SELECT_TOOLCHAIN as SELECT_TOOLCHAIN,
)

DRIVER = interface("DRIVER")
query = DRIVER.query

__all__ = [
    "BUILD_ACTIONS",
    "BUILD_JOBS",
    "EXECUTE_JOB",
    "PARSED_ARGUMENTS",
    "RAW_ARGUMENTS",
    "SELECT_TOOLCHAIN",
    "DRIVER",
    "query",
]
