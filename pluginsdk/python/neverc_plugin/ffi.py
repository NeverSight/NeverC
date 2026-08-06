"""Lifetime-checked access to the complete generated NeverC C ABI.

This module is intentionally thin: :mod:`neverc_plugin.abi` owns C layout and
signatures, while the private in-process bridge owns authority and lifetime.
Every SDK dereference returns through ``Scope.ensure_active()`` first.
"""

from __future__ import annotations

import ctypes
from dataclasses import dataclass
from enum import IntEnum, IntFlag
from typing import Any, Callable, Generic, TypeVar

from . import abi
from . import api as _api


class ContextKind(IntEnum):
    REGISTRATION = 1
    PROCESS = 2
    SESSION = 3
    TASK = 4
    FRAME = 5
    CALLBACK = 6


class Capability(IntFlag):
    CORE = 1 << 0
    REGISTRAR = 1 << 1
    REGISTRAR_CONTEXT = 1 << 2
    SESSION = 1 << 3
    TASK = 1 << 4
    FRAME = 1 << 5
    CONTINUATION = 1 << 6
    INVOCATION = 1 << 7


class NevercError(RuntimeError):
    """A non-success ``NevercStatus`` returned by a raw ABI call."""

    def __init__(self, status: abi.NevercStatus, operation: str = "NeverC call"):
        self.code = int(status.Code)
        self.flags = int(status.Flags)
        self.detail = int(status.Detail)
        self.operation = operation
        super().__init__(
            f"{operation} failed with status {self.code} "
            f"(flags={self.flags}, detail={self.detail})"
        )


def require_ok(
    status: abi.NevercStatus, operation: str = "NeverC call"
) -> abi.NevercStatus:
    if not isinstance(status, abi.NevercStatus):
        raise TypeError("require_ok expects NevercStatus")
    if int(status.Code) != int(abi.NEVERC_STATUS_OK):
        raise NevercError(status, operation)
    return status


@dataclass(frozen=True, slots=True)
class TableHeader:
    StructSize: int
    Major: int
    Minor: int
    Flags: int


def _address(value: Any, name: str) -> int:
    if not isinstance(value, int) or isinstance(value, bool) or value < 0:
        raise RuntimeError(f"NeverC returned an invalid {name}")
    return value


