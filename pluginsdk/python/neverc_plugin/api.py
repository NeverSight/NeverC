"""Complete, lifetime-checked Python surface for NeverC plugins.

The implementation deliberately keeps policy and ergonomic validation here,
while `_neverc_plugin` performs authoritative host validation and all access to
NeverC-owned objects.
"""

from __future__ import annotations

from dataclasses import dataclass, field
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


def _compare_semver(
    left: tuple[tuple[int, int, int], str],
    right: tuple[tuple[int, int, int], str],
) -> int:
    left_version, left_prerelease = left
    right_version, right_prerelease = right
    if left_version != right_version:
        return -1 if left_version < right_version else 1
    if not left_prerelease or not right_prerelease:
        if left_prerelease == right_prerelease:
            return 0
        return 1 if not left_prerelease else -1
    left_parts = left_prerelease.split(".")
    right_parts = right_prerelease.split(".")
    for left_part, right_part in zip(left_parts, right_parts):
        if left_part == right_part:
            continue
        left_numeric = left_part.isdigit()
        right_numeric = right_part.isdigit()
        if left_numeric and right_numeric:
            return -1 if int(left_part) < int(right_part) else 1
        if left_numeric != right_numeric:
            return -1 if left_numeric else 1
        return -1 if left_part < right_part else 1
    if len(left_parts) == len(right_parts):
        return 0
    return -1 if len(left_parts) < len(right_parts) else 1


class InterfaceStability(IntEnum):
    STABLE = 0
    LOCKSTEP = 1


class Concurrency(IntEnum):
    SESSION_SERIAL = 0
    THREAD_SAFE = 1
    PROCESS_SERIAL = 2


class Reentrancy(IntEnum):
    NONE = 0
    ALLOWED = 1


class DependencyKind(IntEnum):
    REQUIRED = 0
    BEFORE = 1
    AFTER = 2


def _enum_value(name: str, value: Any, enum_type: type[IntEnum]) -> IntEnum:
    if isinstance(value, str):
        key = value.upper().replace("-", "_")
        try:
            return enum_type[key]
        except KeyError as error:
            choices = ", ".join(item.name.lower() for item in enum_type)
            raise ValueError(f"{name} must be one of: {choices}") from error
    try:
        result = enum_type(value)
    except (TypeError, ValueError) as error:
        choices = ", ".join(item.name.lower() for item in enum_type)
        raise ValueError(f"{name} must be one of: {choices}") from error
    if isinstance(value, bool):
        raise TypeError(f"{name} must not be a boolean")
    return result


def _bounded_integer(name: str, value: Any, maximum: int) -> int:
    if not isinstance(value, int) or isinstance(value, bool):
        raise TypeError(f"{name} must be an integer")
    if value < 0 or value > maximum:
        raise ValueError(f"{name} must fit uint{maximum.bit_length()}")
    return value


def _metadata_string(name: str, value: Any, *, allow_empty: bool = True) -> str:
    if not isinstance(value, str):
        raise TypeError(f"{name} must be a string")
    if "\x00" in value or (not allow_empty and not value):
        qualifier = "non-empty and " if not allow_empty else ""
        raise ValueError(f"{name} must be {qualifier}free of NUL characters")
    return value


@dataclass(frozen=True, slots=True)
class InterfaceRequirement:
    """A versioned requirement for one official or custom plugin interface.

    ``interface`` may be an official generated interface name or a ``(high,
    low)`` identifier pair.  Custom identifiers must provide ``major``.
    """

    interface: str | tuple[int, int]
    major: int | None = None
    minimum_minor: int = 0
    stability: InterfaceStability | str | int = InterfaceStability.STABLE
    producer_build_id: str = ""
    target_abi_key: str = ""
    llvm_major: int = 0
    high: int = field(init=False, default=0)
    low: int = field(init=False, default=0)

    def __post_init__(self) -> None:
        major = self.major
        if isinstance(self.interface, str):
            from .abi import INTERFACE_SPECS

            name = self.interface.upper()
            try:
                spec = INTERFACE_SPECS[name]
            except KeyError as error:
                raise ValueError(f"unknown NeverC interface: {self.interface!r}") from error
            high, low = spec["high"], spec["low"]
            if major is None:
                major = spec["major"]
            object.__setattr__(self, "interface", name)
        else:
            if (
                not isinstance(self.interface, tuple)
                or len(self.interface) != 2
            ):
                raise TypeError(
                    "interface must be an official name or a (high, low) tuple"
                )
            high, low = self.interface
            if major is None:
                raise ValueError("custom interface requirements must provide major")
        object.__setattr__(self, "high", _bounded_integer("interface high", high, 0xFFFFFFFFFFFFFFFF))
        object.__setattr__(self, "low", _bounded_integer("interface low", low, 0xFFFFFFFFFFFFFFFF))
        object.__setattr__(self, "major", _bounded_integer("interface major", major, 0xFFFF))
        object.__setattr__(
            self,
            "minimum_minor",
            _bounded_integer("interface minimum minor", self.minimum_minor, 0xFFFF),
        )
        object.__setattr__(
            self,
            "stability",
            _enum_value("interface stability", self.stability, InterfaceStability),
        )
        object.__setattr__(
            self,
            "producer_build_id",
            _metadata_string("producer build ID", self.producer_build_id),
        )
        object.__setattr__(
            self,
            "target_abi_key",
            _metadata_string("target ABI key", self.target_abi_key),
        )
        object.__setattr__(
            self,
            "llvm_major",
            _bounded_integer("LLVM major", self.llvm_major, 0xFFFFFFFF),
        )


