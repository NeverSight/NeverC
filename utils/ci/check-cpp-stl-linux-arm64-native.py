"""Run the selected bitset and optional translation tests on a native Linux ARM64 compiler."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import sys
import xml.etree.ElementTree as ET

PLAN = 'utils/ci/cpp-stl-linux-arm64-native-plan.json'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def image(path):
    with path.open('rb') as stream:
        header = stream.read(20)
    assert len(header) == 20 and header[:6] == b'\x7fELF\x02\x01' and header[18:] == b'\xb7\x00'
    return header.hex()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--repo',type=Path,required=True)
    parser.add_argument('--compiler',type=Path,required=True)
    parser.add_argument('--tests',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--prepare-tests',action='store_true')
    args = parser.parse_args()
    assert sys.platform=='linux' and platform.machine()=='aarch64'
    assert os.environ['GITHUB_ACTIONS']=='true' and os.environ['GITHUB_RUN_ATTEMPT']=='1'
    source = (args.repo/PLAN).read_bytes()
    plan = json.loads(source)
    revision = subprocess.check_output(['git','rev-parse','HEAD'],cwd=args.repo,text=True,timeout=30).strip()
    assert revision == os.environ['GITHUB_SHA']
    assert len(plan['private'])==19 and len(plan['sdk'])==695 and len(plan['cases'])==32
    for group in ('private','sdk'):
        assert all(sha((args.repo/name).read_bytes())==digest for name,digest in plan[group].items())
    for name,digest in plan['registered_tests'].items():
        clean = subprocess.check_output(['git','show',revision+':'+name],cwd=args.repo,timeout=30)
        assert sha(clean)==digest
    extractor = args.repo/plan['extractor_path']
    assert sha(extractor.read_bytes())==plan['extractor_sha256']
    source_path = 'tests/neverc/TranslateTests.cpp'
    source_file = args.repo/source_path
    original = subprocess.check_output(['git','show',revision+':'+source_path],cwd=args.repo,timeout=30)
    assert sha(original)==plan['registered_tests'][source_path]
    namespace = {}
    exec(compile(extractor.read_bytes(),str(extractor),'exec'),namespace)
    restorer = args.repo/plan['restorer_path']
    suffix = (args.repo/plan['suffix_path']).read_bytes()
    assert sha(restorer.read_bytes())==plan['restorer_sha256'] and sha(suffix)==plan['suffix_sha256']
    registry = {}
    exec(compile(restorer.read_bytes(),str(restorer),'exec'),registry)
    maintained = registry['restore_original_gtests'](original,suffix)
    assert sha(maintained)==plan['maintained_source_sha256']
    selected, facts = namespace['extract'](maintained,plan['cases'])
    digests = {name:row['sha256'] for name,row in facts['selected_original_test_bodies'].items()}
    assert digests==plan['focused_test_body_sha256'] and len(digests)==32
    assert facts['original_test_names']==plan['original_test_count']==7933
    assert facts['all_non_test_source_retained'] and not facts['selected_bodies_modified']
    assert sha(selected)==facts['generated_source_sha256']==plan['focused_source_sha256']
    if args.prepare_tests:
        assert source_file.read_bytes()==original
        source_file.write_bytes(selected)
        print(json.dumps(dict(selected=len(digests),source_sha256=sha(selected))),flush=True)
        return 0
    assert source_file.read_bytes()==selected
    headers = {'compiler':image(args.compiler),'google_test':image(args.tests)}
    target = subprocess.check_output([args.compiler,'-dumpmachine'],text=True,timeout=30).strip()
    assert target.startswith('aarch64-') and 'linux' in target
    assert len(set(plan['cases'])) == 32
    args.output.mkdir()
    xml = args.output/'stl-arm64.xml'
    command = [str(args.tests),'--gtest_filter='+':'.join(plan['cases']),
               '--gtest_output=xml:'+str(xml)]
    all_cases = []
    with (args.output/'gtest.log').open('xb') as output:
        try:
            rc = subprocess.run(command,stdout=output,stderr=subprocess.STDOUT,
                                timeout=900,check=False).returncode
        except subprocess.TimeoutExpired:
            rc = 124
    if xml.is_file():
        try:
            all_cases = list(ET.parse(xml).getroot().iter('testcase'))
        except ET.ParseError:
            pass
    names = [case.attrib['classname']+'.'+case.attrib['name'] for case in all_cases]
    success = rc==0 and len(names)==len(set(names))==32 and set(names)==set(plan['cases'])
    success &= all(case.attrib['status']=='run' and case.attrib['result']=='completed' and
                   all(case.find(tag) is None for tag in ('error','failure','skipped')) for case in all_cases)
    payload = dict(passed=success,revision=revision,product=plan['product'],
        run=int(os.environ['GITHUB_RUN_ID']),attempt=1,target=target,headers=headers,
        cases=sorted(names),command=command,returncode=rc,
        focus_source_sha256=sha(selected),test_body_sha256=digests,
        plan_sha256=sha(source),gtest_log_sha256=sha((args.output/'gtest.log').read_bytes()),
        xml_sha256=sha(xml.read_bytes()) if xml.is_file() else None)
    (args.output/'results.json').write_text(json.dumps(payload,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(dict(passed=success,tests=len(names),target=target)),flush=True)
    return 0 if success else 1


if __name__=='__main__':
    raise SystemExit(main())
