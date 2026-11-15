"""Use explicit authenticated producer paths; implement no compiler behavior."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import subprocess
import sys

parser = argparse.ArgumentParser()
for name in ['repo', 'component', 'baseline', 'plan', 'output', 'build', 'ir-plan', 'runtime-report', 'explicit-runtime-report']:
    parser.add_argument('--' + name, type=Path, required=True)
args = parser.parse_args()
assert os.environ.get('GITHUB_ACTIONS') == 'true'
plan = json.loads(args.plan.read_text())
provenance = json.loads((args.component / 'provenance.json').read_text())
assert provenance['component_commit'] == plan['component_commit']
assert provenance['component_report_sha256'] == plan['component_report_sha256']
assert provenance['private_archive_sha256'] == plan['private_archive_sha256']
assert provenance['private_prefix_sha256'] == plan['private_prefix_sha256']
for key in ['component_report', 'private_archive', 'private_prefix']:
    path = Path(provenance[key])
    assert path.resolve().is_relative_to(args.component.resolve()) and path.is_file()
    with path.open('rb') as stream:
        assert hashlib.file_digest(stream, 'sha256').hexdigest() == provenance[key + '_sha256']
ir_plan = json.loads(args.ir_plan.read_bytes())
assert not ir_plan['preparation_only'] and ir_plan['execution_binding_required']
assert ir_plan['required_original_member_ir_not_replaced'] and ir_plan['required_original_full221_regression']
with args.explicit_runtime_report.open('rb') as stream:
    assert hashlib.file_digest(stream, 'sha256').hexdigest() == ir_plan['actual_runtime12_report_sha256']
command = [sys.executable, str(args.repo / 'utils/ci/check-cpp-bind-explicit-result-target-ir.py'),
    '--repo', str(args.repo), '--component-report', provenance['component_report'],
    '--runtime-report', str(args.runtime_report), '--plan', str(args.ir_plan),
    '--explicit-runtime-report', str(args.explicit_runtime_report),
    '--archive', provenance['private_archive'],
    '--private-prefix', provenance['private_prefix'], '--output', str(args.output), '--build', str(args.build)]
raise SystemExit(subprocess.run(command, check=False).returncode)
