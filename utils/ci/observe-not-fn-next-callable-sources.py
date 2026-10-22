"""Collect upstream AST evidence for the eight unchanged, unsupported not_fn cases.

This is an Actions-only source observer. It grants no private source permission,
does not execute any program, and does not report a feature or runtime pass.
"""
from pathlib import Path
import argparse
import gzip
import hashlib
import json
import os
import platform
import resource
import signal
import subprocess
import time
import traceback


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def selected_ast(document, owned_names):
    """Keep actual source roots and referenced declarations, preserving raw IDs."""
    by_id, parents = {}, {}
    pending = [(document, None)]
    while pending:
        node, parent = pending.pop()
        identity = node.get('id')
        if identity:
            prior = by_id.get(identity)
            if prior is None or len(node.get('inner', [])) > len(prior.get('inner', [])):
                by_id[identity] = node
                parents[identity] = parent
        for child in node.get('inner', []):
            if isinstance(child, dict):
                pending.append((child, identity or parent))
    roots = [node for node in document.get('inner', [])
             if node.get('name') in owned_names and node.get('kind') in
             {'FunctionDecl', 'CXXRecordDecl', 'VarDecl', 'TypeAliasDecl'}]
    assert any(node.get('kind') == 'FunctionDecl' and node.get('name') == 'main'
               and any(c.get('kind') == 'CompoundStmt' for c in node.get('inner', []))
               for node in roots), 'Actual main definition is absent'
    selected = {node['id']: node for node in roots}
    queue = list(roots)
    missing = set()
    while queue:
        node = queue.pop()
        references = []
        todo = [node]
        while todo:
            current = todo.pop()
            reference = current.get('referencedDecl')
            if isinstance(reference, dict) and reference.get('id'):
                references.append(reference['id'])
            for key in ['referencedMemberDecl', 'typeAliasDeclId']:
                if isinstance(current.get(key), str):
                    references.append(current[key])
            typ = current.get('type')
            if isinstance(typ, dict) and typ.get('typeAliasDeclId'):
                references.append(typ['typeAliasDeclId'])
            for child in current.get('inner', []):
                if isinstance(child, dict):
                    todo.append(child)
        for identity in references:
            if identity in selected:
                continue
            target = by_id.get(identity)
            if target is None:
                missing.add(identity)
                continue
            assert len(selected) < 4096, 'AST excerpt limit reached; full AST retained remotely'
            selected[identity] = target
            queue.append(target)
            # A selected method's actual class/template owns its constructors,
            # original declarations and type defaults. Keep this independent
            # AST role; it is not a private retained-source packet.
            parent = by_id.get(parents.get(identity))
            if parent and parent.get('kind') in {'CXXRecordDecl', 'ClassTemplateSpecializationDecl',
                                                'FunctionTemplateDecl'}:
                key = parent['id']
                if key not in selected:
                    assert len(selected) < 4096
                    selected[key] = parent
                    queue.append(parent)
    return dict(owned_roots=roots, referenced_declarations=list(selected.values()),
                unresolved_reference_ids=sorted(missing),
                private_retention_or_default_packet_proven=False)


