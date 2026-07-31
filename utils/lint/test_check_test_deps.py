import importlib.util
import unittest
from pathlib import Path


SCRIPT = Path(__file__).with_name("check-test-deps.py")
SPEC = importlib.util.spec_from_file_location("check_test_deps", SCRIPT)
CHECK_TEST_DEPS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CHECK_TEST_DEPS)


class CheckTestDepsTest(unittest.TestCase):
    def test_function_parser_ignores_comments_and_literals(self):
        source = r'''
        /* int neverc_fake(void) { return 0; } */
        static const char *text = "neverc_string_fake() {";
        static int neverc_private(void) {
            return 0;
        }
        int neverc_real(
            int value
        ) {
            return value;
        }
        '''

        self.assertEqual(
            CHECK_TEST_DEPS.c_function_symbols(source, "{"),
            {"neverc_real"},
        )

    def test_definition_aliases_resolve_to_emitted_target(self):
        definitions = {
            "neverc_lock_init",
            "neverc_direct_definition",
        }
        aliases = {
            "neverc_mutex_init": "neverc_sync_mutex_init",
            "neverc_lock_init": "neverc_mutex_init",
            "neverc_missing_alias": "neverc_missing",
        }

        self.assertEqual(
            CHECK_TEST_DEPS.preprocessed_definitions(
                definitions, aliases
            ),
            {
                "neverc_sync_mutex_init",
                "neverc_direct_definition",
            },
        )

    def test_dependency_sources_include_transitive_modules_once(self):
        modules = {
            "root": {
                "deps": ["middle"],
                "symbols": {"neverc_root": "src/root.c"},
            },
            "middle": {
                "deps": ["leaf"],
                "symbols": {"neverc_middle": "src/middle.c"},
            },
            "leaf": {
                "deps": ["root"],
                "symbols": {"neverc_leaf": "src/leaf.c"},
            },
        }
        errors = []

        self.assertEqual(
            CHECK_TEST_DEPS.dependency_source_files(
                "root", modules, errors
            ),
            {"src/middle.c", "src/leaf.c"},
        )
        self.assertEqual(errors, [])


if __name__ == "__main__":
    unittest.main()
