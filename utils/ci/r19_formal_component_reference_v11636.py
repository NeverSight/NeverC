"""Link the runtime archive to its independently authenticated source report."""

import argparse
from hashlib import sha256
import json
from pathlib import Path
import re

import r19_formal_full_build_log_packing_v11432 as archive
from r19_formal_sdk_bundle_v10902 import BOOTSTRAP, FORMAL, checked_file


ENTRY = "r19_formal_component_reference_v11636.py"
COMPONENT = "authenticated-component-results.json"
PROVENANCE = "component-provenance.json"
REFERENCE = "component-report-reference.json"


def reference(provenance, report):
    commit = provenance["component_commit"]
    run = provenance["component_run_id"]
    digest = provenance["component_report_sha256"]
    assert re.fullmatch("[0-9a-f]{40}", commit)
    assert type(run) is int and run > 0 and re.fullmatch("[0-9a-f]{64}", digest)
    assert report["passed"] and report["completed"]
    assert report["component_producer_revision"] == commit
    assert report["component_report_sha256"] == digest
    return dict(component_commit=commit, component_run_id=run, component_report_sha256=digest)


def retain_reference(root):
    root = root.resolve()
    bootstrap = root / BOOTSTRAP
    component = checked_file(bootstrap, COMPONENT, archive.MEMBER_LIMIT)
    provenance = json.loads(checked_file(bootstrap, PROVENANCE, archive.MEMBER_LIMIT))
    report = json.loads(checked_file(root / FORMAL, "results.json", archive.MEMBER_LIMIT))
    consumed = reference(provenance, report)
    assert sha256(component).hexdigest() == consumed["component_report_sha256"]
    with (bootstrap / REFERENCE).open("xb") as output:
        output.write((json.dumps(consumed, sort_keys=True, separators=(",", ":")) + "\n").encode())
    # The original source artifact retains these bytes. Remove the hard link
    # from the packed copy after confirming the runtime consumed the same report.
    (bootstrap / COMPONENT).unlink()


def verify_reference(results, plan):
    root = results.resolve().parent.parent
    bootstrap = root / BOOTSTRAP
    provenance = json.loads(checked_file(bootstrap, PROVENANCE, archive.MEMBER_LIMIT))
    consumed = json.loads(checked_file(bootstrap, REFERENCE, 1024))
    report = json.loads(checked_file(root / FORMAL, "results.json", archive.MEMBER_LIMIT))
    assert consumed == reference(provenance, report)
    assert consumed == {name: plan[name] for name in consumed}
    assert not (bootstrap / COMPONENT).exists()


def pack(formal, bootstrap, output, count):
    prior = archive.stage

    def stage(formal, bootstrap, output, count):
        prior(formal, bootstrap, output, count)
        retain_reference(output)

    archive.stage = stage
    try:
        archive.pack(formal, bootstrap, output, count)
    finally:
        archive.stage = prior


def attach(append, base_append, workflow, count, job):
    import yaml
    from r19_bitset_zero_whole_policy_draft_v5642 import once

    updated, files, jobs = append(base_append, workflow, count, job)
    packer = "r19_formal_full_build_log_packing_v11432.py"
    assert ENTRY not in files and packer in files
    revised = once(updated, f'python3 "utils/ci/{packer}"', f'python3 "utils/ci/{ENTRY}"')
    changed = yaml.safe_load(revised)["jobs"]
    assert len(changed) == 56 and changed.keys() == jobs.keys()
    assert {name for name in jobs if changed[name] != jobs[name]} == {job}
    return revised, {**files, ENTRY: Path(__file__).read_bytes()}, changed


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("formal", type=Path)
    parser.add_argument("bootstrap", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("count", type=int)
    args = parser.parse_args()
    pack(args.formal, args.bootstrap, args.output, args.count)
