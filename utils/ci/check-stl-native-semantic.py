"""Diagnose original native failures with an existing component and upstream Clang 20 in Actions."""
from pathlib import Path, PurePosixPath
import argparse
import hashlib
import importlib.util
import json
import os
import platform
import re
import stat
import subprocess
import traceback
import zipfile


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--repo',type=Path,required=True)
    parser.add_argument('--out',type=Path,required=True)
    args=parser.parse_args()
    assert os.environ.get('GITHUB_ACTIONS')=='true' and platform.system()=='Linux'
    root,out=args.repo.resolve(),args.out.resolve()
    out.mkdir(parents=True,exist_ok=False)
    work=out.parent/'stl-native-semantic-work'
    work.mkdir(exist_ok=False)
    plan=json.loads((root/'utils/ci/stl-native-semantic-plan.json').read_text())
    corpus=json.loads((root/'utils/ci/stl-native-semantic-corpus.json').read_text())
    result=dict(diagnostic_only=True,completed=False,revision=os.environ['GITHUB_SHA'],
        plan=plan,commands=[],programs=[],layout_queries=[],actual_c23_executions=0,full_stl_complete=False)
    env=dict(os.environ)
    for name in ['CPATH','CPLUS_INCLUDE_PATH','C_INCLUDE_PATH','OBJC_INCLUDE_PATH','SDKROOT',
                 'CCC_OVERRIDE_OPTIONS','CLANG_CONFIG_FILE_SYSTEM_DIR','CLANG_CONFIG_FILE_USER_DIR']:
        env.pop(name,None)

    def run(command,label,timeout=60):
        command=list(map(str,command))
        p=subprocess.run(command,capture_output=True,timeout=timeout,env=env,cwd=root)
        data=p.stdout+p.stderr
        assert len(data)<=2*1024**2,label
        path=out/(label+'.log')
        path.write_bytes(data)
        result['commands'].append(dict(command=command,label=label,returncode=p.returncode,
            log=path.name,log_sha256=sha(path)))
        return p.returncode,data.decode('utf-8',errors='replace')

    try:
        for path,identity in plan['input_sha256'].items():
            assert sha(root/path)==identity,path
        assert sha(root/'utils/ci/stl-native-semantic-corpus.json')==plan['corpus_sha256']
        assert len(corpus['programs'])==23 and len(corpus['target_abis'])==6
        assert all(t.startswith(('x86_64-','arm64-','aarch64-'))for t in corpus['target_abis'])
        sdk=root/'neverc/lib/Translate/Cpp/SDK'
        for path,identity in plan['sdk_input_sha256'].items():
            assert sha(sdk/path)==identity,path
        spec=importlib.util.spec_from_file_location('_remote_semantic_artifact_download',
            root/'utils/ci/download-windows-arm-sdk-baseline.py')
        download=importlib.util.module_from_spec(spec)
        spec.loader.exec_module(download)
        token=os.environ['GITHUB_TOKEN']
        producer=download.request('actions/runs/42917656752',token)
        assert producer['head_sha']==corpus['compiler_component_revision'] and producer['run_attempt']==1
        assert producer['status']=='completed' and producer['conclusion']=='success'
        job=download.request('actions/jobs/130184139067',token)
        assert job['run_id']==producer['id'] and job['head_sha']==producer['head_sha'] and job['conclusion']=='success'
        artifact=download.request('actions/artifacts/13814169939',token)
        assert not artifact['expired'] and artifact['workflow_run']['id']==producer['id']
        assert artifact['workflow_run']['head_sha']==producer['head_sha']
        assert artifact['digest']=='sha256:'+corpus['compiler_binary_artifact_sha256']
        archive=work/'qualified-component-binaries.zip'
        download.request('actions/artifacts/13814169939/zip',token,archive)
        assert sha(archive)==corpus['compiler_binary_artifact_sha256']
        driver=work/'private-frontend-runtime-driver'
        with zipfile.ZipFile(archive)as source:
            entries=[e for e in source.infolist() if PurePosixPath(e.filename).name==driver.name]
            entry,=entries
            assert not stat.S_ISLNK(entry.external_attr>>16)
            assert entry.file_size==corpus['compiler_driver']['bytes']
            with source.open(entry)as data,driver.open('xb')as target:
                while chunk:=data.read(1024*1024):target.write(chunk)
        assert sha(driver)==corpus['compiler_driver']['sha256']
        assert driver.read_bytes()[:64].hex()==corpus['compiler_driver']['first_64_bytes_hex']
        driver.chmod(0o755)
        result['authenticated_existing_component']=dict(run_id=producer['id'],job_id=job['id'],
            commit=producer['head_sha'],artifact_id=artifact['id'],archive_sha256=sha(archive),
            driver_sha256=sha(driver),compiler_rebuilt=False)
        code,text=run([driver,'__neverc_cpp_frontend','--version'],'component-version')
        assert code==0 and '20.1.8' in text
        reference='/usr/bin/clang++-20'
        code,text=run([reference,'--version'],'upstream-reference-version')
        assert code==0 and '20.1.8' in text
        common=plan['reference_base_arguments']
        core=plan['reference_core_arguments']

        def reference_args(target):
            return [reference,*common,target,*core,
                '-fno-fast-math','-ffp-contract=off','-resource-dir',sdk/'resource',
                '-isystem',sdk/'libcxx','-isystem',sdk/'resource/include',
                # The embedded Action sets this actual LangOption before macros.
                '-Xclang','-fms-compatibility-version=0']

        for index,row in enumerate(corpus['programs']):
            assert hashlib.sha256(row['source'].encode()).hexdigest()==row['source_sha256']
            source=work/('original-'+str(index)+'.cpp')
            source.write_bytes(row['source'].encode())
            for target_index,target in enumerate(corpus['target_abis']):
                label='case-'+str(index)+'-target-'+str(target_index)
                output=work/(label+'.nc')
                private_code,_=run([driver,'translate','--from','cpp',source,'--profile','cpp-core-v2',
                    '--target',target,'-o',output,'--','-std=c++17'],label+'-component')
                reference_code,_=run([*reference_args(target),source],label+'-upstream')
                result['programs'].append(dict(name=row['name'],source_sha256=row['source_sha256'],target=target,
                    private_returncode=private_code,upstream_syntax_returncode=reference_code,
                    generated_c23_sha256=sha(output)if output.is_file()else None,
                    generated_c23_executed=False))
            print('Observed original program',index+1,'of',len(corpus['programs']),flush=True)

        source=work/'sdk-layout.cpp'
        source.write_text('''#include <optional>
#include <tuple>
#ifdef _LIBCPP_ABI_MICROSOFT
#define ABI 1
#else
#define ABI 0
#endif
#ifdef _MSC_VER
#define MSC _MSC_VER
#else
#define MSC 0
#endif
struct E{};
template<unsigned long long...>struct actual_sdk_layout;
actual_sdk_layout<sizeof(std::optional<bool>),sizeof(std::optional<int>),sizeof(std::optional<void*>),
alignof(std::optional<int>),sizeof(std::tuple<>),sizeof(std::tuple<E,E>),
__has_declspec_attribute(empty_bases),ABI,MSC> actual;
''')
        result['layout_source_sha256']=sha(source)
        for target_index,target in enumerate(corpus['target_abis']):
            for implementation in ['component','upstream']:
                if implementation=='component':
                    command=[driver,'translate','--from','cpp',source,'--profile','cpp-core-v2','--target',target,
                        '-o',work/('layout-'+str(target_index)+'.nc'),'--','-std=c++17']
                else:command=[*reference_args(target),source]
                code,text=run(command,'layout-'+str(target_index)+'-'+implementation)
                assert code!=0
                values=re.findall(r'actual_sdk_layout<([^>]+)>',text)
                assert values,(target,implementation,text[:3000])
                result['layout_queries'].append(dict(target=target,implementation=implementation,
                    actual_diagnostics=values))
        assert len(result['programs'])==23*6 and len(result['layout_queries'])==12
        result['completed']=True
        # Completion means all diagnostic observations were collected, not that
        # their original translation or full STL expectations passed.
        return 0
    except Exception:
        result['failure']=traceback.format_exc()
        print(result['failure'],flush=True)
        return 1
    finally:
        (out/'results.json').write_text(json.dumps(result,indent=2)+'\n')


if __name__=='__main__':raise SystemExit(main())
