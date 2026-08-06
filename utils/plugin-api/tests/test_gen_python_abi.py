#!/usr/bin/env python3
"""Contract tests for the generated Python view of the NeverC public C ABI."""

from __future__ import annotations

import importlib.util
import json
import pathlib
import sys
import unittest


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[2]
GENERATOR = HERE.parent / "gen-python-abi.py"


def load_generator():
    spec = importlib.util.spec_from_file_location("gen_python_abi", GENERATOR)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"could not load {GENERATOR}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


class PythonABIGeneratorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.generator = load_generator()
        cls.inventory = cls.generator.collect_inventory()

    def test_inventory_covers_authoritative_sdk(self) -> None:
        manifest = json.loads(
            (ROOT / "pluginsdk/manifest/plugin.json").read_text(encoding="utf-8")
        )
        abi = json.loads(
            (ROOT / "pluginsdk/abi/plugin.json").read_text(encoding="utf-8")
        )
        expected_interfaces = {
            interface["name"]
            for module in manifest["modules"]
            for interface in module["interfaces"]
        }
        expected_records = set(
            next(iter(abi["abi_keys"].values()))["structs"]
        )

        self.assertEqual(len(manifest["modules"]), 15)
        self.assertEqual(len(expected_interfaces), 36)
        self.assertEqual(self.inventory.interfaces, expected_interfaces)
        self.assertEqual(set(self.inventory.interface_specs), expected_interfaces)
        self.assertEqual(
            {
                spec["table"]
                for spec in self.inventory.interface_specs.values()
            },
            {
                "NevercCoreAPI", "NevercIOAPI", "NevercSourceLocationAPI",
                "NevercDriverAPI", "NevercPrepAPI", "NevercASTAPI",
                "NevercParserAPI", "NevercSemaAPI", "NevercIRAnalysisAPI",
                "NevercIRBuilderAPI", "NevercIRCoreAPI", "NevercIRGenAPI",
                "NevercIROptimizationAPI", "NevercIRPassAPI",
                "NevercCallingConventionAPI", "NevercTargetAPI",
                "NevercTargetABIAPI", "NevercMIRAPI", "NevercMIRAnalysisAPI",
                "NevercMIRPassAPI", "NevercMIRProviderAPI",
                "NevercAssemblyProviderAPI", "NevercMCAPI",
                "NevercMCEmissionAPI", "NevercMCProviderAPI",
                "NevercObjectAPI", "NevercObjectFormatAPI",
                "NevercObjectPhaseAPI", "NevercLinkAPI", "NevercLinkPhaseAPI",
                "NevercLinkRegistrarAPI", "NevercLTOAPI",
                "NevercLTORegistrarAPI", "NevercDynCodeAPI",
                "NevercDynCodePhaseAPI", "NevercDynCodeRegistrarAPI",
            },
        )
        self.assertEqual(len(self.inventory.public_records), 366)
        self.assertEqual(self.inventory.public_records, expected_records)
        self.assertGreaterEqual(len(self.inventory.records), 368)
        self.assertEqual(len(self.inventory.function_fields), 815)
        self.assertEqual(len(self.inventory.callback_typedefs), 178)
        self.assertGreaterEqual(len(self.inventory.constants), 5_000)

    def test_finds_anonymous_mc_operand_payload_records(self) -> None:
        operand = self.inventory.records["NevercMCOperandValue"]
        payload = next(field for field in operand.fields if field.name == "Payload")
        self.assertEqual(payload.type.kind, "record")
        self.assertTrue(payload.type.name.startswith("_NevercMCOperandValue_Payload"))
        anonymous_union = self.inventory.records[payload.type.name]
        self.assertEqual(anonymous_union.kind, "union")
        self.assertIn("TargetExtension", {field.name for field in anonymous_union.fields})

    def test_preserves_function_pointer_signatures(self) -> None:
        callback = self.inventory.callback_typedefs["NevercIRPassRunFn"]
        self.assertEqual(callback.result.c_spelling, "NevercStatus")
        self.assertEqual(
            [argument.c_spelling for argument in callback.arguments],
            [
                "const NevercIRPassInvocation *",
                "NevercIRPreservedAnalyses *",
                "void *",
            ],
        )

        core = self.inventory.records["NevercIRCoreAPI"]
        get_value_kind = next(
            field for field in core.fields if field.name == "GetValueKind"
        )
        self.assertEqual(get_value_kind.type.kind, "function_pointer")
        self.assertEqual(get_value_kind.type.result.c_spelling, "NevercStatus")
        self.assertEqual(len(get_value_kind.type.arguments), 4)

    def test_every_userdata_callback_has_named_native_trampoline_metadata(self):
        callback_fields = {
            f"{record.name}.{field.name}": field.type
            for record in self.inventory.records.values()
            if any(candidate.name == "UserData" for candidate in record.fields)
            for field in record.fields
            if field.type.kind == "function_pointer"
        }
        self.assertEqual(len(callback_fields), 75)
        for symbol, callback in callback_fields.items():
            with self.subTest(symbol=symbol):
                self.assertEqual(len(callback.argument_names), len(callback.arguments))
                self.assertEqual(callback.argument_names.count("UserData"), 1)

        generated = self.generator.render_trampolines(self.inventory)
        self.assertEqual(generated, self.generator.render_trampolines(
            self.generator.collect_inventory()
        ))
        for symbol in callback_fields:
            self.assertIn(symbol, generated)
        self.assertIn("configurePythonCallbackRecord", generated)

    def test_resolves_constants_without_silent_omission(self) -> None:
        constants = self.inventory.constants
        self.assertEqual(constants["NEVERC_STATUS_OK"], 0)
        self.assertEqual(constants["NEVERC_IR_PASS_LEVEL_FUNCTION"], 3)
        self.assertEqual(
            constants["NEVERC_INTERFACE_IR_PASS_HIGH"], 0x4E43504952500001
        )
        self.assertEqual(
            constants["NEVERC_PHASE_DRIVER_RAW_ARGUMENTS_DOMAIN"], "driver"
        )
        self.assertEqual(
            constants["NEVERC_IR_PRESERVE_ALL"], 1 << 63
        )
        self.assertEqual(self.inventory.unresolved_constants, {})

    def test_render_is_deterministic_and_complete(self) -> None:
        first = self.generator.render_python(self.inventory)
        second = self.generator.render_python(self.generator.collect_inventory())
        self.assertEqual(first, second)
        self.assertIn("class NevercIRCoreAPI(ctypes.Structure):", first)
        self.assertIn("NEVERC_IR_PASS_LEVEL_FUNCTION = 3", first)
        self.assertIn("FUNCTION_SIGNATURES", first)
        self.assertIn("ABI_LAYOUTS", first)
        self.assertIn("def validate_layout", first)


if __name__ == "__main__":
    unittest.main()
