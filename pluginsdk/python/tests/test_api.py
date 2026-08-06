from __future__ import annotations

import sys
import types
import unittest

from neverc_plugin import (
    Concurrency,
    DependencyKind,
    InterfaceRequirement,
    InterfaceStability,
    Phase,
    Plugin,
    PluginDependency,
    Reentrancy,
)
from neverc_plugin import api
from neverc_plugin import phases


class FakeNative:
    def __init__(self) -> None:
        self.options = []
        self.observers = []
        self.diagnostics = []
        self.stale = False

    def register_option(self, handle, descriptor):
        if self.stale:
            raise RuntimeError("stale NeverC context")
        self.options.append((handle, descriptor))

    def register_observer(self, handle, high, low, name, points, callback):
        if self.stale:
            raise RuntimeError("stale NeverC context")
        self.observers.append((handle, high, low, name, points, callback))

    def emit_diagnostic(self, handle, severity, message, code):
        if self.stale:
            raise RuntimeError("stale NeverC context")
        self.diagnostics.append((handle, severity, message, code))

    def option_values(self, handle, spelling):
        if self.stale:
            raise RuntimeError("stale NeverC context")
        return ("one", "two")

    def check_cancelled(self, handle):
        if self.stale:
            raise RuntimeError("stale NeverC context")

    def frame_arguments(self, handle):
        if self.stale:
            raise RuntimeError("stale NeverC context")
        return (("-O2", 0, "command line", 3),)


class PluginDecoratorTests(unittest.TestCase):
    def make_module(self, source: str, name: str = "neverc_test_plugin"):
        module = types.ModuleType(name)
        module.__dict__["Plugin"] = Plugin
        sys.modules[name] = module
        self.addCleanup(sys.modules.pop, name, None)
        exec(source, module.__dict__)
        return module

    def test_records_strict_metadata(self):
        module = self.make_module(
            """
@Plugin(id="org.neverc.test-python", name="Test", version="1.2.3-rc.1+build.5")
class TestPlugin:
    pass
"""
        )
        spec = module.__neverc_plugin__
        self.assertEqual(spec.id, "org.neverc.test-python")
        self.assertEqual(spec.version_info, (1, 2, 3))
        self.assertEqual(spec.prerelease, "rc.1")
        self.assertEqual(spec.build_metadata, "build.5")
        self.assertIs(spec.plugin_class, module.TestPlugin)

    def test_rejects_bad_ids_and_versions(self):
        for plugin_id in ("", "Org.plugin", "org..plugin", "org.-bad"):
            with self.subTest(plugin_id=plugin_id), self.assertRaises(ValueError):
                Plugin(id=plugin_id, name="Test", version="1.0.0")
        for version in ("1", "01.0.0", "1.0.0-01", "1.0.0+"):
            with self.subTest(version=version), self.assertRaises(ValueError):
                Plugin(id="org.neverc.test", name="Test", version=version)

    def test_rejects_two_plugins_in_one_module(self):
        with self.assertRaises(RuntimeError):
            self.make_module(
                """
@Plugin(id="org.neverc.first", name="First", version="1.0.0")
class First:
    pass
@Plugin(id="org.neverc.second", name="Second", version="1.0.0")
class Second:
    pass
""",
                "neverc_duplicate_plugin",
            )

    def test_records_complete_native_metadata(self):
        module = types.ModuleType("neverc_metadata_plugin")
        module.__dict__.update(
            Plugin=Plugin,
            InterfaceRequirement=InterfaceRequirement,
            PluginDependency=PluginDependency,
        )
        sys.modules[module.__name__] = module
        self.addCleanup(sys.modules.pop, module.__name__, None)
        exec(
            '''
@Plugin(
    id="org.neverc.metadata",
    name="Metadata",
    version="2.3.4",
    concurrency="thread_safe",
    reentrancy="allowed",
    required_interfaces=(InterfaceRequirement("IR_BUILDER", minimum_minor=1),),
    optional_interfaces=(InterfaceRequirement("MIR"),),
    dependencies=(
        PluginDependency(
            "org.neverc.base", minimum="1.2.0", maximum="2.0.0",
            kind="after", allow_prerelease=True,
        ),
    ),
)
class MetadataPlugin:
    pass
''',
            module.__dict__,
        )
        spec = module.__neverc_plugin__
        self.assertEqual(spec.concurrency, Concurrency.THREAD_SAFE)
        self.assertEqual(spec.reentrancy, Reentrancy.ALLOWED)
        self.assertEqual(spec.required_interfaces[0].interface, "IR_BUILDER")
        self.assertEqual(spec.required_interfaces[0].minimum_minor, 1)
        self.assertEqual(spec.optional_interfaces[0].stability, InterfaceStability.STABLE)
        self.assertEqual(spec.dependencies[0].kind, DependencyKind.AFTER)
        self.assertEqual(spec.dependencies[0].minimum_info, (1, 2, 0))
        self.assertEqual(spec.dependencies[0].maximum_info, (2, 0, 0))

    def test_rejects_invalid_complete_metadata(self):
        with self.assertRaises(ValueError):
            InterfaceRequirement("DOES_NOT_EXIST")
        with self.assertRaises(ValueError):
            InterfaceRequirement((1, 2))
        with self.assertRaises(ValueError):
            PluginDependency("org.neverc.base", minimum="2.0.0", maximum="1.0.0")
        with self.assertRaises(ValueError):
            PluginDependency(
                "org.neverc.base",
                minimum="2.0.0-alpha",
                maximum="1.0.0-beta",
            )
        self.assertEqual(
            PluginDependency(
                "org.neverc.base",
                minimum="2.0.0-alpha",
                maximum="2.0.0",
            ).maximum_info,
            (2, 0, 0),
        )
        requirement = InterfaceRequirement("IR_BUILDER")
        with self.assertRaises(ValueError):
            Plugin(
                id="org.neverc.bad",
                name="Bad",
                version="1.0.0",
                abi_flags=1,
            )
        with self.assertRaises(ValueError):
            Plugin(
                id="org.neverc.bad",
                name="Bad",
                version="1.0.0",
                required_interfaces=(requirement,),
                optional_interfaces=(requirement,),
            )


