from __future__ import annotations

import ctypes
import unittest

from neverc_plugin import abi
from neverc_plugin import domains


class DomainParityTests(unittest.TestCase):
    def test_all_manifest_interfaces_have_domain_entry_points(self):
        self.assertEqual(len(domains.ALL_INTERFACES), 36)
        self.assertEqual(set(domains.BY_NAME), set(abi.INTERFACE_SPECS))
        for interface in domains.ALL_INTERFACES:
            with self.subTest(interface=interface.name):
                spec = abi.INTERFACE_SPECS[interface.name]
                self.assertIs(interface.table_type, getattr(abi, spec["table"]))
                self.assertEqual(interface.high, spec["high"])
                self.assertEqual(interface.low, spec["low"])
                self.assertEqual(interface.major, spec["major"])
                self.assertEqual(interface.minor, spec["minor"])

    def test_every_interface_slot_has_an_exact_raw_binder(self):
        slots = []
        for interface in domains.ALL_INTERFACES:
            table = interface.table_type
            for field, field_type in table._fields_:
                symbol = f"{table.__name__}.{field}"
                if field_type is ctypes.c_void_p and symbol in abi.FUNCTION_SIGNATURES:
                    slots.append(symbol)
                    self.assertIsNotNone(abi.function_type(symbol))
        self.assertEqual(len(slots), 713)
        self.assertEqual(len(slots), len(set(slots)))

    def test_every_registrar_operation_is_reachable_through_raw_tables(self):
        registrar_tables = {
            interface.table_type for interface in domains.ALL_INTERFACES
        }
        registrar_tables.add(abi.NevercRegistrarAPI)
        operations = []
        for table in registrar_tables:
            for field, _ in table._fields_:
                symbol = f"{table.__name__}.{field}"
                if field.startswith("Register"):
                    operations.append(symbol)
                    self.assertIn(symbol, abi.FUNCTION_SIGNATURES)
        self.assertEqual(len(operations), 30)


if __name__ == "__main__":
    unittest.main()
