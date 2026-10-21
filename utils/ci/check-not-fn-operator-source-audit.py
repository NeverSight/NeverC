"""Run original audit tests before any expensive C++ build; never grant runtime support."""
from pathlib import Path
import argparse, ast, hashlib, json, re, subprocess, sys, time
parser=argparse.ArgumentParser();parser.add_argument('--repo',required=True);parser.add_argument('--output',required=True)
args=parser.parse_args();root=Path(args.repo).resolve();out=Path(args.output).resolve();out.mkdir(parents=True,exist_ok=True)
sha=lambda path:hashlib.sha256(Path(path).read_bytes()).hexdigest()
prefix='neverc/lib/Translate/Cpp/Frontend/'
sources=[prefix+'IsolateSymbols.py',prefix+'AuditArchiveTests.py',prefix+'CMakeLists.txt','utils/ci/check-not-fn-operator-source-audit.py']
revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip()
report={'revision':revision,'source_sha256':{name:sha(root/name) for name in sources},'completed':False,'passed':False,
        'private_not_fn_admission_implemented':True,'not_fn_lowering_implemented':True,'cpp_units_compiled':0,
        'actual_frontend_requests':0,'formal_xml':0,'full_ci_passed':False,'published_to_dev':False,'goal_complete':False}
def save():(out/'results.json').write_text(json.dumps(report,indent=2)+'\n')
save()
assert sys.version_info[:3]==(3,12,10)
tree=ast.parse((root/(prefix+'AuditArchiveTests.py')).read_text())
report['original_test_names']=[node.name for node in ast.walk(tree) if isinstance(node,ast.FunctionDef) and node.name.startswith('test_')]
assert len(report['original_test_names'])==133
command=[sys.executable,'-E','-B',str(root/(prefix+'AuditArchiveTests.py'))]
start=time.monotonic()
log=out/'original-audit-tests.log'
with log.open('wb') as stream:
    completed=subprocess.run(command,cwd=root,stdout=stream,stderr=subprocess.STDOUT,timeout=120)
text=log.read_text()
counts=re.findall(r'^Ran (\d+) tests? in ',text,re.M)
assert counts==['133']
report.update(command=command,timeout_seconds=120,elapsed_seconds=time.monotonic()-start,
              returncode=completed.returncode,log_sha256=sha(log),actual_test_count=133,
              completed=True,passed=completed.returncode==0 and re.search(r'^OK$',text,re.M) is not None)
save()
raise SystemExit(0 if report['passed'] else 1)
