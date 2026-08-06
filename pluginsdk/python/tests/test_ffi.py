from __future__ import annotations

import ctypes
import unittest

from neverc_plugin import abi
from neverc_plugin import api
from neverc_plugin import ffi


class FakeNative:
    def __init__(self, snapshot):
        self.snapshot = snapshot
        self.active = True
        self.checks = 0
        self.callback_buffers = []
        self.transferred = []
        self.released = []

    def context_capabilities(self, handle):
        if not self.active:
            raise RuntimeError("NeverC context is no longer active")
        return dict(self.snapshot)

    def context_is_active(self, handle, runtime_id, context_id):
        self.checks += 1
        return (
            self.active
            and runtime_id == self.snapshot["runtime_id"]
            and context_id == self.snapshot["context_id"]
        )

    def bind_callback_record(self, handle, record_name, data, callbacks):
        buffer = ctypes.create_string_buffer(data, len(data))
        self.callback_buffers.append(buffer)
        return len(self.callback_buffers), ctypes.addressof(buffer)

    def transfer_callback_binding(self, handle, binding_id):
        self.transferred.append(binding_id)

    def release_callback_binding(self, handle, binding_id):
        self.released.append(binding_id)


class ScopedFFITests(unittest.TestCase):
    def setUp(self):
        self.previous_native = api._native
        self.core = abi.NevercCoreAPI()
        self.core.Header.StructSize = ctypes.sizeof(abi.NevercCoreAPI)
        self.core.Header.Major = abi.NEVERC_CORE_API_MAJOR
        self.registrar = abi.NevercRegistrarAPI()
        self.registrar.Header.StructSize = ctypes.sizeof(abi.NevercRegistrarAPI)
        self.registrar.Header.Major = abi.NEVERC_PLUGIN_ABI_MAJOR
        self.snapshot = {
            "runtime_id": 7,
            "context_id": 11,
            "kind": 1,
            "mask": int(
                ffi.Capability.CORE
                | ffi.Capability.REGISTRAR
                | ffi.Capability.REGISTRAR_CONTEXT
            ),
            "core_address": ctypes.addressof(self.core),
            "core_context_address": 0x1000,
            "registrar_address": ctypes.addressof(self.registrar),
            "registrar_context_address": 0x2000,
            "session": (0, 0),
            "task": (0, 0),
            "frame_address": 0,
            "continuation_address": 0,
            "invocation_address": 0,
        }
        self.native = FakeNative(self.snapshot)
        api._native = self.native

    def tearDown(self):
        api._native = self.previous_native

    def test_registration_exposes_checked_core_and_registrar(self):
        context = api.RegistrationContext(object())
        scope = context.ffi
        self.assertEqual(scope.kind, ffi.ContextKind.REGISTRATION)
        self.assertEqual(scope.registrar_context_address, 0x2000)
        self.assertEqual(
            scope.core.header.StructSize, ctypes.sizeof(abi.NevercCoreAPI)
        )
        self.assertEqual(
            scope.registrar.header.StructSize,
            ctypes.sizeof(abi.NevercRegistrarAPI),
        )
        self.assertGreaterEqual(self.native.checks, 2)

    def test_stale_scope_fails_before_invalid_pointer_dereference(self):
        scope = ffi.from_context(api.RegistrationContext(object()))
        table = ffi.TableView(scope, abi.NevercCoreAPI, 1)
        self.native.active = False
        with self.assertRaisesRegex(RuntimeError, "no longer active"):
            _ = table.header
        with self.assertRaisesRegex(RuntimeError, "no longer active"):
            table.function("QueryInterface")

    def test_struct_size_is_checked_before_binding_slot(self):
        scope = ffi.from_context(api.RegistrationContext(object()))
        self.core.Header.StructSize = abi.NevercCoreAPI.QueryInterface.offset
        with self.assertRaisesRegex(RuntimeError, "StructSize"):
            scope.core.function("QueryInterface")

    def test_status_failure_is_typed(self):
        failed = abi.NevercStatus()
        failed.Code = abi.NEVERC_STATUS_INVALID_ARGUMENT
        failed.Flags = 3
        failed.Detail = 99
        with self.assertRaises(ffi.NevercError) as raised:
            ffi.require_ok(failed, "NevercCoreAPI.QueryInterface")
        self.assertEqual(raised.exception.code, abi.NEVERC_STATUS_INVALID_ARGUMENT)
        self.assertEqual(raised.exception.detail, 99)

    def test_borrowed_views_check_scope_on_every_read(self):
        storage = ctypes.create_string_buffer(b"hello")
        native_view = abi.NevercStringView()
        native_view.Data = ctypes.cast(storage, ctypes.POINTER(ctypes.c_char))
        native_view.Length = 5
        scope = ffi.from_context(api.RegistrationContext(object()))
        view = ffi.StringView(scope, native_view)
        self.assertEqual(view.text, "hello")
        self.native.active = False
        with self.assertRaisesRegex(RuntimeError, "no longer active"):
            _ = view.text

    def test_transaction_resolves_and_destroys_exactly_once(self):
        scope = ffi.from_context(api.RegistrationContext(object()))
        events = []
        transaction = ffi.Transaction(
            scope,
            commit=lambda: events.append("commit"),
            abort=lambda: events.append("abort"),
            destroy=lambda: events.append("destroy"),
        )
        with transaction:
            transaction.commit()
        self.assertEqual(events, ["commit", "destroy"])
        with self.assertRaisesRegex(RuntimeError, "resolved"):
            transaction.abort()

        events.clear()
        with self.assertRaisesRegex(ValueError, "explode"):
            with ffi.Transaction(
                scope,
                commit=lambda: events.append("commit"),
                abort=lambda: events.append("abort"),
                destroy=lambda: events.append("destroy"),
            ):
                raise ValueError("explode")
        self.assertEqual(events, ["abort", "destroy"])

    def test_continuation_is_one_shot_and_scope_checked(self):
        scope = ffi.from_context(api.RegistrationContext(object()))
        continuation = ffi.OneShotContinuation(scope, lambda value: value + 1)
        self.assertEqual(continuation(4), 5)
        with self.assertRaisesRegex(RuntimeError, "already invoked"):
            continuation(4)

        stale = ffi.OneShotContinuation(scope, lambda: None)
        self.native.active = False
        with self.assertRaisesRegex(RuntimeError, "no longer active"):
            stale()

    def test_callback_binding_has_explicit_transfer_and_failure_release(self):
        scope = ffi.from_context(api.RegistrationContext(object()))
        descriptor = abi.NevercObserverDescriptor()
        descriptor.Header.StructSize = ctypes.sizeof(descriptor)
        with ffi.bind_callbacks(
            scope, descriptor, {"Callback": lambda scope, frame, point: None}
        ) as bound:
            self.assertEqual(bound.pointer.contents.Header.StructSize,
                             ctypes.sizeof(descriptor))
            bound.transfer()
        self.assertEqual(self.native.transferred, [1])
        self.assertEqual(self.native.released, [])

        with ffi.bind_callbacks(scope, descriptor, {}) as untransferred:
            self.assertFalse(untransferred.transferred)
        self.assertEqual(self.native.released, [2])

    def test_decode_record_owns_callback_bytes(self):
        handle = abi.NevercHandle()
        handle.Owner = 12
        handle.Value = 34
        raw = ctypes.string_at(ctypes.addressof(handle), ctypes.sizeof(handle))
        decoded = ffi.decode_record(abi.NevercHandle, raw)
        self.assertEqual((decoded.Owner, decoded.Value), (12, 34))


if __name__ == "__main__":
    unittest.main()