class Scope:
    """An authenticated snapshot of capabilities for one native callback."""

    __slots__ = (
        "_native_handle",
        "_runtime_id",
        "_context_id",
        "_kind",
        "_mask",
        "_snapshot",
    )

    def __init__(self, native_handle: object):
        snapshot = _api._host_function("context_capabilities")(native_handle)
        if not isinstance(snapshot, dict):
            raise RuntimeError("NeverC returned a malformed capability snapshot")
        try:
            self._runtime_id = int(snapshot["runtime_id"])
            self._context_id = int(snapshot["context_id"])
            self._kind = ContextKind(snapshot["kind"])
            self._mask = Capability(snapshot["mask"])
        except (KeyError, TypeError, ValueError, OverflowError) as error:
            raise RuntimeError("NeverC returned a malformed capability identity") from error
        self._native_handle = native_handle
        self._snapshot = snapshot

    @property
    def kind(self) -> ContextKind:
        return self._kind

    @property
    def capabilities(self) -> Capability:
        return self._mask

    def ensure_active(self) -> None:
        active = _api._host_function("context_is_active")(
            self._native_handle, self._runtime_id, self._context_id
        )
        if active is not True:
            raise RuntimeError("NeverC context is no longer active")

    def require(self, capability: Capability) -> None:
        self.ensure_active()
        if not self._mask & capability:
            raise RuntimeError(
                f"NeverC context does not provide capability {capability.name}"
            )

    def _snapshot_address(self, name: str, capability: Capability) -> int:
        self.require(capability)
        return _address(self._snapshot.get(name), name)

    @property
    def core(self) -> "TableView":
        return TableView(
            self,
            abi.NevercCoreAPI,
            self._snapshot_address("core_address", Capability.CORE),
        )

    @property
    def core_context_address(self) -> int:
        return self._snapshot_address("core_context_address", Capability.CORE)

    @property
    def registrar(self) -> "TableView":
        return TableView(
            self,
            abi.NevercRegistrarAPI,
            self._snapshot_address("registrar_address", Capability.REGISTRAR),
        )

    @property
    def registrar_context_address(self) -> int:
        return self._snapshot_address(
            "registrar_context_address", Capability.REGISTRAR_CONTEXT
        )

    @property
    def session(self) -> tuple[int, int]:
        self.require(Capability.SESSION)
        return tuple(self._snapshot["session"])

    @property
    def task(self) -> tuple[int, int]:
        self.require(Capability.TASK)
        return tuple(self._snapshot["task"])

    @property
    def frame(self) -> "CheckedPointer":
        return CheckedPointer(
            self,
            abi.NevercPhaseFrame,
            self._snapshot_address("frame_address", Capability.FRAME),
        )

    @property
    def continuation_address(self) -> int:
        return self._snapshot_address(
            "continuation_address", Capability.CONTINUATION
        )

    @property
    def invocation_address(self) -> int:
        return self._snapshot_address("invocation_address", Capability.INVOCATION)

    def table(self, record_type: type[ctypes.Structure], address: int) -> "TableView":
        self.ensure_active()
        return TableView(self, record_type, address)

    def query(
        self,
        table_type: type[ctypes.Structure],
        interface_high: int,
        interface_low: int,
        major: int,
        minimum_minor: int = 0,
    ) -> "TableView":
        """Query and size-check one official interface table."""

        interface_id = abi.NevercInterfaceID()
        interface_id.High = interface_high
        interface_id.Low = interface_low
        table = ctypes.c_void_p()
        provided_minor = ctypes.c_uint16()
        struct_size = ctypes.c_uint64()
        result = self.core.function("QueryInterface")(
            ctypes.c_void_p(self.core_context_address),
            interface_id,
            major,
            minimum_minor,
            ctypes.byref(table),
            ctypes.byref(provided_minor),
            ctypes.byref(struct_size),
        )
        require_ok(result, f"query interface {table_type.__name__}")
        if not table.value:
            raise RuntimeError(f"NeverC returned a null {table_type.__name__} table")
        minimum = ctypes.sizeof(abi.NevercABITableHeader)
        if struct_size.value < minimum:
            raise RuntimeError(
                f"NeverC returned {table_type.__name__} StructSize "
                f"{struct_size.value}, expected at least {minimum}"
            )
        view = TableView(self, table_type, table.value)
        if view.header.StructSize != struct_size.value:
            raise RuntimeError(
                f"NeverC returned inconsistent {table_type.__name__} StructSize"
            )
        return view


def from_context(context: object) -> Scope:
    try:
        native_handle = context._native_handle
    except AttributeError as error:
        raise TypeError("expected a NeverC plugin context") from error
    return Scope(native_handle)


_Record = TypeVar("_Record", bound=ctypes.Structure)


class CheckedPointer(Generic[_Record]):
    __slots__ = ("_scope", "_record_type", "_address")

    def __init__(self, scope: Scope, record_type: type[_Record], address: int):
        if not isinstance(record_type, type) or not issubclass(
            record_type, (ctypes.Structure, ctypes.Union)
        ):
            raise TypeError("record_type must be a ctypes record")
        self._scope = scope
        self._record_type = record_type
        self._address = _address(address, f"{record_type.__name__} address")

    @property
    def address(self) -> int:
        self._scope.ensure_active()
        if not self._address:
            raise RuntimeError(f"NeverC returned a null {self._record_type.__name__}")
        return self._address

    def copy(self) -> _Record:
        """Copy a record while its callback scope is active."""

        address = self.address
        result = self._record_type()
        ctypes.memmove(ctypes.addressof(result), address, ctypes.sizeof(result))
        return result


class TableView(CheckedPointer[_Record]):
    """Checked view of a versioned ABI table whose slots remain raw."""

    def _contents(self) -> _Record:
        return ctypes.cast(
            self.address, ctypes.POINTER(self._record_type)
        ).contents

    @property
    def header(self) -> TableHeader:
        native = self._contents().Header
        return TableHeader(
            int(native.StructSize),
            int(native.Major),
            int(native.Minor),
            int(native.Flags),
        )

    def _validate_field(self, field: str) -> None:
        field_types = dict(self._record_type._fields_)
        try:
            native_field = getattr(self._record_type, field)
            field_type = field_types[field]
        except (AttributeError, KeyError) as error:
            raise AttributeError(
                f"{self._record_type.__name__} has no ABI field {field!r}"
            ) from error
        required = native_field.offset + ctypes.sizeof(field_type)
        if self.header.StructSize < required:
            raise RuntimeError(
                f"{self._record_type.__name__}.{field} requires StructSize "
                f"{required}, host provides {self.header.StructSize}"
            )

    def function(self, field: str) -> Callable[..., Any]:
        symbol = f"{self._record_type.__name__}.{field}"
        self._validate_field(field)
        abi.function_type(symbol)  # Validate generated signature now.

        def invoke(*arguments: Any) -> Any:
            self._validate_field(field)
            function = abi.bind_function(self._contents(), field)
            return function(*arguments)

        invoke.__name__ = field
        invoke.__qualname__ = symbol
        return invoke

    def call(self, field: str, *arguments: Any, checked: bool = True) -> Any:
        result = self.function(field)(*arguments)
        if checked and isinstance(result, abi.NevercStatus):
            require_ok(result, f"{self._record_type.__name__}.{field}")
        return result


