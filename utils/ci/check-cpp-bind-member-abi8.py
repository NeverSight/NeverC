"""Actions-only reference capture; LLVM text, no linking or private translation."""
from pathlib import Path
import argparse, hashlib, json, os, platform, re, signal, subprocess, traceback

def sha(path):
    with Path(path).open('rb')as f:return hashlib.file_digest(f,'sha256').hexdigest()

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--repo',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
    root=args.repo.resolve();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    report=dict(passed=False,completed=False,reference_only=True,compile_only=True,
        private_frontend_requests=0,generated_programs=0,formal_xml=0,
        private_member_bind_support_verified=False,full_stl_complete=False,commands=[],targets=[])
    def save():(out/'results.json').write_text(json.dumps(report,indent=2)+'\n')
    def run(command,label):
        command=list(map(str,command));log=out/(label+'.log')
        with log.open('w')as stream:
            process=subprocess.Popen(command,cwd=root,stdout=stream,stderr=subprocess.STDOUT,start_new_session=True)
            try:code=process.wait(timeout=60)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid,signal.SIGKILL);process.wait();stream.write('\nABI COMMAND TIMED OUT\n');code=-1000
        report['commands'].append(dict(command=command,pid=process.pid,returncode=code,
            timeout_seconds=60,log=log.name,log_sha256=sha(log)))
        save();return code
    try:
        assert os.environ.get('GITHUB_ACTIONS')=='true'
        assert platform.system()=='Linux'and platform.machine()=='x86_64'
        report['revision']=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip()
        source=root/'utils/ci/cpp-bind-member-abi8-probe.cpp'
        plan_path=root/'utils/ci/cpp-bind-member-abi8-plan.json'
        plan=json.loads(plan_path.read_bytes());report['plan']=plan
        assert plan['preparation_only']is False and plan['fixture_group']=='bind-member-pointer-reference'
        assert plan['required_commands']==9 and plan['required_ir_products']==8
        targets=['x86_64-unknown-linux-gnu','aarch64-unknown-linux-gnu','x86_64-apple-darwin',
            'arm64-apple-darwin','x86_64-pc-windows-msvc','aarch64-pc-windows-msvc',
            'i686-pc-windows-msvc','i686-unknown-linux-gnu']
        assert plan['target_abis']==targets and sha(source)==plan['source_sha256']
        report['source_sha256']={str(p.relative_to(root)):sha(p)for p in [source,plan_path,
            root/'utils/ci/check-cpp-bind-member-abi8.py',root/'.github/workflows/diagnose-cpp-template-source.yml']}
        sdk=root/'neverc/lib/Translate/Cpp/SDK';catalog=json.loads((sdk/'catalog.json').read_bytes())
        assert sha(sdk/'catalog.json')==plan['sdk_catalog_sha256']=='1497f7d37860d32d9e53798fbcdc0fe473a4c4e66ac05fa81a45910adfb1d167'
        rows=catalog['headers']+catalog['metadata'];assert len(rows)==plan['sdk_files']==665
        for row in rows:assert sha(sdk/row['root']/row['path'])==row['sha256']
        report['sdk_files_verified']=665;report['sdk_catalog_sha256']=sha(sdk/'catalog.json')
        clang=Path('/usr/bin/clang++-20')
        assert run([clang,'--version'],'clang-version')==0
        assert 'clang version 20.1.8'in(out/'clang-version.log').read_text()
        report['compiler_sha256']=sha(clang)
        flags=[clang,'--no-default-config','-x','c++','-std=c++17','-nostdinc','-nostdinc++',
            '-Wc++20-extensions','-Wc++23-extensions','-Wc++26-extensions','-Werror=writable-strings',
            '-fno-ms-compatibility','-fno-ms-extensions','-D_LIBCPP_NO_VCRUNTIME',
            '-fno-delayed-template-parsing','-D_LIBCPP_REMOVE_TRANSITIVE_INCLUDES',
            '-D_NEVERC_CPP_CORE_V2_SDK','-D_LIBCPP___ITERATOR_ISTREAM_ITERATOR_H',
            '-D_LIBCPP___ITERATOR_ISTREAMBUF_ITERATOR_H','-D_LIBCPP___ITERATOR_OSTREAM_ITERATOR_H',
            '-D_LIBCPP___ITERATOR_OSTREAMBUF_ITERATOR_H','-D_LIBCPP_DISABLE_VISIBILITY_ANNOTATIONS',
            '-fno-fast-math','-ffp-contract=off','-resource-dir',sdk/'resource',
            '-isystem',sdk/'libcxx','-isystem',sdk/'resource/include']
        report['common_flags']=list(map(str,flags))
        names=re.findall(r'\b(nct_member_\w+)\s*=',source.read_text())
        assert len(names)==len(set(names))==23
        for index,target in enumerate(targets):
            ir=out/('abi-'+str(index)+'.ll')
            code=run(flags+['-target',target,'-S','-emit-llvm','-O0',source,'-o',ir],'abi-'+str(index))
            entry=dict(target=target,compile_returncode=code,passed=False)
            if code==0:
                text=ir.read_text();globals={}
                for name in names:
                    lines=re.findall(r'^@'+re.escape(name)+r'\s*=.*$',text,re.M)
                    assert len(lines)==1,(target,name)
                    globals[name]=lines[0]
                triples=re.findall(r'^target triple = "([^"]+)"$',text,re.M)
                layouts=re.findall(r'^target datalayout = "([^"]+)"$',text,re.M)
                assert len(triples)==len(layouts)==1
                entry.update(passed=True,ir=ir.name,ir_sha256=sha(ir),ir_bytes=ir.stat().st_size,
                    actual_llvm_target=triples[0],actual_llvm_data_layout=layouts[0],actual_global_initializers=globals)
            report['targets'].append(entry);save()
        report['completed']=True
        report['passed']=len(report['targets'])==8 and len(report['commands'])==9 and all(x['passed']for x in report['targets'])
        save()
    except Exception:
        report['error']=traceback.format_exc();save();print(report['error'],flush=True)
    raise SystemExit(0 if report['passed']else 1)

if __name__=='__main__':main()
