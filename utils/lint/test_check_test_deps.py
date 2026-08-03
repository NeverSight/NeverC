import contextlib
import importlib.util
import io
import json
import tempfile
import unittest
from pathlib import Path


SCRIPT = Path(__file__).with_name("check-test-deps.py")
SPEC = importlib.util.spec_from_file_location("check_test_deps", SCRIPT)
CHECK_TEST_DEPS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CHECK_TEST_DEPS)


class CheckTestDepsTest(unittest.TestCase):
    def run_validation_fixture(self, manifest, sources=None, headers=None):
        sources = sources or {}
        headers = headers or {}

        with tempfile.TemporaryDirectory() as temp_dir:
            repo = Path(temp_dir)
            manifest_path = repo / "std" / "manifest.json"
            tests_cpp = repo / "tests" / "neverc" / "StdLibTests.cpp"
            manifest_path.parent.mkdir(parents=True)
            tests_cpp.parent.mkdir(parents=True)
            manifest_path.write_text(json.dumps(manifest))
            tests_cpp.write_text("")

            for relative_path, contents in sources.items():
                path = repo / "std" / relative_path
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(contents)
            for relative_path, contents in headers.items():
                path = repo / "std" / relative_path
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(contents)

            original_paths = (
                CHECK_TEST_DEPS.REPO,
                CHECK_TEST_DEPS.MANIFEST,
                CHECK_TEST_DEPS.TESTS_CPP,
            )
            CHECK_TEST_DEPS.REPO = repo
            CHECK_TEST_DEPS.MANIFEST = manifest_path
            CHECK_TEST_DEPS.TESTS_CPP = tests_cpp
            output = io.StringIO()
            try:
                with contextlib.redirect_stdout(output):
                    result = CHECK_TEST_DEPS.main()
            finally:
                (
                    CHECK_TEST_DEPS.REPO,
                    CHECK_TEST_DEPS.MANIFEST,
                    CHECK_TEST_DEPS.TESTS_CPP,
                ) = original_paths

        return result, output.getvalue()

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

    def test_test_parser_expands_multiple_dependency_macros(self):
        source = r'''
#define TCP_DEPS \
    "src/net/tcp/tcp.c", "src/context/context.c"
#define HTTP_TLS_DEPS \
    "src/crypto/tls/tls.c"
STD_TEST(http, "src/net/http/http.c", TCP_DEPS, HTTP_TLS_DEPS)
STD_TEST(tcp, TCP_DEPS)
        '''

        self.assertEqual(
            CHECK_TEST_DEPS.parse_tests(source),
            {
                "http": {
                    "src/net/http/http.c",
                    "src/net/tcp/tcp.c",
                    "src/context/context.c",
                    "src/crypto/tls/tls.c",
                },
                "tcp": {
                    "src/net/tcp/tcp.c",
                    "src/context/context.c",
                },
            },
        )

    def test_main_rejects_missing_header_for_non_network_module(self):
        result, output = self.run_validation_fixture(
            {
                "modules": {
                    "fmt": {
                        "header": "include/neverc/std/fmt.h",
                        "symbols": {"neverc_fmt_print": "src/fmt.c"},
                    }
                }
            },
            sources={
                "src/fmt.c": "int neverc_fmt_print(void) { return 0; }\n"
            },
        )

        self.assertEqual(result, 1)
        self.assertIn("header 'include/neverc/std/fmt.h' does not exist", output)

    def test_main_rejects_unregistered_non_network_public_api(self):
        result, output = self.run_validation_fixture(
            {
                "modules": {
                    "fmt": {
                        "header": "include/neverc/std/fmt.h",
                        "symbols": {"neverc_fmt_anchor": "src/fmt.c"},
                    }
                }
            },
            sources={
                "src/fmt.c": (
                    "int neverc_fmt_anchor(void) { return 0; }\n"
                    "int neverc_fmt_print(void) { return 0; }\n"
                )
            },
            headers={
                "include/neverc/std/fmt.h": "int neverc_fmt_print(void);\n"
            },
        )

        self.assertEqual(result, 1)
        self.assertIn("public std API 'neverc_fmt_print'", output)
        self.assertIn("is not registered in manifest.json", output)


if __name__ == "__main__":
    unittest.main()
