"""Link retained real objects with the actual newly built Mach-O backend."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import signal
import stat
import struct
import subprocess
import time
import traceback
import urllib.error
import urllib.request
import zipfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--repo', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--build', type=Path, required=True)
    args = parser.parse_args()
    root, out, build = args.repo.resolve(), args.output.resolve(), args.build.resolve()
    out.mkdir(parents=True, exist_ok=True)
    r = {'passed': False, 'completed': False, 'commands': [], 'images': [],
         'neverc_compiler_rebuilt': False, 'actual_backend_rebuilt': True,
         'library_stubs_used': False, 'apt_llvm_library_abi_mixed': False,
         'mach_o_images_executed': False, 'original_cli_crashes_fixed': False,
         'full_ci_passed': False, 'published_to_dev': False, 'goal_complete': False}

    def sha(path):
        return hashlib.sha256(Path(path).read_bytes()).hexdigest()

    def save():
        (out / 'results.json').write_text(json.dumps(r, indent=2) + '\n')

    def run(argv, label, timeout=60):
        argv = list(map(str, argv))
        log = out / (label + '.log')
        log.parent.mkdir(parents=True, exist_ok=True)
        start = time.monotonic()
        with log.open('w') as stream:
            try:
                process = subprocess.Popen(argv, cwd=root, stdout=stream,
                    stderr=subprocess.STDOUT, start_new_session=True)
                code = process.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                # Kill this diagnostic's owned compiler workers before closing
                # or hashing their log. Never leave writers behind the timeout.
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                process.wait()
                stream.write('\nDIAGNOSTIC COMMAND TIMED OUT; OWNED PROCESS GROUP STOPPED\n')
                code = -1000
        r['commands'].append({'command': argv, 'returncode': code, 'timeout_seconds': timeout,
                             'log': str(log.relative_to(out)), 'log_sha256': sha(log),
                             'elapsed_seconds': time.monotonic() - start})
        save()
        print(label, code, flush=True)
        assert code == 0, (label, code)

    save()
    try:
        r['revision'] = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip()
        names = ['neverc/include/neverc/Linker/MachO/DataConst.h',
                 'neverc/lib/Linker/Backends/MachO/Driver/MachODriver.cpp',
                 'neverc/lib/Linker/Backends/MachO/State/MachOLinkerContext.cpp',
                 'neverc/lib/Linker/Core/Runtime/Session.cpp',
                 'neverc/include/neverc/Linker/Core/Runtime/Session.h',
                 'utils/ci/MachOImageBackend.cpp', 'utils/ci/check-macho-image-backend.py',
                 '.github/workflows/diagnose-cpp-template-source.yml']
        r['source_sha256'] = {name: sha(root / name) for name in names}
        run(['/usr/bin/clang++-22', '--version'], 'environment/clang')
        token = os.environ['GH_TOKEN']
        base_url = 'https://api.github.com/repos/NeverSight/NeverC/'
        request = urllib.request.Request(base_url + 'actions/artifacts/12233992237',
                  headers={'Authorization': 'Bearer ' + token, 'Accept': 'application/vnd.github+json'})
        with urllib.request.urlopen(request, timeout=45) as stream:
            artifact = json.load(stream)
        assert artifact['name'] == 'cpp-linker-residual-failures-af815072de0e1abcd32f55a9d93b21efbf7cd090'
        assert artifact['digest'] == 'sha256:24917ad1ffe3c818f23771f0d8d48bc2ae357d51db70e42e97001f8adac528e1'
        assert artifact['workflow_run']['head_sha'] == 'af815072de0e1abcd32f55a9d93b21efbf7cd090'
        class NoRedirect(urllib.request.HTTPRedirectHandler):
            def redirect_request(self, req, fp, code, msg, headers, newurl):
                return None
        request = urllib.request.Request(base_url + 'actions/artifacts/12233992237/zip',
                  headers={'Authorization': 'Bearer ' + token})
        try:
            stream = urllib.request.build_opener(NoRedirect).open(request, timeout=45)
        except urllib.error.HTTPError as error:
            assert error.code == 302 and error.headers['Location'].startswith('https://')
            stream = urllib.request.urlopen(error.headers['Location'], timeout=120)
        archive = out / 'retained-original-inputs.zip'
        with stream, archive.open('wb') as dest:
            while chunk := stream.read(1024 * 1024):
                dest.write(chunk)
        assert sha(archive) == '24917ad1ffe3c818f23771f0d8d48bc2ae357d51db70e42e97001f8adac528e1'
        r['input_artifact'] = artifact
        r['input_archive_sha256'] = sha(archive)
        fixture = out / 'inputs/image.o'
        fixture.parent.mkdir()
        suffix = '/captured-fixtures/NativeMachOImageKinds/macho_kinds_dir/image.o'
        with zipfile.ZipFile(archive) as z:
            found = [entry for entry in z.infolist() if entry.filename.endswith(suffix)]
            assert len(found) == 1 and not stat.S_ISLNK(found[0].external_attr >> 16)
            fixture.write_bytes(z.read(found[0]))
            r['input_object_archive_path'] = found[0].filename
        raw = fixture.read_bytes()
        assert raw[:4] == b'\xcf\xfa\xed\xfe' and struct.unpack_from('<I', raw, 4)[0] == 0x100000C
        assert struct.unpack_from('<I', raw, 12)[0] == 1
        r['input_object_sha256'] = sha(fixture)
        hook = out / 'build/macho-backend-owner.cmake'
        hook.parent.mkdir()
        hook.write_text('''add_executable(macho-image-backend "''' + (root / names[5]).as_posix() + '''")
set_target_properties(macho-image-backend PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_include_directories(macho-image-backend BEFORE PRIVATE "''' + (root / 'neverc/include').as_posix() + '''" "''' + (root / 'neverc/include/neverc').as_posix() + '''" "''' + (root / 'llvm/include').as_posix() + '''" "${CMAKE_BINARY_DIR}/include")
target_compile_options(macho-image-backend PRIVATE -fno-fast-math -ffp-contract=off)
target_link_libraries(macho-image-backend PRIVATE linkerMachO)
''')
        r['cmake_owner'] = {'path': str(hook.relative_to(out)), 'sha256': sha(hook), 'actual_target': 'linkerMachO'}
        run(['cmake', '-S', root / 'llvm', '-B', build, '-G', 'Ninja',
             '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_C_COMPILER=/usr/bin/clang-22',
             '-DCMAKE_CXX_COMPILER=/usr/bin/clang++-22', '-DCMAKE_PROJECT_LLVM_INCLUDE=' + str(hook),
             '-DLLVM_ENABLE_PROJECTS=neverc', '-DLLVM_TARGETS_TO_BUILD=X86;AArch64',
             '-DLLVM_INCLUDE_TESTS=OFF', '-DNEVERC_INCLUDE_TESTS=OFF',
             '-DNEVERC_ENABLE_PYTHON_PLUGINS=OFF', '-DNEVERC_BUNDLE_PYTHON_RUNTIME=OFF',
             '-DLLVM_INCLUDE_EXAMPLES=OFF', '-DLLVM_INCLUDE_BENCHMARKS=OFF',
             '-DLLVM_ENABLE_ASSERTIONS=ON', '-DLLVM_ENABLE_ZLIB=OFF', '-DLLVM_ENABLE_ZSTD=OFF',
             '-DLLVM_ENABLE_LIBXML2=OFF', '-DLLVM_ENABLE_CURL=OFF', '-DLLVM_ENABLE_TERMINFO=OFF'],
            'build/configure', 180)
        run(['cmake', '--build', build, '--target', 'macho-image-backend', '--verbose', '--parallel', '4'],
            'build/actual-macho-backend', 7200)
        matches = [f for f in build.rglob('macho-image-backend') if f.is_file()]
        assert len(matches) == 1
        binary = matches[0]
        raw = binary.read_bytes()
        assert raw[:6] == b'\x7fELF\x02\x01' and raw[18:20] == b'\x3e\x00'
        header = out / 'actual-backend-elf-header.bin'
        header.write_bytes(raw[:64])
        r['actual_backend_executable'] = {'sha256': sha(binary), 'bytes': len(raw), 'remote_path': str(binary),
                                        'header_sha256': sha(header), 'download_to_user_host': False}
        for mode, filetype in [('preload', 5), ('dylinker', 7), ('kext', 11)]:
            for policy in ['default', 'data_const', 'no_data_const']:
                directory = out / 'images' / mode / policy
                directory.mkdir(parents=True)
                image = directory / 'image'
                merged = directory / 'kext-objects'
                run([binary, mode, policy, fixture, image, merged], 'link/' + mode + '-' + policy)
                raw = image.read_bytes()
                magic, cpu, subtype, kind, count, total, flags, reserved = struct.unpack_from('<8I', raw)
                assert magic == 0xFEEDFACF and cpu == 0x100000C and kind == filetype
                cursor = 32
                commands, segments = [], []
                for i in range(count):
                    command, size = struct.unpack_from('<II', raw, cursor)
                    assert size >= 8 and size % 8 == 0 and cursor + size <= 32 + total
                    commands.append(command)
                    if command == 0x19:
                        segments.append(raw[cursor + 8:cursor + 24].split(b'\0')[0].decode())
                    cursor += size
                assert cursor == 32 + total and '__PAGEZERO' not in segments
                if mode == 'preload':
                    assert 5 in commands and 14 not in commands and not flags & 4
                if mode == 'dylinker':
                    assert 5 in commands and 15 in commands
                if mode == 'kext':
                    assert not flags & 0x80 and merged.is_dir()
                    merged_objects = list(merged.rglob('*.o'))
                    assert merged_objects
                    for path in merged_objects:
                        data = path.read_bytes()
                        assert data[:4] == b'\xcf\xfa\xed\xfe' and struct.unpack_from('<I', data, 12)[0] == 1
                r['images'].append({'mode': mode, 'policy': policy, 'path': str(image.relative_to(out)),
                    'sha256': sha(image), 'bytes': len(raw), 'filetype': kind,
                    'load_commands': commands, 'segments': segments, 'flags': flags})
                save()
        r.update(completed=True, passed=True, actual_backend_link_invocations=9, actual_macho_images=9)
        save()
        return 0
    except Exception:
        r['error'] = traceback.format_exc()
        save()
        print(r['error'], flush=True)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
