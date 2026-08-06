import sys

from neverc_plugin import Plugin


if sys.implementation.name != "cpython" or sys.version_info[:3] != (3, 12, 10):
    raise RuntimeError(
        "NeverC Python plugins require managed CPython 3.12.10; "
        f"loaded {sys.implementation.name} {sys.version_info[:3]}"
    )


@Plugin(
    id="com.neverc.test.python-runtime-version",
    name="Python Runtime Version Test",
    version="1.0.0",
)
class RuntimeVersionPlugin:
    pass