class StringView:
    __slots__ = ("_scope", "_data", "_length")

    def __init__(self, scope: Scope, native: abi.NevercStringView):
        if not isinstance(native, abi.NevercStringView):
            raise TypeError("StringView expects NevercStringView")
        self._scope = scope
        self._data = ctypes.cast(native.Data, ctypes.c_void_p).value or 0
        self._length = int(native.Length)

    @property
    def bytes(self) -> bytes:
        self._scope.ensure_active()
        if not self._data and self._length:
            raise RuntimeError("NeverC returned an invalid string view")
        return ctypes.string_at(self._data, self._length) if self._length else b""

    @property
    def text(self) -> str:
        return self.bytes.decode("utf-8", "strict")


class ByteView:
    __slots__ = ("_scope", "_data", "_length")

    def __init__(self, scope: Scope, native: abi.NevercByteView):
        if not isinstance(native, abi.NevercByteView):
            raise TypeError("ByteView expects NevercByteView")
        self._scope = scope
        self._data = ctypes.cast(native.Data, ctypes.c_void_p).value or 0
        self._length = int(native.Length)

    @property
    def bytes(self) -> bytes:
        self._scope.ensure_active()
        if not self._data and self._length:
            raise RuntimeError("NeverC returned an invalid byte view")
        return ctypes.string_at(self._data, self._length) if self._length else b""


class Transaction:
    """Exactly-once commit/abort/destroy guard for mutable SDK operations."""

    __slots__ = ("_scope", "_commit", "_abort", "_destroy", "_resolved", "_closed")

    def __init__(
        self,
        scope: Scope,
        *,
        commit: Callable[[], Any],
        abort: Callable[[], Any],
        destroy: Callable[[], Any] | None = None,
    ):
        if not isinstance(scope, Scope):
            raise TypeError("Transaction expects a Scope")
        if not callable(commit) or not callable(abort):
            raise TypeError("commit and abort must be callable")
        if destroy is not None and not callable(destroy):
            raise TypeError("destroy must be callable")
        self._scope = scope
        self._commit = commit
        self._abort = abort
        self._destroy = destroy
        self._resolved = False
        self._closed = False

    def __enter__(self) -> "Transaction":
        if self._closed:
            raise RuntimeError("NeverC transaction is already closed")
        self._scope.ensure_active()
        return self

    @staticmethod
    def _check_result(result: Any, operation: str) -> Any:
        if isinstance(result, abi.NevercStatus):
            require_ok(result, operation)
        return result

    def _resolve(self, operation: str, callback: Callable[[], Any]) -> Any:
        if self._resolved:
            raise RuntimeError("NeverC transaction is already resolved")
        if self._closed:
            raise RuntimeError("NeverC transaction is already closed")
        self._scope.ensure_active()
        self._resolved = True
        return self._check_result(callback(), operation)

    def commit(self) -> Any:
        return self._resolve("commit transaction", self._commit)

    def abort(self) -> Any:
        return self._resolve("abort transaction", self._abort)

    def close(self) -> None:
        if self._closed:
            return
        try:
            if not self._resolved:
                self.abort()
        finally:
            self._closed = True
            if self._destroy is not None:
                self._scope.ensure_active()
                self._check_result(self._destroy(), "destroy transaction")

    def __exit__(self, exc_type, exc_value, traceback) -> bool:
        self.close()
        return False


