"""Qualify fixture syntax and reference runtime; no NeverC support claim."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import signal
import subprocess
import time
import traceback


def sha(path):
    with Path(path).open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()


def main():
    p=argparse.ArgumentParser();p.add_argument('--repo',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
    a=p.parse_args();root=a.repo.resolve();out=a.out.resolve();out.mkdir()
    work=Path(os.environ['RUNNER_TEMP'])/'deleted-param-reference-v768';work.mkdir()
    plan=json.loads((root/'utils/ci/windows-stl-diagnostic-plan.json').read_text())
    report=dict(revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),
        plan=plan,commands=[],syntax=[],native_programs=[],abi_queries=[],completed=False,passed=False,
        diagnostic_only=True,fixture_fix_validated=False,actual_c23_executions=0,
        private_support_verified=False,full_stl_complete=False)
    def save():(out/'results.json').write_text(json.dumps(report,indent=2)+'\n')
    env=dict(os.environ)
    for key in ['CPATH','CPLUS_INCLUDE_PATH','C_INCLUDE_PATH','OBJC_INCLUDE_PATH','SDKROOT',
                'CCC_OVERRIDE_OPTIONS','CLANG_CONFIG_FILE_SYSTEM_DIR','CLANG_CONFIG_FILE_USER_DIR']:
        env.pop(key,None)
    def run(argv,label,timeout=60):
        argv=list(map(str,argv));log=out/(label+'.log');start=time.monotonic()
        with log.open('w') as stream:
            process=subprocess.Popen(argv,cwd=root,env=env,stdout=stream,stderr=subprocess.STDOUT,
                                     creationflags=subprocess.CREATE_NEW_PROCESS_GROUP)
            try:code=process.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                subprocess.run(['taskkill','/PID',str(process.pid),'/T','/F'],stdout=stream,stderr=subprocess.STDOUT,timeout=15)
                process.wait(timeout=15);code=-1000
        report['commands'].append(dict(label=label,command=argv,returncode=code,timeout_seconds=timeout,
            elapsed_seconds=time.monotonic()-start,log=log.name,log_sha256=sha(log)))
        save();print(label,code,flush=True);return code,log.read_text(errors='replace')
    save()
    try:
        assert len(plan['programs'])==7 and len(plan['target_abis'])==6
        assert all('i686' not in target for target in plan['target_abis'])
        for path,digest in plan['input_sha256'].items():assert sha(root/path)==digest
        sdk=root/'neverc/lib/Translate/Cpp/SDK'
        actual={f.relative_to(sdk).as_posix():sha(f) for f in sdk.rglob('*') if f.is_file()}
        assert actual==plan['sdk_input_sha256'] and len(actual)==673
        catalog=json.loads((sdk/'catalog.json').read_text())
        assert sha(sdk/'catalog.json')==plan['sdk_catalog_sha256']
        assert len(catalog['headers'])+len(catalog['metadata'])==665
        for row in catalog['headers']+catalog['metadata']:assert sha(sdk/row['root']/row['path'])==row['sha256']
        reference=Path(r'C:\Program Files\LLVM\bin\clang++.exe')
        code,version=run([reference,'--version'],'reference-version')
        assert code==0 and '20.1.8' in version
        report['reference_compiler_sha256']=sha(reference);report['sdk_files_verified']=665
        flags=[*plan['reference_base_arguments']]
        assert flags[-1] in ['-target','--target','--target=']
        flags=[flag for flag in flags[:-1] if flag!='-fsyntax-only']
        core=[*plan['reference_core_arguments'],'-Xclang','-fms-compatibility-version=0',
              '-resource-dir',sdk/'resource','-isystem',sdk/'libcxx','-isystem',sdk/'resource/include']
        for index,row in enumerate(plan['programs']):
            for form in ['before','after']:
                path=work/(str(index)+'-'+form+'.cpp');path.write_text(row[form],encoding='utf-8',newline='\n')
                assert sha(path)==row[form+'_sha256']
                for target_index,target in enumerate(plan['target_abis']):
                    label='syntax-'+str(index)+'-'+form+'-'+str(target_index)
                    code,text=run([reference,*flags,'--target='+target,*core,'-fsyntax-only',path],label)
                    expected=1 if form=='before' and 'windows' in target else 0
                    satisfied=(code==0) if expected==0 else (code==1 and 'deleted function' in text)
                    report['syntax'].append(dict(name=row['name'],form=form,target=target,returncode=code,
                        expected_returncode=expected,source_sha256=sha(path),expectation_satisfied=satisfied,label=label))
                    save()
            if row['runtime_reference_required']:
                path=work/(str(index)+'-after.cpp')
                for optimization in ['-O0','-O2']:
                    name='runtime-'+str(index)+optimization;binary=work/(name+'.exe')
                    code,_=run([reference,*flags,'--target=x86_64-pc-windows-msvc',*core,
                                optimization,'-fno-inline',path,'-o',binary],name+'-compile',120)
                    assert code==0 and binary.is_file()
                    code,_=run([binary],name+'-execute')
                    report['native_programs'].append(dict(name=row['name'],optimization=optimization,
                        source_sha256=sha(path),binary_sha256=sha(binary),returncode=code))
                    save();assert code==0
        assert len(report['commands'])==89 and len(report['syntax'])==84 and len(report['native_programs'])==2
        passed=all(row['expectation_satisfied'] for row in report['syntax'])
        report.update(completed=True,passed=passed,fixture_fix_validated=passed,actual_cpp_reference_executions=2)
        save();return 0 if passed else 1
    except BaseException:
        report['failure']=traceback.format_exc();save();raise


if __name__=='__main__':raise SystemExit(main())