@dataclass(frozen=True, slots=True)
class PluginDependency:
    id: str
    minimum: str = "0.0.0"
    maximum: str | None = None
    kind: DependencyKind | str | int = DependencyKind.REQUIRED
    allow_prerelease: bool = False
    minimum_info: tuple[int, int, int] = field(init=False, default=(0, 0, 0))
    minimum_prerelease: str = field(init=False, default="")
    minimum_build_metadata: str = field(init=False, default="")
    maximum_info: tuple[int, int, int] = field(init=False, default=(0, 0, 0))
    maximum_prerelease: str = field(init=False, default="")
    maximum_build_metadata: str = field(init=False, default="")

    def __post_init__(self) -> None:
        object.__setattr__(self, "id", _validate_plugin_id(self.id))
        minimum_info, minimum_pre, minimum_build = _parse_semver(self.minimum)
        if self.maximum is None:
            maximum_info, maximum_pre, maximum_build = (0, 0, 0), "", ""
        else:
            maximum_info, maximum_pre, maximum_build = _parse_semver(self.maximum)
            if _compare_semver(
                (minimum_info, minimum_pre), (maximum_info, maximum_pre)
            ) >= 0:
                raise ValueError("dependency maximum must be greater than minimum")
        if not isinstance(self.allow_prerelease, bool):
            raise TypeError("dependency allow_prerelease must be a boolean")
        object.__setattr__(self, "kind", _enum_value("dependency kind", self.kind, DependencyKind))
        object.__setattr__(self, "minimum_info", minimum_info)
        object.__setattr__(self, "minimum_prerelease", minimum_pre)
        object.__setattr__(self, "minimum_build_metadata", minimum_build)
        object.__setattr__(self, "maximum_info", maximum_info)
        object.__setattr__(self, "maximum_prerelease", maximum_pre)
        object.__setattr__(self, "maximum_build_metadata", maximum_build)


@dataclass(frozen=True, slots=True)
class PluginSpec:
    id: str
    name: str
    version: str
    version_info: tuple[int, int, int]
    prerelease: str
    build_metadata: str
    abi_flags: int
    concurrency: Concurrency
    reentrancy: Reentrancy
    required_interfaces: tuple[InterfaceRequirement, ...]
    optional_interfaces: tuple[InterfaceRequirement, ...]
    dependencies: tuple[PluginDependency, ...]
    plugin_class: type[Any]


_PluginClass = TypeVar("_PluginClass", bound=type[Any])


def Plugin(
    *,
    id: str,
    name: str,
    version: str,
    abi_flags: int = 0,
    concurrency: Concurrency | str | int = Concurrency.SESSION_SERIAL,
    reentrancy: Reentrancy | str | int = Reentrancy.NONE,
    required_interfaces: Sequence[InterfaceRequirement] = (),
    optional_interfaces: Sequence[InterfaceRequirement] = (),
    dependencies: Sequence[PluginDependency] = (),
) -> Callable[[_PluginClass], _PluginClass]:
    """Declare the single NeverC plugin class exported by a script module."""

    plugin_id = _validate_plugin_id(id)
    if not isinstance(name, str):
        raise TypeError("plugin name must be a string")
    if not name or "\x00" in name:
        raise ValueError("plugin name must be non-empty and contain no NUL")
    version_info, prerelease, build_metadata = _parse_semver(version)
    normalized_flags = _bounded_integer("plugin ABI flags", abi_flags, 0xFFFFFFFFFFFFFFFF)
    if normalized_flags != 0:
        raise ValueError("this NeverC plugin ABI defines no non-zero plugin flags")
    normalized_concurrency = _enum_value("concurrency", concurrency, Concurrency)
    normalized_reentrancy = _enum_value("reentrancy", reentrancy, Reentrancy)
    normalized_required = tuple(required_interfaces)
    normalized_optional = tuple(optional_interfaces)
    normalized_dependencies = tuple(dependencies)
    if any(not isinstance(item, InterfaceRequirement) for item in normalized_required):
        raise TypeError("required_interfaces must contain InterfaceRequirement values")
    if any(not isinstance(item, InterfaceRequirement) for item in normalized_optional):
        raise TypeError("optional_interfaces must contain InterfaceRequirement values")
    if any(not isinstance(item, PluginDependency) for item in normalized_dependencies):
        raise TypeError("dependencies must contain PluginDependency values")
    required_ids = {(item.high, item.low) for item in normalized_required}
    optional_ids = {(item.high, item.low) for item in normalized_optional}
    if len(required_ids) != len(normalized_required):
        raise ValueError("required_interfaces must not contain duplicate IDs")
    if len(optional_ids) != len(normalized_optional):
        raise ValueError("optional_interfaces must not contain duplicate IDs")
    if required_ids & optional_ids:
        raise ValueError("an interface cannot be both required and optional")
    dependency_keys = {(item.id, int(item.kind)) for item in normalized_dependencies}
    if len(dependency_keys) != len(normalized_dependencies):
        raise ValueError("dependencies must not contain duplicate ID/kind pairs")

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
            abi_flags=normalized_flags,
            concurrency=normalized_concurrency,
            reentrancy=normalized_reentrancy,
            required_interfaces=normalized_required,
            optional_interfaces=normalized_optional,
            dependencies=normalized_dependencies,
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

    @property
    def ffi(self):
        """Return the complete lifetime-checked low-level ABI scope."""
        from .ffi import Scope

        return Scope(self._native_handle)

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