class OneShotContinuation:
    """A continuation that cannot be retained and invoked twice."""

    __slots__ = ("_scope", "_invoke", "_invoked")

    def __init__(self, scope: Scope, invoke: Callable[..., Any]):
        if not isinstance(scope, Scope):
            raise TypeError("OneShotContinuation expects a Scope")
        if not callable(invoke):
            raise TypeError("continuation must be callable")
        self._scope = scope
        self._invoke = invoke
        self._invoked = False

    @property
    def invoked(self) -> bool:
        return self._invoked

    def __call__(self, *arguments: Any, **keywords: Any) -> Any:
        if self._invoked:
            raise RuntimeError("NeverC continuation was already invoked")
        self._scope.ensure_active()
        self._invoked = True
        return self._invoke(*arguments, **keywords)


def decode_record(record_type: type[_Record], data: bytes) -> _Record:
    """Decode immutable callback bytes into an owned ctypes record copy."""

    if not isinstance(data, bytes):
        raise TypeError("callback record data must be bytes")
    if len(data) != ctypes.sizeof(record_type):
        raise ValueError(
            f"{record_type.__name__} needs {ctypes.sizeof(record_type)} bytes, "
            f"received {len(data)}"
        )
    result = record_type()
    ctypes.memmove(ctypes.addressof(result), data, len(data))
    return result


class BoundCallbackRecord(Generic[_Record]):
    """Native-trampolined descriptor with explicit ownership transfer."""

    __slots__ = (
        "_scope",
        "_record_type",
        "_binding_id",
        "_address",
        "_transferred",
        "_released",
    )

    def __init__(
        self,
        scope: Scope,
        record_type: type[_Record],
        binding_id: int,
        address: int,
    ):
        self._scope = scope
        self._record_type = record_type
        self._binding_id = binding_id
        self._address = address
        self._transferred = False
        self._released = False

    @property
    def pointer(self):
        if self._released:
            raise RuntimeError("NeverC callback binding was released")
        self._scope.ensure_active()
        return ctypes.cast(self._address, ctypes.POINTER(self._record_type))

    @property
    def transferred(self) -> bool:
        return self._transferred

    def transfer(self) -> None:
        if self._released:
            raise RuntimeError("NeverC callback binding was released")
        if self._transferred:
            raise RuntimeError("NeverC callback binding was already transferred")
        self._scope.ensure_active()
        _api._host_function("transfer_callback_binding")(
            self._scope._native_handle, self._binding_id
        )
        self._transferred = True

    def release(self) -> None:
        if self._released:
            return
        if self._transferred:
            raise RuntimeError("NeverC callback binding ownership was transferred")
        self._scope.ensure_active()
        _api._host_function("release_callback_binding")(
            self._scope._native_handle, self._binding_id
        )
        self._released = True

    def __enter__(self) -> "BoundCallbackRecord[_Record]":
        self._scope.ensure_active()
        return self

    def __exit__(self, exc_type, exc_value, traceback) -> bool:
        if not self._transferred:
            self.release()
        return False


def bind_callbacks(
    scope: Scope,
    descriptor: _Record,
    callbacks: dict[str, Callable[..., Any]],
) -> BoundCallbackRecord[_Record]:
    """Bind official descriptor callbacks through generated native trampolines."""

    if not isinstance(scope, Scope):
        raise TypeError("bind_callbacks expects a Scope")
    if not isinstance(descriptor, (ctypes.Structure, ctypes.Union)):
        raise TypeError("descriptor must be a generated ctypes record")
    if type(descriptor).__name__ not in abi.PUBLIC_RECORDS:
        raise TypeError("descriptor is not a public NeverC ABI record")
    if not isinstance(callbacks, dict):
        raise TypeError("callbacks must be a dict")
    for name, callback in callbacks.items():
        if not isinstance(name, str) or not name:
            raise TypeError("callback names must be non-empty strings")
        if not callable(callback):
            raise TypeError(f"callback {name!r} is not callable")
    scope.ensure_active()
    data = ctypes.string_at(ctypes.addressof(descriptor), ctypes.sizeof(descriptor))
    binding_id, address = _api._host_function("bind_callback_record")(
        scope._native_handle, type(descriptor).__name__, data, callbacks
    )
    return BoundCallbackRecord(
        scope, type(descriptor), int(binding_id), _address(address, "callback record")
    )


__all__ = [
    "ByteView",
    "BoundCallbackRecord",
    "Capability",
    "CheckedPointer",
    "ContextKind",
    "NevercError",
    "OneShotContinuation",
    "Scope",
    "StringView",
    "TableHeader",
    "TableView",
    "Transaction",
    "bind_callbacks",
    "decode_record",
    "from_context",
    "require_ok",
]
