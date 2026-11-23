"""Native reference semantics only; no private frontend or generated program."""
from pathlib import Path
import argparse, hashlib, json, os, platform, subprocess, traceback, re
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
    files=['utils/ci/check-cpp-bind-call-arguments-native40.py','utils/ci/cpp-bind-call-arguments-native40-plan.json','.github/workflows/diagnose-cpp-template-source.yml']
    report['source_sha256']={name:sha(root/name) for name in files}
    sdk=root/'neverc/lib/Translate/Cpp/SDK';catalog=json.loads((sdk/'catalog.json').read_text())
    report['sdk_catalog_sha256']=sha(sdk/'catalog.json')
    assert report['sdk_catalog_sha256']=='b4a7b597150882cd93285481cc060d2eab6949b5c8d50b7ce234579cc814bd77'
    headers=catalog['headers']+catalog['metadata'];assert len(headers)==687
    for row in headers:assert sha(sdk/row['root']/row['path'])==row['sha256']
    report['sdk_files_verified']=687
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
    plan=json.loads((root/'utils/ci/cpp-bind-call-arguments-native40-plan.json').read_text());report['plan']=plan
    assert plan['sdk_catalog_sha256']==report['sdk_catalog_sha256']
    assert len(plan['sdk_input_sha256'])==695
    for name,identity in plan['sdk_input_sha256'].items():assert sha(sdk/name)==identity
    report['exact_sdk_input_sha256']=plan['sdk_input_sha256']
    report['sdk_inputs_verified']=695
    assert len(plan['cases'])==22 and plan['required_native_programs']==44
    assert plan['fixture_group']=='bind-call-arguments-native40' and plan['preparation_only'] is False
    assert plan['execution_binding_required'] is True
    assert re.fullmatch('[0-9a-f]{40}', plan['source_repair_commit'])
    assert plan['clean_source_basis']==plan['source_repair_commit']
    if plan.get('source_syntax_validation_pending', False):
        assert plan['post_merge_validation'] is True
        assert plan['actual_current_sdk_baseline750_report_sha256'] is None
        assert plan['actual_current_sdk_baseline750_proof_sha256'] is None
    else:
        for field in ['actual_current_sdk_baseline750_report_sha256', 'actual_current_sdk_baseline750_proof_sha256']:
            assert re.fullmatch('[0-9a-f]{64}', plan[field])
    nonzero_exits = {case['name']: (case['source_sha256'], case['expected_exit_code'])
                     for case in plan['cases'] if 'expected_exit_code' in case}
    assert nonzero_exits == {'sdk-r19-native-unordered-set-values-arrow-count': ('7118feb71b629659076ae77ee3142eba8feb879b969d1f770f5eeb057a368ef6', 0), 'sdk-r19-native-unordered-set-modifiers-pointer-receiver': ('01085bcc977bf72e8dc2eb29370e187b6ce9b8253ee9c751b4d12026e67adedc', 1), 'sdk-r19-native-unordered-multiset-values-arrow-count': ('39551d658ab9fa413e5b004fb1be2ab945477b93450c09d83cd1e60b99f8e367', 0), 'sdk-r19-native-unordered-multiset-modifiers-pointer-receiver': ('77d203192676e332f9e6ea65660bf269ec9cfeb2993a377bbb477c0cbfcfed42', 0), 'sdk-r19-native-unordered-set-global-iterators-find-pointer-receiver': ('2b830a69437e8855b6ee1d04637e2d8572eda29915c93e46c498baa12a82004c', 0), 'sdk-r19-native-unordered-set-scalar-insertion-pointer-receiver': ('f21d07cecbfe974a0ef19816e73e5bfea9dc503e0398baf1bf4d95512f24ae10', 0), 'sdk-r19-native-unordered-set-iterator-erase-pointer-receiver': ('c7d5814fc7926bf4d2a33bce5dceffc2e2440b8f71157ae4432fe7c620c04f51', 0), 'sdk-r19-native-unordered-set-equal-range-pointer-receiver': ('803b38bd3009ec61d5a5d749a7d5b8533280137f58f81801fa57a3bec812a3b9', 0), 'sdk-r19-native-unordered-set-local-iterators-pointer-receiver': ('d414f18aa9bfe4a79b96786c57df5116491efbe0d242afdaca7fc7a9e22fbada', 0), 'sdk-r19-native-unordered-set-emplacement-pointer-receiver': ('316e07406a67b6772bc5ba9b6712020c9883a8b3284579f02271b1aa78ec1c3f', 0), 'sdk-r19-native-unordered-set-equality-swap-pointer-member-swap': ('c64f66c538c0af7a2f9b6297d3563707bfdc3a8b616c75375e88dd57576c994f', 0), 'sdk-r19-native-unordered-set-pointer-range-pointer-receiver': ('649214658a809dfb8f15db45a26b43e7d9a61f6d967958ae148a8900f4d4d103', 0), 'sdk-r19-native-unordered-set-pointer-construction-explicit-bucket-count-and-policies': ('bc70e35beb412efd617a0d2743a28e1a7e756c295eea909ab73d572a24db42f2', 0)}
    for case in plan['cases']:
        expected_exit = case.get('expected_exit_code', 0)
        assert type(expected_exit) is int and 0 <= expected_exit <= 255
        folder=out/case['name'];folder.mkdir();source=folder/'input.cpp'
        source.write_text(case['source']);assert sha(source)==case['source_sha256']
        modes=[]
        for mode in ['-O0','-O2']:
            program=folder/('native'+mode)
            built=run(flags+[mode,source,'-o',program],case['name']+'/'+mode+'-build')
            executed=run([program],case['name']+'/'+mode+'-run') if built==0 else None
            record=dict(mode=mode,build_returncode=built,run_returncode=executed,passed=built==0 and executed==expected_exit)
            if built==0:
                data=program.read_bytes();assert data[:6]==b'\x7fELF\x02\x01' and data[18:20]==b'\x3e\x00'
                record.update(binary_sha256=sha(program),binary_bytes=len(data),elf_header_hex=data[:64].hex())
            modes.append(record)
        report['cases'].append(dict(name=case['name'],source_sha256=sha(source),modes=modes))
        save()
    report['completed']=True
    report['passed']=len(report['cases'])==22 and all(m['passed'] for c in report['cases'] for m in c['modes'])
    save()
except Exception:
    report['error']=traceback.format_exc();save();print(report['error'],flush=True)
raise SystemExit(0 if report['passed'] else 1)
