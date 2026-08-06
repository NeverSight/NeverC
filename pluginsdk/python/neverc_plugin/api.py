"""Small, lifetime-checked Python surface for NeverC plugins.

The implementation deliberately keeps policy and ergonomic validation here,
while `_neverc_plugin` performs authoritative host validation and all access to
NeverC-owned objects.
"""

from __future__ import annotations

from dataclasses import dataclass
from enum import IntEnum
import re
import sys
from types import ModuleType
from typing import Any, Callable, Iterable, Mapping, Sequence, TypeVar

try:
    import _neverc_plugin as _native
except ModuleNotFoundError:
    _native = None


_PLUGIN_ID_SEGMENT = re.compile(r"^[a-z0-9](?:[a-z0-9_-]{0,61}[a-z0-9])?$")
_SEMVER = re.compile(
    r"^(0|[1-9][0-9]*)\."
    r"(0|[1-9][0-9]*)\."
    r"(0|[1-9][0-9]*)"
    r"(?:-((?:0|[1-9][0-9]*|[0-9A-Za-z-]*[A-Za-z-][0-9A-Za-z-]*)"
    r"(?:\.(?:0|[1-9][0-9]*|[0-9A-Za-z-]*[A-Za-z-][0-9A-Za-z-]*))*))?"
    r"(?:\+([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?$"
)


def _validate_plugin_id(value: str) -> str:
    if not isinstance(value, str):
        raise TypeError("plugin id must be a string")
    if not value or len(value.encode("utf-8")) > 255:
        raise ValueError("plugin id must contain 1 to 255 bytes")
    segments = value.split(".")
    if any(not _PLUGIN_ID_SEGMENT.fullmatch(segment) for segment in segments):
        raise ValueError(f"plugin id is not canonical: {value!r}")
    return value


def _parse_semver(value: str) -> tuple[tuple[int, int, int], str, str]:
    if not isinstance(value, str):
        raise TypeError("plugin version must be a string")
    match = _SEMVER.fullmatch(value)
    if match is None:
        raise ValueError(f"plugin version is not strict SemVer: {value!r}")
    numeric = tuple(int(match.group(index)) for index in range(1, 4))
    if any(component > 0xFFFFFFFF for component in numeric):
        raise ValueError("plugin version component exceeds uint32")
    return numeric, match.group(4) or "", match.group(5) or ""


@dataclass(frozen=True, slots=True)
class PluginSpec:
    id: str
    name: str
    version: str
    version_info: tuple[int, int, int]
    prerelease: str
    build_metadata: str
    plugin_class: type[Any]


_PluginClass = TypeVar("_PluginClass", bound=type[Any])


def Plugin(*, id: str, name: str, version: str) -> Callable[[_PluginClass], _PluginClass]:
    """Declare the single NeverC plugin class exported by a script module."""

    plugin_id = _validate_plugin_id(id)
    if not isinstance(name, str):
        raise TypeError("plugin name must be a string")
    if not name or "\x00" in name:
        raise ValueError("plugin name must be non-empty and contain no NUL")
    version_info, prerelease, build_metadata = _parse_semver(version)

    def decorate(plugin_class: _PluginClass) -> _PluginClass:
        if not isinstance(plugin_class, type):
            raise TypeError("Plugin can decorate only a class")
        module = sys.modules.get(plugin_class.__module__)
        if not isinstance(module, ModuleType):
            raise RuntimeError("plugin class is not attached to an imported module")
        if hasattr(module, "__neverc_plugin__"):
            raise RuntimeError("a module may declare only one NeverC plugin")
        spec = PluginSpec(
            id=plugin_id,
            name=name,
            version=version,
            version_info=version_info,
            prerelease=prerelease,
            build_metadata=build_metadata,
            plugin_class=plugin_class,
        )
        setattr(module, "__neverc_plugin__", spec)
        setattr(plugin_class, "__neverc_plugin__", spec)
        return plugin_class

    return decorate


@dataclass(frozen=True, slots=True)
class Phase:
    high: int
    low: int
    name: str

    def __post_init__(self) -> None:
        for label, value in (("high", self.high), ("low", self.low)):
            if not isinstance(value, int) or isinstance(value, bool):
                raise TypeError(f"phase {label} must be an integer")
            if value < 0 or value > 0xFFFFFFFFFFFFFFFF:
                raise ValueError(f"phase {label} must fit uint64")
        if not isinstance(self.name, str) or not self.name or "\x00" in self.name:
            raise ValueError("phase name must be a non-empty string without NUL")


class ArgumentOrigin(IntEnum):
    COMMAND_LINE = 0
    CONFIGURATION = 1
    PLUGIN = 2


class TaskKind(IntEnum):
    INVOCATION = 1
    TRANSLATION_UNIT = 2
    LTO = 3
    LINK = 4
    CODEGEN = 5
    OBJECT = 6
    DYNCODE = 7


