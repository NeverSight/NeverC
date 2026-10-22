"""Check the replay CLI against the real driver source without running C++."""
import ast
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]


def request_contract(runner, driver):
    required = []
    for position in [2, 4]:
        match = re.search(r'std::strcmp\(Argv\[' + str(position) + r'\], "([^"]+)"\) == 0', driver)
        assert match, 'The real driver argument contract must be present'
        required.append(match.group(1))
    calls = []
    for node in ast.walk(ast.parse(runner)):
        if not isinstance(node, ast.Call) or not isinstance(node.func, ast.Name) or node.func.id != 'run' or not node.args:
            continue
        command = node.args[0]
        if not isinstance(command, ast.List):
            continue
        values = [item.value if isinstance(item, ast.Constant) else None for item in command.elts]
        if '--request' not in values:
            continue
        assert len(values) == 6 and values[1] == '__neverc_cpp_frontend'
        assert [values[2], values[4]] == required
        calls.append(command)
    assert len(calls) == 1
    assert 'NEVERC_CPP_PROTOCOL_EXECUTION_DIR' in runner
    assert 'std::getenv("NEVERC_CPP_PROTOCOL_EXECUTION_DIR")' in driver


class FrontendReplayProtocolTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.runner = (ROOT / 'utils/ci/check-cpp-current216-elf-combined-808.py').read_text()
        cls.driver = (ROOT / 'utils/ci/FrontendProtocolDriver.cpp').read_text()

    def test_current_replay_matches_real_driver(self):
        request_contract(self.runner, self.driver)

    def test_actual_failed_response_flag_is_rejected(self):
        bad = self.runner.replace("'--output', response]", "'--response', response]", 1)
        self.assertNotEqual(bad, self.runner)
        with self.assertRaises(AssertionError):
            request_contract(bad, self.driver)

    def test_missing_pid_environment_is_rejected(self):
        with self.assertRaises(AssertionError):
            request_contract(self.runner.replace('NEVERC_CPP_PROTOCOL_EXECUTION_DIR', 'INVALID_INVOCATION_DIR'), self.driver)


if __name__ == '__main__':
    unittest.main()