class PhaseTests(unittest.TestCase):
    def test_phase_is_immutable_and_bounded(self):
        phase = Phase(1, 2, "neverc.test.phase")
        with self.assertRaises(Exception):
            phase.low = 4
        with self.assertRaises(ValueError):
            Phase(-1, 0, "bad")
        with self.assertRaises(TypeError):
            Phase(True, 0, "bad")
        with self.assertRaises(ValueError):
            Phase(0, 0, "")

    def test_generated_catalog_contains_every_builtin_phase(self):
        self.assertEqual(len(phases.ALL_PHASES), 130)
        self.assertEqual(len(phases.BY_ID), 130)
        self.assertEqual(len(phases.BY_NAME), 130)
        self.assertEqual(
            sum(len(items) for items in phases.DOMAIN_PHASES.values()), 130
        )


class ContextTests(unittest.TestCase):
    def setUp(self):
        self.previous_native = api._native
        self.native = FakeNative()
        api._native = self.native

    def tearDown(self):
        api._native = self.previous_native

    def test_option_and_observer_mapping(self):
        context = api.RegistrationContext("registration")
        context.option(
            "--trace",
            kind="flag",
            help="trace phases",
            aliases=("--trace-too",),
        )
        descriptor = self.native.options[0][1]
        self.assertEqual(descriptor["form"], 0)
        self.assertEqual(descriptor["aliases"], ("--trace-too",))

        context.option(
            "--mode",
            kind="separate",
            value_type="enum",
            enum_values={"fast": 1, "safe": 2},
        )
        enum_descriptor = self.native.options[1][1]
        self.assertEqual(enum_descriptor["argument_count"], 0)
        self.assertEqual(enum_descriptor["enum_values"], (("fast", 1), ("safe", 2)))

        callback = lambda frame: None
        phase = Phase(0x10, 0x20, "neverc.test.phase")
        returned = context.observer(
            phase, when=("before", "after"), fn=callback
        )
        self.assertIs(returned, callback)
        self.assertEqual(self.native.observers[0][1:6], (0x10, 0x20, "neverc.test.phase", 3, callback))

    def test_observer_decorator_form(self):
        context = api.RegistrationContext("registration")
        phase = Phase(1, 2, "neverc.test.phase")

        @context.observer(phase, when="after")
        def callback(frame):
            return None

        self.assertIs(self.native.observers[0][-1], callback)

    def test_frame_services_and_stale_errors(self):
        frame = api.Frame(
            "frame",
            {
                "phase_high": 1,
                "phase_low": 2,
                "phase_name": "neverc.driver.raw_arguments",
                "when": "before",
                "session": (3, 4),
                "task": (5, 6),
                "target_triple": "x86_64-linux-gnu",
                "cpu": "",
                "features": "",
                "object_format": "elf",
                "execution_level": 1,
                "input_handle": (7, 8),
                "output_handle": (0, 0),
            },
        )
        self.assertEqual(frame.arguments[0].value, "-O2")
        self.assertEqual(frame.option_values("--trace"), ("one", "two"))
        frame.emit_remark("seen", code=1001)
        self.assertEqual(self.native.diagnostics[0][1:], (1, "seen", 1001))
        self.native.stale = True
        with self.assertRaisesRegex(RuntimeError, "stale"):
            frame.arguments

    def test_rejects_inconsistent_option_shapes(self):
        context = api.RegistrationContext("registration")
        with self.assertRaises(ValueError):
            context.option("--bad", kind="flag", value_type="string")
        with self.assertRaises(ValueError):
            context.option("--bad", kind="separate", argument_count=1)
        with self.assertRaises(ValueError):
            context.option("--bad", kind="multi_arg", argument_count=0)
        with self.assertRaises(ValueError):
            context.option("--bad", value_type="enum")
        with self.assertRaises(ValueError):
            context.option("--bad", enum_values={"value": 1})
        with self.assertRaises(ValueError):
            context.option(
                "--bad", value_type="enum", enum_values={"value": True}
            )
        with self.assertRaises(TypeError):
            context.option("--bad", required=1)
        with self.assertRaises(ValueError):
            context.option("--bad", kind="multi_arg", argument_count=True)
        with self.assertRaises(ValueError):
            context.option("bad")


if __name__ == "__main__":
    unittest.main()
