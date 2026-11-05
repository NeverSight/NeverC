"""Inspect real Clang AST witnesses remotely; execute no translated or native program."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import platform
import resource
import signal
import subprocess
import traceback


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--repo', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    assert os.environ.get('GITHUB_ACTIONS') == 'true'
    assert platform.system() == 'Linux' and platform.machine() == 'x86_64'
    root, out = args.repo.resolve(), args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
    report = dict(passed=False, completed=False, diagnostic_only=True, ast_only=True,
        private_frontend_requests=0, native_programs_executed=0, generated_programs=0,
        translation_support_verified=False, full_stl_complete=False, commands=[], cases=[])

    def save():
        (out / 'results.json').write_text(json.dumps(report, indent=2) + '\n')

    def run(command, label, *, stdout=None):
        log = out / (label + '.log')
        log.parent.mkdir(parents=True, exist_ok=True)

        def cap_ast_file():
            resource.setrlimit(resource.RLIMIT_FSIZE, (128 * 1024**2, 128 * 1024**2))

        with log.open('wb') as stream:
            try:
                process = subprocess.Popen(list(map(str, command)), cwd=root,
                    stdout=stdout if stdout is not None else stream, stderr=stream,
                    start_new_session=True, preexec_fn=cap_ast_file)
                code = process.wait(timeout=60)
            except subprocess.TimeoutExpired:
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                process.wait()
                stream.write(b'AST SYNTAX COMMAND TIMED OUT\n')
                code = -1000
        report['commands'].append(dict(command=list(map(str, command)), returncode=code,
            timeout_seconds=60, log=str(log.relative_to(out)), log_sha256=sha(log)))
        save()
        return code

    try:
        report['revision'] = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip()
        runner = 'utils/ci/inspect-bind-callable-ast8.py'
        plan_path = 'utils/ci/cpp-bind-callable-ast8-plan.json'
        report['source_sha256'] = {path: sha(root / path) for path in
            (runner, plan_path, '.github/workflows/diagnose-cpp-template-source.yml')}
        plan = json.loads((root / plan_path).read_text())
        assert plan['ast_only'] and len(plan['cases']) == 8
        report['plan'] = plan
        sdk = root / 'neverc/lib/Translate/Cpp/SDK'
        catalog = json.loads((sdk / 'catalog.json').read_text())
        assert sha(sdk / 'catalog.json') == '1497f7d37860d32d9e53798fbcdc0fe473a4c4e66ac05fa81a45910adfb1d167'
        records = catalog['headers'] + catalog['metadata']
        assert len(records) == 665
        for row in records:
            assert sha(sdk / row['root'] / row['path']) == row['sha256']
        report['sdk_files_verified'] = 665
        clang = '/usr/bin/clang++-20'
        assert run([clang, '--version'], 'compiler-version') == 0
        assert 'clang version 20.1.8' in (out / 'compiler-version.log').read_text()
        report['compiler_sha256'] = sha(clang)
        flags = [clang, '--no-default-config', '-std=c++17', '-stdlib=libc++', '-nostdinc++',
            '-resource-dir', sdk / 'resource', '-isystem', sdk / 'libcxx',
            '-fno-fast-math', '-ffp-contract=off', '-D_LIBCPP_REMOVE_TRANSITIVE_INCLUDES',
            '-D_NEVERC_CPP_CORE_V2_SDK', '-D_LIBCPP___ITERATOR_ISTREAM_ITERATOR_H',
            '-D_LIBCPP___ITERATOR_ISTREAMBUF_ITERATOR_H', '-D_LIBCPP___ITERATOR_OSTREAM_ITERATOR_H',
            '-D_LIBCPP___ITERATOR_OSTREAMBUF_ITERATOR_H', '-D_LIBCPP_DISABLE_VISIBILITY_ANNOTATIONS',
            '-fsyntax-only', '-Xclang', '-ast-dump=json']
        for case in plan['cases']:
            folder = out / case['name']
            folder.mkdir()
            source = folder / 'input.cpp'
            source.write_text(case['source'])
            assert sha(source) == case['source_sha256']
            raw = folder / 'ast-remote-only.json'
            with raw.open('wb') as stream:
                code = run(flags + [source], case['name'] + '/syntax', stdout=stream)
            expected_invalid = case['expected'] == 'invalid'
            assert code == (1 if expected_invalid else 0), (case['name'], code)
            assert 0 < raw.stat().st_size <= 128 * 1024**2
            tree = json.loads(raw.read_bytes())
            assert tree['kind'] == 'TranslationUnitDecl'
            stack, selected, inspected = [tree], [], 0
            fields = ('id', 'kind', 'name', 'type', 'valueCategory', 'castKind', 'opcode',
                'referencedDecl', 'ctorType', 'constructionKind', 'anyInit', 'isImplicit',
                'isUsed', 'mangledName', 'explicitlyDefaulted', 'definitionData', 'loc', 'range')
            remaining = [100000]

            def witness(node, depth=0):
                remaining[0] -= 1
                assert remaining[0] >= 0
                value = {key: node[key] for key in fields if key in node}
                if node.get('inner'):
                    if depth == 16:
                        value['deeper_children_omitted'] = len(node['inner'])
                    else:
                        value['inner'] = [witness(child, depth + 1) for child in node['inner']]
                return value

            while stack:
                node = stack.pop()
                inspected += 1
                assert inspected <= 200000
                name, kind = node.get('name'), node.get('kind')
                wanted = (kind == 'ClassTemplateSpecializationDecl' and
                    name in ('__bind', '__weak_result_type') and bool(node.get('completeDefinition')))
                wanted |= kind == 'CXXRecordDecl' and name in ('Op', 'Counter') and bool(node.get('completeDefinition'))
                wanted |= kind == 'FunctionDecl' and name == '__invoke' and any(
                    child.get('kind') == 'CompoundStmt' for child in node.get('inner', []))
                if wanted:
                    selected.append(witness(node))
                    assert len(selected) <= 1024
                stack.extend(child for child in node.get('inner', []) if isinstance(child, dict))
            assert any(node.get('kind') == 'ClassTemplateSpecializationDecl' and node.get('name') == '__bind'
                for node in selected)
            document = dict(case=case['name'], source_sha256=case['source_sha256'],
                ast_sha256=sha(raw), ast_bytes=raw.stat().st_size, inspected_nodes=inspected,
                selected_original_ast_nodes=selected, bounded_depth16_omissions_are_explicit=True,
                source_or_runtime_permission_granted=False)
            target = folder / 'callable-witnesses.json'
            target.write_text(json.dumps(document, indent=2) + '\n')
            report['cases'].append(dict(name=case['name'], source_sha256=case['source_sha256'],
                syntax_returncode=code, expected_invalid=expected_invalid, ast_sha256=sha(raw),
                witness=str(target.relative_to(out)), witness_sha256=sha(target),
                selected_original_ast_nodes=len(selected), inspected_nodes=inspected))
            save()
        report.update(passed=True, completed=True, actual_ast_syntax_commands=8,
            actual_cpp_program_executions=0, full_ast_files_retained_remotely_only=True)
        save()
    except Exception:
        report['failure'] = traceback.format_exc()
        save()
        raise
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