@dataclass(frozen=True, slots=True)
class Argument:
    value: str
    origin: ArgumentOrigin
    source: str
    position: int


def _host_function(name: str) -> Callable[..., Any]:
    if _native is None:
        raise RuntimeError(
            "NeverC's native Python plugin bridge is unavailable; "
            "use this operation only from a Python-enabled NeverC plugin"
        )
    function = getattr(_native, name, None)
    if function is None:
        raise RuntimeError(f"NeverC Python bridge does not provide {name}()")
    return function


def _string_sequence(name: str, value: Iterable[str]) -> tuple[str, ...]:
    if isinstance(value, (str, bytes)):
        raise TypeError(f"{name} must be an iterable of strings")
    result = tuple(value)
    if any(not isinstance(item, str) or not item or "\x00" in item for item in result):
        raise ValueError(f"{name} must contain non-empty strings without NUL")
    if len(set(result)) != len(result):
        raise ValueError(f"{name} must not contain duplicates")
    return result


class _Context:
    __slots__ = ("_native_handle",)

    def __init__(self, native_handle: object) -> None:
        self._native_handle = native_handle

    def option_values(self, spelling: str) -> tuple[str, ...]:
        if not isinstance(spelling, str) or not spelling or "\x00" in spelling:
            raise ValueError("option spelling must be a non-empty string without NUL")
        return tuple(_host_function("option_values")(self._native_handle, spelling))

    def check_cancelled(self) -> None:
        _host_function("check_cancelled")(self._native_handle)

    def _emit(self, severity: int, message: str, code: int = 0) -> None:
        if not isinstance(message, str) or not message or "\x00" in message:
            raise ValueError("diagnostic message must be non-empty and contain no NUL")
        if (
            not isinstance(code, int)
            or isinstance(code, bool)
            or code < 0
            or code > 0xFFFFFFFF
        ):
            raise ValueError("diagnostic code must fit uint32")
        _host_function("emit_diagnostic")(
            self._native_handle, severity, message, code
        )

    def emit_remark(self, message: str, *, code: int = 0) -> None:
        self._emit(1, message, code)

    def emit_warning(self, message: str, *, code: int = 0) -> None:
        self._emit(2, message, code)

    def emit_error(self, message: str, *, code: int = 0) -> None:
        self._emit(3, message, code)

    def emit_fatal(self, message: str, *, code: int = 0) -> None:
        self._emit(4, message, code)


class _StateContext(_Context):
    __slots__ = ("state",)

    def __init__(self, native_handle: object, info: Mapping[str, Any] | None = None) -> None:
        super().__init__(native_handle)
        self.state: Any = None


class ProcessContext(_StateContext):
    pass


class SessionContext(_StateContext):
    __slots__ = ("handle",)

    def __init__(self, native_handle: object, info: Mapping[str, Any]) -> None:
        super().__init__(native_handle, info)
        self.handle = tuple(info["handle"])


class TaskContext(_StateContext):
    __slots__ = ("handle", "kind")

    def __init__(self, native_handle: object, info: Mapping[str, Any]) -> None:
        super().__init__(native_handle, info)
        self.handle = tuple(info["handle"])
        self.kind = TaskKind(info["kind"])


_OPTION_FORMS = {"flag": 0, "joined": 1, "separate": 2, "multi_arg": 3}
_OPTION_VALUE_TYPES = {
    "bool": 0,
    "int": 1,
    "uint": 2,
    "string": 3,
    "enum": 4,
    "path": 5,
}
_OPTION_MULTIPLICITIES = {"single": 0, "last_wins": 1, "append": 2}
_OBSERVER_POINTS = {"before": 1, "after": 2, "after_commit": 4}


