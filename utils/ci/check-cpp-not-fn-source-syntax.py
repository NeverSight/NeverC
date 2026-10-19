#!/usr/bin/env python3
"""Check preceding full source and not_fn source retention and descriptions against Clang 20 APIs.

This is syntax-only evidence. Private bridge linkage, retained Sema callbacks,
managed behavior, source guards, runtime modes and formal suites remain required.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys
import traceback
from pathlib import Path

BASE='89574033312b3bf5946cecf4a3d59903792c99d5'
PREFIX='neverc/lib/Translate/Cpp/Frontend/'
FILES=['Frontend.cpp','Lowering.cpp','MathSDK.cpp','Frontend.h']


def h(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream,'sha256').hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo',type=Path,required=True)
    parser.add_argument('--clangxx',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--headers',type=Path,required=True)
    args=parser.parse_args();root=args.repo.resolve();output=args.output.resolve()
    output.mkdir(parents=True,exist_ok=False)
    report={'passed':False,'completed':False,'syntax_only':True,'private_bridge_linked':False,
            'runtime_admission_implemented':False,'source_descriptions_only':False,'source_retention_only':False,'source_retention_executed':False,'native_sdk_configuration_fix_present':True,'native_sdk_configuration_runtime_verified':False,'source_completion_implemented':False,'descriptor_executed':False,'managed_behavior_verified':False,
            'goal_complete':False,'commands':[],'phases':[]}
    def save():
        (output/'results.json').write_text(json.dumps(report,indent=2)+'\n')
    def blob(revision,name):
        return subprocess.check_output(['git','show',revision+':'+name],cwd=root)
    def run(command,log,timeout=120):
        command=list(map(str,command))
        with log.open('w') as stream:
            try:
                result=subprocess.run(command,cwd=root,stdout=stream,stderr=subprocess.STDOUT,timeout=timeout)
                code=result.returncode
            except subprocess.TimeoutExpired:
                stream.write('\nSYNTAX CHECK TIMED OUT\n');code=-1
        report['commands'].append({'command':command,'returncode':code,'log':str(log),'log_sha256':h(log),'timeout_seconds':timeout})
        save();return code
    try:
        revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip()
        report.update(revision=revision,baseline_revision=BASE,compiler_sha256=h(args.clangxx))
        preparation=json.loads((args.headers/'provenance.json').read_text())
        assert preparation['passed'] and preparation['returncode']==0
        assert preparation['source_archive_sha256']=='6898f963c8e938981e6c4a302e83ec5beb4630147c7311183cf61069af16333d'
        assert preparation['patcher_sha256']==h(root/(PREFIX+'IsolateSymbols.py'))
        environment=output/'environment';environment.mkdir()
        shutil.copy2(args.headers/'provenance.json',environment/'provenance.json')
        shutil.copy2(args.headers/'prepare-private-headers.log',environment/'prepare-private-headers.log')
        prefix=Path(preparation['prefix']);assert h(prefix)==preparation['prefix_sha256']
        shutil.copy2(prefix,environment/'NeverCCppPrivatePrefix.h')
        upstream=Path(preparation['upstream'])
        for name,digest in preparation['patched_header_sha256'].items():
            assert h(upstream/name)==digest
            target=environment/name;target.parent.mkdir(parents=True,exist_ok=True)
            shutil.copy2(upstream/name,target)
        report['private_header_preparation']=preparation
        assert run([args.clangxx,'--version'],output/'clang-version.log')==0
        assert 'clang version 20.1.8' in (output/'clang-version.log').read_text()
        cmake=blob(BASE,PREFIX+'CMakeLists.txt').decode()
        inputs=re.search(r'foreach\(_input (.*?)\)\n  file\(SHA256',cmake,re.S).group(1).split()
        assert len(inputs)==19
        sdk=root/'neverc/lib/Translate/Cpp/SDK'
        assert h(sdk/'catalog.json')=='1497f7d37860d32d9e53798fbcdc0fe473a4c4e66ac05fa81a45910adfb1d167'
        report['sdk_catalog_sha256']=h(sdk/'catalog.json')
        for label,source_revision in [('preceding-full-source',BASE),('not-fn-source-draft',revision)]:
            directory=output/label;directory.mkdir()
            identities={}
            for name in FILES+['FrontendEntry.h']:
                data=blob(source_revision,PREFIX+name)
                (directory/name).write_bytes(data)
                identities[PREFIX+name]=hashlib.sha256(data).hexdigest()
            identity='builtin:llvm20.1.8:6898f963c8e938981e6c4a302e83ec5beb4630147c7311183cf61069af16333d'
            identity_inputs={}
            for name in inputs:
                data=blob(source_revision,PREFIX+name)
                digest=hashlib.sha256(data).hexdigest();identity+=':'+digest
                identity_inputs[name]=digest
            identity+=':'+h(sdk/'catalog.json')+':'+h(sdk/'embed.py')
            build_id=hashlib.sha256(identity.encode()).hexdigest()
            template=blob(source_revision,PREFIX+'BuildID.h.in').decode()
            (directory/'BuildID.h').write_text(template.replace('@NEVERC_CPP_BUILD_ID@',build_id))
            generated=directory/'BuiltinCppSdkData.h'
            assert run([sys.executable,sdk/'embed.py','--catalog',sdk/'catalog.json','--root',sdk,'--output',generated],
                       directory/'generate-sdk.log')==0
            phase={'name':label,'revision':source_revision,'source_sha256':identities,
                   'identity_input_sha256':identity_inputs,'build_id':build_id,
                   'build_header_sha256':h(directory/'BuildID.h'),'sdk_header_sha256':h(generated),'cases':[]}
            report['phases'].append(phase)
            for name in FILES[:-1]:
                log=directory/(name+'.syntax.log')
                code=run([args.clangxx,'-std=c++17','-fsyntax-only','-fexceptions','-fno-rtti','-DNDEBUG',
                          '-DCLANG_BUILD_STATIC','-I'+str(directory),'-I'+str(upstream/'clang/include'),
                          '-I/usr/lib/llvm-20/include','-include',prefix,directory/name],log)
                phase['cases'].append({'name':name,'returncode':code,'passed':code==0,
                                       'log':str(log),'log_sha256':h(log),'timeout_seconds':120})
                save();print(('PASS' if code==0 else 'FAIL')+' syntax '+label+' '+name,flush=True)
        report['completed']=True
        report['passed']=len(report['phases'])==2 and all(len(p['cases'])==3 and all(c['passed'] for c in p['cases']) for p in report['phases'])
        assert report['compiler_sha256']==h(args.clangxx)
        save();assert report['passed'],'Syntax/API check failed; inspect exact baseline and draft logs.'
        print('PASS two exact source snapshots / six Clang20 source-description syntax checks; runtime and private linkage unverified.',flush=True)
    except BaseException:
        report['failure']=traceback.format_exc();save();raise


if __name__=='__main__':
    main()
