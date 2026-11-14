"""Check the real MSVC ARM64 build and unchanged native STL GTests in Actions."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import struct
import subprocess
import traceback
import xml.etree.ElementTree as ET

def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream,'sha256').hexdigest()

def pe(path):
    with path.open('rb') as stream:
        header=stream.read(64)
        assert len(header)==64 and header[:2]==b'MZ'
        offset=struct.unpack_from('<I',header,60)[0]
        stream.seek(offset)
        signature=stream.read(6)
        assert signature[:4]==b'PE\0\0' and struct.unpack_from('<H',signature,4)[0]==0xaa64
    return dict(sha256=sha(path),bytes=path.stat().st_size,first64=header.hex(),machine=0xaa64)

def main():
    parser=argparse.ArgumentParser()
    for name in ['repo','build','output']:
        parser.add_argument('--'+name,type=Path,required=True)
    args=parser.parse_args()
    root,build,out=args.repo.resolve(),args.build.resolve(),args.output.resolve()
    out.mkdir(parents=True,exist_ok=True)
    report=dict(revision=os.environ['GITHUB_SHA'],run_id=int(os.environ['GITHUB_RUN_ID']),
        run_attempt=int(os.environ['GITHUB_RUN_ATTEMPT']),completed=False,passed=False,
        full_stl_complete=False,host_target='aarch64-pc-windows-msvc')
    try:
        assert report['run_attempt']==1
        plan_path=root/'utils/ci/cpp-msvc-arm64-stl-plan.json'
        plan=json.loads(plan_path.read_text())
        report['plan_sha256']=sha(plan_path)
        inputs={}
        for name,digest in plan['input_sha256'].items():
            path=root/name
            assert not Path(name).is_absolute() and '..' not in Path(name).parts
            inputs[name]=sha(path)
            assert inputs[name]==digest
        report['input_sha256']=inputs
        report['exact_private_input_sha256']=plan['exact_private_input_sha256']
        report['exact_sdk_input_sha256']=plan['exact_sdk_input_sha256']
        log=out/'build.log'
        raw=log.read_bytes()
        report['build_log_raw_sha256']=sha(log)
        text=raw.decode('utf-8',errors='replace')
        marker='NeverC: disable MSVC19.52 ARM64 optimization for private ASTWriterDecl.cpp'
        count=text.count(marker)
        report['actual_exact_unit_workaround_invocations']=count
        contexts=[line for line in text.splitlines() if marker in line or
            'overriding' in line and any(flag in line for flag in ['/Od','/Ob0','/Oi-']) or
            'Builtin C++ frontend:' in line or 'Built target neverc' in line]
        assert len(contexts)<=100
        context=out/'build-context.log'
        context.write_text('\n'.join(contexts)+'\n')
        report['build_context_sha256']=sha(context)
        assert 1<=count<=4,'The native build must actually exercise the exact MSVC19.52 workaround'
        compiler_files=list((build/'_deps/neverc-cpp-build/CMakeFiles').glob('*/CMakeCXXCompiler.cmake'))
        compiler_file,=compiler_files
        metadata=compiler_file.read_text()
        assert 'set(CMAKE_CXX_COMPILER_ID "MSVC")' in metadata
        version,=re.findall(r'set\(CMAKE_CXX_COMPILER_VERSION "([^"]+)"\)',metadata)
        assert version.startswith('19.52.')
        report.update(private_cmake_compiler_sha256=sha(compiler_file),actual_host_cxx_version=version)
        compiler=build/'bin/neverc.exe'
        tests=build/'bin/neverc-translate-tests.exe'
        report['compiler_pe']=pe(compiler)
        report['gtest_pe']=pe(tests)
        expected=plan['expected_gtests']
        assert len(expected)==len(set(expected))==38
        xml=out/'gtest.xml'
        result=subprocess.run([str(tests),'--gtest_filter='+':'.join(expected),
            '--gtest_output=xml:'+str(xml)],capture_output=True,timeout=7200)
        output=out/'gtest.log'
        output.write_text((result.stdout+result.stderr).decode('utf-8',errors='backslashreplace'))
        report.update(actual_gtest_returncode=result.returncode,gtest_log_sha256=sha(output))
        assert xml.is_file(),'Real native GoogleTest XML is required'
        cases=ET.parse(xml).getroot().findall('.//testcase')
        names=[case.attrib['classname']+'.'+case.attrib['name'] for case in cases]
        failed=[name for name,case in zip(names,cases) if case.find('failure') is not None or case.find('error') is not None]
        skipped=[name for name,case in zip(names,cases) if case.find('skipped') is not None]
        report.update(xml_sha256=sha(xml),actual_gtests=len(names),actual_gtest_names=names,
            failed=failed,skipped=skipped)
        assert len(names)==len(set(names))==38 and set(names)==set(expected)
        assert result.returncode==0 and not failed and not skipped
        report.update(completed=True,passed=True)
    except BaseException:
        report['failure']=traceback.format_exc()
    (out/'results.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({key:report.get(key) for key in ['revision','completed','passed',
        'actual_host_cxx_version','actual_exact_unit_workaround_invocations','actual_gtests','failed','skipped']}),flush=True)
    return 0 if report['passed'] else 1

if __name__=='__main__':
    raise SystemExit(main())
