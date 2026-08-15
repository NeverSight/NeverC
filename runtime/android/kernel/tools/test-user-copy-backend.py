#!/usr/bin/env python3
"""Unit tests for user-copy artifact symbol policy."""

from __future__ import annotations

import importlib.util
import unittest
from pathlib import Path


CHECKER_PATH = Path(__file__).with_name("check-user-copy-backend.py")
SPEC = importlib.util.spec_from_file_location(
    "check_user_copy_backend", CHECKER_PATH
)
assert SPEC is not None and SPEC.loader is not None
CHECKER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CHECKER)

SAFE_SYMBOLS = frozenset(
    {
        "neverc_krt_mem_read_user",
        "neverc_krt_mem_write_user",
        "_neverc_krt_mem_copy_from_user_compat",
        "_neverc_krt_mem_copy_to_user_compat",
        "_neverc_krt_simple_read_from_buffer",
        "_neverc_krt_simple_write_to_buffer",
    }
)


def nm_output(
    defined: set[str] | frozenset[str],
    *,
    prefix: str = "",
    suffix: str = "",
    undefined: set[str] | frozenset[str] = frozenset(),
) -> str:
    lines = [
        f"{index:016x} t {prefix}{symbol}{suffix}"
        for index, symbol in enumerate(sorted(defined), 1)
    ]
    lines.extend(f"                 U {symbol}" for symbol in sorted(undefined))
    return "\n".join(lines) + "\n"


class ArtifactSymbolPolicyTest(unittest.TestCase):
    def violations(
        self,
        defined: set[str] | frozenset[str],
        *,
        prefix: str = "",
        undefined: set[str] | frozenset[str] = frozenset(),
    ) -> list[str]:
        symbols = CHECKER.parse_defined_symbols(
            nm_output(defined, prefix=prefix, undefined=undefined)
        )
        return CHECKER.artifact_symbol_violations(symbols)

    def test_complete_safe_symbol_set_passes(self) -> None:
        self.assertEqual(self.violations(SAFE_SYMBOLS), [])

    def test_legacy_state_definitions_fail(self) -> None:
        for legacy in (
            "_neverc_krt_copy_from_user",
            "_neverc_krt_copy_to_user",
        ):
            with self.subTest(legacy=legacy):
                violations = self.violations(SAFE_SYMBOLS | {legacy})
                self.assertTrue(any(legacy in item for item in violations))

    def test_every_required_definition_is_enforced(self) -> None:
        for missing in SAFE_SYMBOLS:
            with self.subTest(missing=missing):
                violations = self.violations(SAFE_SYMBOLS - {missing})
                self.assertTrue(any(missing in item for item in violations))

    def test_undefined_symbol_does_not_satisfy_requirement(self) -> None:
        missing = "neverc_krt_mem_read_user"
        violations = self.violations(
            SAFE_SYMBOLS - {missing}, undefined={missing}
        )
        self.assertTrue(any(missing in item for item in violations))

    def test_runtime_local_prefix_is_normalized(self) -> None:
        self.assertEqual(
            self.violations(SAFE_SYMBOLS, prefix="__neverc_nvk_local."),
            [],
        )

    def test_parallel_codegen_runtime_local_suffix_is_normalized(self) -> None:
        runtime_locals = frozenset(
            {
                "_neverc_krt_simple_read_from_buffer",
                "_neverc_krt_simple_write_to_buffer",
            }
        )
        symbols = CHECKER.parse_defined_symbols(
            nm_output(SAFE_SYMBOLS - runtime_locals)
            + nm_output(
                runtime_locals,
                prefix="__neverc_nvk_local.",
                suffix=".__pcg3298071600",
            )
        )
        self.assertEqual(CHECKER.artifact_symbol_violations(symbols), [])

    def test_prefixed_legacy_state_still_fails(self) -> None:
        legacy = "_neverc_krt_copy_from_user"
        violations = self.violations(
            SAFE_SYMBOLS | {legacy}, prefix="__neverc_nvk_local."
        )
        self.assertTrue(any(legacy in item for item in violations))

    def test_parallel_codegen_legacy_state_still_fails(self) -> None:
        legacy = "_neverc_krt_copy_from_user"
        symbols = CHECKER.parse_defined_symbols(
            nm_output(SAFE_SYMBOLS)
            + nm_output(
                {legacy},
                prefix="__neverc_nvk_local.",
                suffix=".__pcg3298071600",
            )
        )
        violations = CHECKER.artifact_symbol_violations(symbols)
        self.assertTrue(any(legacy in item for item in violations))


if __name__ == "__main__":
    unittest.main()
