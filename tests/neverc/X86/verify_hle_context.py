#!/usr/bin/env python3
"""Compare native HLE completion with independent raw prefixed instructions.

Every operation has an explicit PAUSE boundary. This exercises native fallback,
register/flag publication and fault atomicity; it does not claim hardware HLE
transaction commit or a generic RTM checkpoint implementation.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess

ROOT=Path(__file__).resolve().parents[3]
FIXTURE=ROOT/'tests/fixtures/x86/hle-context'

def digest(path): return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler',type=Path,default=ROOT/'build-neverc/bin/neverc')
    parser.add_argument('--backend-llc',type=Path)
    parser.add_argument('--new-pm',action='store_true')
    parser.add_argument('--llvm-only',action='store_true')
    parser.add_argument('--source',type=Path)
    parser.add_argument('--manifest',type=Path)
    parser.add_argument('--keep',type=Path,required=True)
    args=parser.parse_args()
    if (args.new_pm or args.llvm_only) and not args.backend_llc:
        parser.error('--new-pm and --llvm-only require --backend-llc')
    if args.llvm_only and args.source:
        parser.error('source compilation requires the NeverC frontend')
    if bool(args.source) != bool(args.manifest):
        parser.error('source qualification requires its manifest')
    manifest = None
    if args.source:
        manifest=json.loads(args.manifest.read_text())
        assert manifest['source_sha256']==digest(args.source)
        assert manifest['native_requests']==0 and manifest['request']['native_hle_steps'] is True
    directory=args.keep.resolve();directory.mkdir(parents=True,exist_ok=True)
    spec=importlib.util.spec_from_file_location('hle_fixture',FIXTURE/'generate.py')
    generator=importlib.util.module_from_spec(spec);spec.loader.exec_module(generator)
    forms=generator.generate(directory)
    if args.source:
        generator.generate_source(directory,args.source.read_text(),manifest)
    paths={'host':shutil.which('clang')}
    if not paths['host']:parser.error('host Clang is required')
    if not args.llvm_only: paths['compiler']=str(args.compiler.resolve())
    if args.backend_llc: paths['llc']=str(args.backend_llc.resolve())
    hashes={name:digest(p) for name,p in paths.items()}
    commands=[]
    def run(command):
        command=list(map(str,command))
        r=subprocess.run(command,cwd=directory,capture_output=True,text=True,timeout=180)
        commands.append({'argv':command,'returncode':r.returncode,'stdout':r.stdout,'stderr':r.stderr})
        (directory/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
        if r.returncode:raise RuntimeError(f'{command}\n{r.stdout}\n{r.stderr}')
        return r
    run([paths['host'],'-c','original.S','-o','original.o'])
    profiles=[]
    for opt in ('O0','O2'):
        if args.llvm_only:shutil.copyfile(directory/'candidate.ll',directory/(opt+'.ll'))
        else:
            flags=['-'+opt,'-std=c23','-ffreestanding','-fno-lto','-fno-builtin-mimalloc',
                   '-fno-builtin','-mno-red-zone','-fno-stack-protector','-fno-pie']
            run([paths['compiler'],'--target=x86_64-unknown-linux-gnu',*flags,
                 *(['-S','-emit-llvm'] if args.backend_llc else ['-c']),
                 'candidate.c','-o',opt+('.ll' if args.backend_llc else '.o')])
        if args.backend_llc:
            run([paths['llc'],'-mtriple=x86_64-unknown-linux-gnu','-filetype=obj','-verify-machineinstrs',
                 '-'+opt,*(['-enable-new-pm'] if args.new_pm else []),opt+'.ll','-o',opt+'.o'])
        exe=directory/opt
        run([paths['host'],'-O2','-no-pie',*(['-DHLE_SOURCE'] if args.source else []),'-I',directory,FIXTURE/'runner.c','original.o',opt+'.o','-o',exe])
        result=json.loads(run([exe]).stdout)
        assert result['pairs']==forms*256
        assert result['fault_before_publication']==forms*2
        profiles.append({'optimization':opt,**result})
    assert hashes=={name:digest(p) for name,p in paths.items()},'compiler changed during qualification'
    record={'passed':True,'forms':forms,'profiles':profiles,'new_pass_manager':args.new_pm,
            'llvm_only':args.llvm_only,'physical_transaction_commit_qualified':False,
            'scope':'native HLE operations with abort-before-publication boundaries',
            'undefined_flags_compared':False,
            'source_sha256':digest(args.source) if args.source else None,
            'manifest_sha256':digest(args.manifest) if args.manifest else None,
            'tools':{name:{'path':p,'sha256':hashes[name]} for name,p in paths.items()},
            'fixture_sha256':{p.name:digest(p) for p in FIXTURE.iterdir() if p.is_file()},
            'files':{p.name:digest(p) for p in directory.iterdir() if p.is_file() and p.name!='qualification.json'}}
    (directory/'qualification.json').write_text(json.dumps(record,indent=2)+'\n')
    print(json.dumps({'passed':True,'forms':forms,'profiles':profiles}))

if __name__=='__main__':main()
