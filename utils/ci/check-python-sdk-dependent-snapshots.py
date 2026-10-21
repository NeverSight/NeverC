"""Actions-only faithful generator/check replay; no compiler or component build."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import platform
import re
import subprocess
import sys
import traceback

parser=argparse.ArgumentParser();parser.add_argument('--out',type=Path,required=True)
args=parser.parse_args();out=args.out.resolve();out.mkdir(parents=True,exist_ok=True)
root=Path(__file__).resolve().parents[2]
sha=lambda path:hashlib.sha256(Path(path).read_bytes()).hexdigest()
plan=json.loads((root/'utils/ci/python-sdk-dependent-snapshots-plan.json').read_text())
report=dict(completed=False,passed=False,revision=os.environ.get('GITHUB_SHA'),
    host=os.environ['NEVERC_SNAPSHOT_HOST'],python=sys.version,python_executable=sys.executable,
    machine=platform.machine(),plan=plan,commands=[],source_sha256={},
    canonical_files={},formal_xml_cases=0,neverc_or_private_cpp_rebuilt=False,
    published_to_dev=False,full_ci_passed=False,goal_complete=False)
def save():
    temporary=out/'results.new';temporary.write_text(json.dumps(report,indent=2)+'\n');temporary.replace(out/'results.json')
def run(argv,name,env=None):
    log=out/(name+'.log')
    try:
        r=subprocess.run([str(v) for v in argv],cwd=root,env=env,
            stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=60)
        log.write_bytes(r.stdout);code=r.returncode
    except subprocess.TimeoutExpired as error:
        log.write_bytes(error.stdout or b'');code=124
    report['commands'].append(dict(name=name,argv=[str(v) for v in argv],returncode=code,
        timeout_seconds=60,log=log.name,log_sha256=sha(log)))
    save();return code
save()
try:
    for name,digest in plan['expected_source_sha256'].items():
        assert sha(root/name)==digest,name
        report['source_sha256'][name]=digest
    clang=Path(os.environ['NEVERC_CLANG']);assert clang.is_file()
    report['actual_clang_path']=str(clang.resolve());report['actual_clang_sha256']=sha(clang)
    assert run([clang,'--version'],'clang-version')==0
    assert 'clang version 22.' in (out/'clang-version.log').read_text()
    generator=root/'utils/plugin-api/gen-python-abi.py'
    # Exact original CTest command, with the existing NEVERC_CLANG setting.
    gen_check=run([sys.executable,generator,'--check'],'original-generation-check')
    canonical_py=out/'canonical-abi.py';canonical_inc=out/'canonical-trampolines.inc'
    canonical=run([sys.executable,generator,'--output',canonical_py,
        '--trampolines-output',canonical_inc],'original-canonical-generation')
    if canonical==0:
        for name,path in zip(['pluginsdk/python/neverc_plugin/abi.py',
            'neverc/lib/Plugin/Python/PythonPluginTrampolines.inc'],[canonical_py,canonical_inc]):
            report['canonical_files'][name]=dict(path=path.name,sha256=sha(path),bytes=path.stat().st_size,
                byte_identical_to_committed_candidate=path.read_bytes()==(root/name).read_bytes())
    environment=dict(os.environ,PYTHONDONTWRITEBYTECODE='1',PYTHONPATH=str(root/'pluginsdk/python'))
    commands=[('original-abi-layout',['-p','test_abi.py','-v']),('original-sdk-tests',['-v'])]
    statuses=[]
    for name,options in commands:
        status=run([sys.executable,'-m','unittest','discover','-s',root/'pluginsdk/python/tests',*options],name,environment)
        log=(out/(name+'.log')).read_text(errors='replace')
        counts=re.findall(r'Ran (\d+) tests?',log)
        assert counts and int(counts[-1])>0
        assert not re.search(r'(?i)(?:skipped=|\.\.\. skipped|OK \(skipped)',log),'An original test was skipped.'
        report.setdefault('actual_unittest_counts',{})[name]=int(counts[-1]);statuses.append(status)
    for name in ['links','facts']:
        statuses.append(run([sys.executable,root/'utils/plugin-api'/('check-docs-'+name+'.py')],
            'original-docs-'+name,environment))
    report.update(completed=True,passed=bool(gen_check==0 and canonical==0 and
        len(report['canonical_files'])==2 and all(v['byte_identical_to_committed_candidate'] for v in report['canonical_files'].values()) and
        all(code==0 for code in statuses)))
    save()
except BaseException:
    report['failure']=traceback.format_exc();save();raise
raise SystemExit(0 if report['passed'] else 1)