def main():
    parser = argparse.ArgumentParser()
    for name in ['repo', 'plan', 'output']:
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    root, out = args.repo.resolve(), args.output.resolve()
    assert os.environ.get('GITHUB_ACTIONS') == 'true', 'C++ observation is Actions-only'
    assert (platform.system(), platform.machine(), platform.python_version()) == ('Linux', 'x86_64', '3.12.10')
    out.mkdir(parents=True, exist_ok=True)
    plan = json.loads(args.plan.read_text())
    report = dict(completed=False, collection_passed=False, observation_only=True,
                  private_frontend_requests=0, native_programs=0, generated_programs=0,
                  formal_xml=0, feature_support_verified=False, full_stl_complete=False,
                  all_cpp_execution_on_actions=True, commands=[], cases=[])

    def save():
        temp = out / 'results.new'
        temp.write_text(json.dumps(report, indent=2) + '\n')
        temp.replace(out / 'results.json')

    def run(argv, directory, label):
        stdout, stderr = directory / (label + '.stdout'), directory / (label + '.stderr')
        env = dict(os.environ)
        for key in ['GITHUB_TOKEN', 'GH_TOKEN', 'ACTIONS_RUNTIME_TOKEN']:
            env.pop(key, None)
        start = time.monotonic()

        def limit_output():
            resource.setrlimit(resource.RLIMIT_FSIZE, (512 * 1024 ** 2, 512 * 1024 ** 2))

        with stdout.open('wb') as output, stderr.open('wb') as errors:
            process = subprocess.Popen(list(map(str, argv)), cwd=root, env=env,
                                       stdout=output, stderr=errors, start_new_session=True,
                                       preexec_fn=limit_output)
            try:
                code = process.wait(timeout=60)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait(timeout=10)
                code = -1000
        row = dict(command=list(map(str, argv)), pid=process.pid, returncode=code,
                   timeout_seconds=60, elapsed_seconds=time.monotonic() - start,
                   stdout=stdout.relative_to(out).as_posix(), stdout_sha256=sha(stdout),
                   stderr=stderr.relative_to(out).as_posix(), stderr_sha256=sha(stderr))
        report['commands'].append(row)
        save()
        return code, stdout

    save()
    try:
        report['revision'] = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip()
        report['plan'] = plan
        report['source_sha256'] = {name: sha(root / name) for name in plan['source_paths']}
        prefix = root / 'neverc/lib/Translate/Cpp/Frontend'
        assert len(plan['exact_private_input_sha256']) == 19
        for name, identity in plan['exact_private_input_sha256'].items():
            assert sha(prefix / name) == identity
        original = json.loads((root / plan['original_clean216_plan']).read_text())
        original_cases = {case['name']: case for case in original['cases']}
        for case in plan['cases']:
            prior = original_cases[case['name']]
            assert prior['source'] == case['source']
            assert prior['source_sha256'] == case['source_sha256']
            assert prior['expected'] == 'reject'
        sdk = root / 'neverc/lib/Translate/Cpp/SDK'
        assert sha(sdk / 'catalog.json') == plan['sdk_catalog_sha256']
        catalog = json.loads((sdk / 'catalog.json').read_text())
        entries = catalog['headers'] + catalog['metadata']
        assert len(entries) == 665
        for entry in entries:
            assert sha(sdk / entry['root'] / entry['path']) == entry['sha256']
        report['sdk_files_verified'] = 665
        compiler = Path('/usr/bin/clang++-20')
        code, version = run([compiler, '--version'], out, 'clang-version')
        assert code == 0 and 'clang version 20.1.8' in version.read_text()
        report['compiler_sha256'] = sha(compiler)
        flags = [compiler, '--no-default-config', '-std=c++17', '-stdlib=libc++',
                 '-nostdinc++', '-resource-dir', sdk / 'resource',
                 '-isystem', sdk / 'libcxx', '-fno-fast-math', '-ffp-contract=off',
                 '-D_LIBCPP_REMOVE_TRANSITIVE_INCLUDES', '-D_NEVERC_CPP_CORE_V2_SDK',
                 '-D_LIBCPP___ITERATOR_ISTREAM_ITERATOR_H', '-D_LIBCPP___ITERATOR_ISTREAMBUF_ITERATOR_H',
                 '-D_LIBCPP___ITERATOR_OSTREAM_ITERATOR_H', '-D_LIBCPP___ITERATOR_OSTREAMBUF_ITERATOR_H',
                 '-D_LIBCPP_DISABLE_VISIBILITY_ANNOTATIONS']
        report['flags'] = list(map(str, flags))
        assert len(plan['cases']) == 8 and len({case['name'] for case in plan['cases']}) == 8
        for case in plan['cases']:
            folder = out / case['name']
            folder.mkdir()
            source = folder / 'input.cpp'
            source.write_text(case['source'])
            assert sha(source) == case['source_sha256']
            code, raw = run(flags + ['-fsyntax-only', '-Xclang', '-ast-dump=json', source], folder, 'ast')
            row = dict(name=case['name'], source_sha256=sha(source), syntax_returncode=code,
                       upstream_ast_only=True, private_source_packets_verified=False)
            report['cases'].append(row)
            save()
            if code == 0:
                with raw.open() as stream:
                    document = json.load(stream)
                selection = selected_ast(document, set(case['owned_names']))
                excerpt = folder / 'selected-ast.json'
                excerpt.write_text(json.dumps(selection, indent=2) + '\n')
                assert excerpt.stat().st_size <= 32 * 1024 ** 2
                row.update(selected_ast=excerpt.relative_to(out).as_posix(),
                           selected_ast_sha256=sha(excerpt),
                           selected_declarations=len(selection['referenced_declarations']),
                           unresolved_reference_ids=selection['unresolved_reference_ids'])
                del document, selection
            # Preserve the full, exact stdout byte stream remotely; summaries
            # and bounded excerpts are the only artifacts fetched by the agent.
            raw_sha = sha(raw)
            compressed = folder / 'full-ast.json.gz'
            with raw.open('rb') as original, gzip.open(compressed, 'wb') as zipped:
                for chunk in iter(lambda: original.read(1048576), b''):
                    zipped.write(chunk)
            digest = hashlib.sha256()
            with gzip.open(compressed, 'rb') as zipped:
                for chunk in iter(lambda: zipped.read(1048576), b''):
                    digest.update(chunk)
            assert digest.hexdigest() == raw_sha
            row.update(full_ast_sha256=raw_sha, full_ast_gzip_sha256=sha(compressed),
                       full_ast_bytes=raw.stat().st_size, compressed_ast_bytes=compressed.stat().st_size)
            raw.unlink()
            save()
        report['completed'] = True
        report['collection_passed'] = len(report['cases']) == 8 and all(
            case['syntax_returncode'] == 0 and case.get('selected_ast_sha256')
            for case in report['cases'])
        save()
    except Exception:
        report['error'] = traceback.format_exc()
        save()
        print(report['error'], flush=True)
    return 0 if report['collection_passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
