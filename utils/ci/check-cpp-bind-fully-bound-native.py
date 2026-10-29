"""Native reference semantics only; no private frontend or generated program."""
from pathlib import Path
import argparse, hashlib, json, os, platform, subprocess, traceback
parser=argparse.ArgumentParser();parser.add_argument('--repo',type=Path,required=True)
parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
root=args.repo.resolve();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
sha=lambda path:hashlib.sha256(Path(path).read_bytes()).hexdigest()
report={'passed':False,'completed':False,'native_only':True,'reference_native_only':True,
        'private_frontend_requests':0,'generated_programs':0,'formal_xml':0,
        'private_support_complete':False,'published_to_dev':False,'goal_complete':False,
        'commands':[],'cases':[]}
def save():(out/'results.json').write_text(json.dumps(report,indent=2)+chr(10))
def run(command,label):
    command=list(map(str,command));log=out/(label+'.log');log.parent.mkdir(parents=True,exist_ok=True)
    with log.open('w') as stream:
        try:code=subprocess.run(command,cwd=root,stdout=stream,stderr=subprocess.STDOUT,timeout=60).returncode
        except subprocess.TimeoutExpired:
            stream.write('NATIVE COMMAND TIMED OUT'+chr(10));code=-1000
    report['commands'].append(dict(command=command,returncode=code,timeout_seconds=60,
        log=str(log.relative_to(out)),log_sha256=sha(log)))
    save();return code
try:
    assert os.environ.get('GITHUB_ACTIONS') == 'true'
    assert platform.system()=='Linux' and platform.machine()=='x86_64'
    report['revision']=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip()
    files=['utils/ci/check-cpp-bind-fully-bound-native.py','utils/ci/cpp-bind-fully-bound-native-plan.json','.github/workflows/diagnose-cpp-template-source.yml']
    report['source_sha256']={name:sha(root/name) for name in files}
    sdk=root/'neverc/lib/Translate/Cpp/SDK';catalog=json.loads((sdk/'catalog.json').read_text())
    report['sdk_catalog_sha256']=sha(sdk/'catalog.json')
    assert report['sdk_catalog_sha256']=='1497f7d37860d32d9e53798fbcdc0fe473a4c4e66ac05fa81a45910adfb1d167'
    headers=catalog['headers']+catalog['metadata'];assert len(headers)==665
    for row in headers:assert sha(sdk/row['root']/row['path'])==row['sha256']
    report['sdk_files_verified']=665
    clangxx=Path('/usr/bin/clang++-20')
    assert run([clangxx,'--version'],'clang-version')==0
    assert 'clang version 20.1.8' in (out/'clang-version.log').read_text()
    report['compiler_sha256']=sha(clangxx)
    flags = [clangxx, '--no-default-config', '-std=c++17', '-stdlib=libc++',
             '-nostdinc++', '-resource-dir', sdk / 'resource',
             '-isystem', sdk / 'libcxx', '-fno-fast-math', '-ffp-contract=off',
             '-D_LIBCPP_REMOVE_TRANSITIVE_INCLUDES', '-D_NEVERC_CPP_CORE_V2_SDK',
             '-D_LIBCPP___ITERATOR_ISTREAM_ITERATOR_H', '-D_LIBCPP___ITERATOR_ISTREAMBUF_ITERATOR_H',
             '-D_LIBCPP___ITERATOR_OSTREAM_ITERATOR_H', '-D_LIBCPP___ITERATOR_OSTREAMBUF_ITERATOR_H',
             '-D_LIBCPP_DISABLE_VISIBILITY_ANNOTATIONS']
    report['native_flags']=list(map(str,flags))
    plan=json.loads((root/'utils/ci/cpp-bind-fully-bound-native-plan.json').read_text());report['plan']=plan
    assert len(plan['cases'])==7
    for case in plan['cases']:
        folder=out/case['name'];folder.mkdir();source=folder/'input.cpp'
        source.write_text(case['source']);assert sha(source)==case['source_sha256']
        modes=[]
        for mode in ['-O0','-O2']:
            program=folder/('native'+mode)
            built=run(flags+[mode,source,'-o',program],case['name']+'/'+mode+'-build')
            executed=run([program],case['name']+'/'+mode+'-run') if built==0 else None
            record=dict(mode=mode,build_returncode=built,run_returncode=executed,passed=built==0 and executed==0)
            if built==0:
                data=program.read_bytes();assert data[:6]==b'\x7fELF\x02\x01' and data[18:20]==b'\x3e\x00'
                record.update(binary_sha256=sha(program),binary_bytes=len(data),elf_header_hex=data[:64].hex())
            modes.append(record)
        report['cases'].append(dict(name=case['name'],source_sha256=sha(source),modes=modes))
        save()
    report['completed']=True
    report['passed']=len(report['cases'])==7 and all(m['passed'] for c in report['cases'] for m in c['modes'])
    save()
except Exception:
    report['error']=traceback.format_exc();save();print(report['error'],flush=True)
raise SystemExit(0 if report['passed'] else 1)
