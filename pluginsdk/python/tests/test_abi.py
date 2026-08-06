from __future__ import annotations

import ctypes
import unittest

from neverc_plugin import abi


class GeneratedABITests(unittest.TestCase):
    def test_complete_inventory_and_layouts(self):
        self.assertEqual(len(abi.PUBLIC_RECORDS), 366)
        self.assertEqual(len(abi.FUNCTION_SIGNATURES), 993)
        self.assertEqual(set(abi.ABI_LAYOUTS), {
            "aarch64-le-64-sysv",
            "aarch64-le-64-win",
            "x86_64-le-64-sysv",
            "x86_64-le-64-win",
        })
        abi.validate_all_layouts()

    def test_tagged_and_anonymous_records_are_usable(self):
        self.assertEqual(ctypes.sizeof(abi.NevercLinkRequest), 632)
        self.assertEqual(ctypes.sizeof(abi.NevercLTORequest), 584)
        operand = abi.NevercMCOperandValue()
        operand.Payload.TargetExtension.Kind = 7
        self.assertEqual(operand.Payload.TargetExtension.Kind, 7)

    def test_raw_function_binder_calls_exact_slot(self):
        symbol = "NevercIRPassDescriptor.DestroyUserData"
        seen = []
        callback_type = abi.function_type(symbol)

        @callback_type
        def destroy(user_data):
            seen.append(user_data)

        descriptor = abi.NevercIRPassDescriptor()
        descriptor.DestroyUserData = ctypes.cast(destroy, ctypes.c_void_p).value
        bound = abi.bind_function(descriptor, "DestroyUserData")
        bound(ctypes.c_void_p(0x1234))
        self.assertEqual(seen, [0x1234])

    def test_constants_and_typedef_aliases_are_exported(self):
        self.assertEqual(abi.NEVERC_STATUS_OK, 0)
        self.assertEqual(abi.NEVERC_IR_PRESERVE_ALL, 1 << 63)
        self.assertEqual(abi.NEVERC_PHASE_DRIVER_RAW_ARGUMENTS_DOMAIN, "driver")
        self.assertIs(abi.NevercTaskHandle, abi.NevercHandle)


if __name__ == "__main__":
    unittest.main()
