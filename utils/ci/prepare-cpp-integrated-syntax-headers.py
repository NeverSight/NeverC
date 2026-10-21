#!/usr/bin/env python3
"""Prepare genuine pinned private Clang headers for remote syntax checks."""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess
import sys
import tarfile
import urllib.request

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--repo',required=True,type=Path)
parser.add_argument('--output',required=True,type=Path)
args=parser.parse_args();root=args.repo.resolve();output=args.output.resolve()
output.mkdir(parents=True,exist_ok=False)
pin='6898f963c8e938981e6c4a302e83ec5beb4630147c7311183cf61069af16333d'
url='https://github.com/llvm/llvm-project/releases/download/llvmorg-20.1.8/llvm-project-20.1.8.src.tar.xz'
archive=output/'llvm-project-20.1.8.src.tar.xz'
with urllib.request.urlopen(url,timeout=60) as response,archive.open('wb') as stream:
    while data:=response.read(1024*1024):stream.write(data)
def h(path):
    with path.open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()
assert h(archive)==pin,'Pinned upstream source archive mismatch'
with tarfile.open(archive) as source:
    for member in source:
        path=Path(member.name)
        assert not path.is_absolute() and '..' not in path.parts
        assert path.parts[0]=='llvm-project-20.1.8.src'
        source.extract(member,output,filter='data')
upstream=output/'llvm-project-20.1.8.src'
headers=['clang/include/clang/AST/ASTConsumer.h','clang/include/clang/AST/DeclCXX.h',
         'clang/include/clang/Sema/Overload.h','clang/include/clang/Sema/Sema.h',
         'clang/include/clang/Sema/TemplateDeduction.h','clang/include/clang/Sema/DelayedDiagnostic.h']
before={name:h(upstream/name) for name in headers}
patcher=root/'neverc/lib/Translate/Cpp/Frontend/IsolateSymbols.py'
prefix=output/'NeverCCppPrivatePrefix.h';log=output/'prepare-private-headers.log'
with log.open('w') as stream:
    command=[sys.executable,str(patcher),'--source',str(upstream),'--output',str(prefix)]
    result=subprocess.run(command,cwd=root,stdout=stream,stderr=subprocess.STDOUT,timeout=180)
report={'passed':result.returncode==0,'source_url':url,'source_archive_sha256':h(archive),
        'patcher_sha256':h(patcher),'command':command,'returncode':result.returncode,
        'log':str(log),'log_sha256':h(log),'prefix':str(prefix),'prefix_sha256':h(prefix),
        'upstream':str(upstream),'original_header_sha256':before,
        'patched_header_sha256':{name:h(upstream/name) for name in headers},
        'private_bridge_linked':False,'syntax_only':True}
(output/'provenance.json').write_text(json.dumps(report,indent=2)+'\n')
assert report['passed'],'Official private source preparation failed'
print('Prepared SHA-pinned LLVM20.1.8 headers with unchanged official IsolateSymbols.py.',flush=True)