class RegistrationContext(_Context):
    def option(
        self,
        spelling: str,
        *,
        kind: str = "flag",
        value_type: str = "bool",
        multiplicity: str = "single",
        argument_count: int | None = None,
        required: bool = False,
        hidden: bool = False,
        help: str = "",
        metavar: str = "",
        aliases: Sequence[str] = (),
        conflicts: Sequence[str] = (),
        requires: Sequence[str] = (),
        enum_values: Mapping[str, int] | None = None,
    ) -> None:
        if not isinstance(spelling, str) or not spelling.startswith("-") or "\x00" in spelling:
            raise ValueError("option spelling must start with '-' and contain no NUL")
        try:
            form = _OPTION_FORMS[kind]
        except (KeyError, TypeError) as error:
            raise ValueError(f"unsupported option kind: {kind!r}") from error
        try:
            native_value_type = _OPTION_VALUE_TYPES[value_type]
        except (KeyError, TypeError) as error:
            raise ValueError(f"unsupported option value type: {value_type!r}") from error
        try:
            native_multiplicity = _OPTION_MULTIPLICITIES[multiplicity]
        except (KeyError, TypeError) as error:
            raise ValueError(f"unsupported option multiplicity: {multiplicity!r}") from error
        if argument_count is None:
            argument_count = 1 if form == 3 else 0
        if (
            not isinstance(argument_count, int)
            or isinstance(argument_count, bool)
            or argument_count < 0
            or argument_count > 0xFFFFFFFF
        ):
            raise ValueError("argument_count must fit uint32")
        if form == 0 and (argument_count != 0 or native_value_type != 0):
            raise ValueError("flag options must be bool with argument_count=0")
        if form == 3 and argument_count == 0:
            raise ValueError("multi_arg options need at least one argument")
        if form != 3 and argument_count != 0:
            raise ValueError("argument_count is only valid for multi_arg options")
        native_enum_values: tuple[tuple[str, int], ...] = ()
        if enum_values is not None:
            if not isinstance(enum_values, Mapping):
                raise TypeError("enum_values must be a mapping of names to integers")
            native_enum_values = tuple(enum_values.items())
            for enum_name, enum_value in native_enum_values:
                if not isinstance(enum_name, str) or not enum_name or "\x00" in enum_name:
                    raise ValueError("enum value names must be non-empty strings without NUL")
                if (
                    not isinstance(enum_value, int)
                    or isinstance(enum_value, bool)
                    or not -(1 << 63) <= enum_value < (1 << 63)
                ):
                    raise ValueError("enum values must fit int64")
        if native_value_type == 4 and not native_enum_values:
            raise ValueError("enum options require enum_values")
        if native_value_type != 4 and native_enum_values:
            raise ValueError("enum_values is only valid for enum options")
        if not isinstance(help, str) or "\x00" in help:
            raise ValueError("option help must be a string without NUL")
        if not isinstance(metavar, str) or "\x00" in metavar:
            raise ValueError("option metavar must be a string without NUL")
        if not isinstance(required, bool) or not isinstance(hidden, bool):
            raise TypeError("required and hidden must be bool")
        descriptor = {
            "spelling": spelling,
            "form": form,
            "value_type": native_value_type,
            "multiplicity": native_multiplicity,
            "argument_count": argument_count,
            "required": required,
            "hidden": hidden,
            "help": help,
            "metavar": metavar,
            "aliases": _string_sequence("aliases", aliases),
            "conflicts": _string_sequence("conflicts", conflicts),
            "requires": _string_sequence("requires", requires),
            "enum_values": native_enum_values,
        }
        _host_function("register_option")(self._native_handle, descriptor)

    def observer(
        self,
        phase: Phase,
        *,
        when: str | Sequence[str] = "before",
        fn: Callable[[Frame], None] | None = None,
    ) -> Callable[[Callable[[Frame], None]], Callable[[Frame], None]] | Callable[[Frame], None]:
        if not isinstance(phase, Phase):
            raise TypeError("observer phase must be a Phase")
        names = (when,) if isinstance(when, str) else tuple(when)
        if not names:
            raise ValueError("observer requires at least one delivery point")
        try:
            points = sum({_OBSERVER_POINTS[name] for name in names})
        except (KeyError, TypeError) as error:
            raise ValueError(f"unsupported observer delivery point: {when!r}") from error

        def register(callback: Callable[[Frame], None]) -> Callable[[Frame], None]:
            if not callable(callback):
                raise TypeError("observer callback must be callable")
            _host_function("register_observer")(
                self._native_handle,
                phase.high,
                phase.low,
                phase.name,
                points,
                callback,
            )
            return callback

        return register if fn is None else register(fn)


class Frame(_Context):
    __slots__ = (
        "phase",
        "when",
        "session",
        "task",
        "target_triple",
        "cpu",
        "features",
        "object_format",
        "execution_level",
        "input_handle",
        "output_handle",
    )

    def __init__(self, native_handle: object, info: Mapping[str, Any]) -> None:
        super().__init__(native_handle)
        self.phase = Phase(info["phase_high"], info["phase_low"], info["phase_name"])
        self.when = info["when"]
        self.session = tuple(info["session"])
        self.task = tuple(info["task"])
        self.target_triple = info["target_triple"]
        self.cpu = info["cpu"]
        self.features = info["features"]
        self.object_format = info["object_format"]
        self.execution_level = info["execution_level"]
        self.input_handle = tuple(info["input_handle"])
        self.output_handle = tuple(info["output_handle"])

    @property
    def arguments(self) -> tuple[Argument, ...]:
        values = _host_function("frame_arguments")(self._native_handle)
        return tuple(
            Argument(value, ArgumentOrigin(origin), source, position)
            for value, origin, source, position in values
        )


__all__ = [
    "Argument",
    "ArgumentOrigin",
    "Frame",
    "Phase",
    "Plugin",
    "PluginSpec",
    "ProcessContext",
    "RegistrationContext",
    "SessionContext",
    "TaskContext",
    "TaskKind",
]
