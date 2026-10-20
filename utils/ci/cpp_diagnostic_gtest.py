"""Build a real GTest executable through this repository's owning CMake targets."""
from pathlib import Path
import hashlib


def build_gtest(root, output, clangxx, compiler, sources, libraries, binary_name,
                run, configure_label, build_timeout):
    """Reuse the compiler artifact; build only the requested harness and owners."""
    root, output = Path(root), Path(output)
    support = output / 'llvm-support'
    target = 'cpp-diagnostic-gtest'
    hook = output / 'build/real-gtest-owner.cmake'
    hook.parent.mkdir(parents=True, exist_ok=True)
    lines = ['find_package(GTest REQUIRED)', 'add_executable(' + target]
    lines += ['  "' + Path(source).as_posix() + '"' for source in sources]
    lines += [')', 'set_target_properties(' + target + ' PROPERTIES',
              '  CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF',
              '  OUTPUT_NAME "' + binary_name + '"',
              '  RUNTIME_OUTPUT_DIRECTORY "' + output.as_posix() + '")',
              'target_include_directories(' + target + ' PRIVATE',
              '  "' + (root / 'neverc/include').as_posix() + '"',
              '  "' + (root / 'llvm/include').as_posix() + '"',
              '  "${CMAKE_BINARY_DIR}/include"',
              '  "${CMAKE_BINARY_DIR}/tools/neverc/include")',
              'target_compile_definitions(' + target + ' PRIVATE',
              '  NEVERC_BINARY=\\"' + Path(compiler).as_posix() + '\\"',
              '  TEST_SOURCE_DIR=\\"' + (root / 'tests/neverc').as_posix() + '\\"',
              '  TEST_C_DIR=\\"' + (root / 'tests/c').as_posix() + '\\")',
              'target_compile_options(' + target + ' PRIVATE',
              '  -O0 -g0 -fno-fast-math -ffp-contract=off)',
              'target_link_libraries(' + target + ' PRIVATE',
              '  GTest::gtest_main GTest::gtest ' + ' '.join(libraries) + ')']
    hook.write_text('\n'.join(lines) + '\n')
    # This tree has no llvm-config tool and relocates Support to CSupport.
    # Let its real library targets provide all transitive archives and public
    # include paths, instead of guessing a list or mixing apt LLVM's ABI.
    config = ['cmake', '-S', root / 'llvm', '-B', support, '-G', 'Ninja',
              '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_C_COMPILER=/usr/bin/clang-22',
              '-DCMAKE_CXX_COMPILER=' + str(clangxx),
              '-DCMAKE_PROJECT_LLVM_INCLUDE=' + str(hook),
              '-DLLVM_ENABLE_PROJECTS=neverc', '-DLLVM_TARGETS_TO_BUILD=X86;AArch64',
              '-DLLVM_INCLUDE_TESTS=OFF', '-DNEVERC_INCLUDE_TESTS=OFF',
              '-DNEVERC_ENABLE_PYTHON_PLUGINS=OFF', '-DNEVERC_BUNDLE_PYTHON_RUNTIME=OFF',
              '-DLLVM_INCLUDE_EXAMPLES=OFF', '-DLLVM_INCLUDE_BENCHMARKS=OFF',
              '-DLLVM_INCLUDE_UTILS=OFF', '-DLLVM_ENABLE_ASSERTIONS=OFF',
              '-DLLVM_ENABLE_ZLIB=OFF', '-DLLVM_ENABLE_ZSTD=OFF',
              '-DLLVM_ENABLE_LIBXML2=OFF', '-DLLVM_ENABLE_CURL=OFF',
              '-DLLVM_ENABLE_TERMINFO=OFF']
    assert run(config, configure_label, 180) == 0
    assert run(['cmake', '--build', support, '--target', target, '--verbose',
                '--parallel', '4'], 'build/real-gtest', build_timeout) == 0
    binary = output / binary_name
    assert binary.is_file()
    return binary, {'path': str(hook.relative_to(output)),
                    'sha256': hashlib.sha256(hook.read_bytes()).hexdigest(),
                    'target': target, 'libraries': libraries,
                    'header_owner': 'actual repository LLVMSupport PUBLIC CSupport include paths',
                    'compiler_artifact_rebuilt': False,
                    'cmake_hook': 'CMAKE_PROJECT_LLVM_INCLUDE'}
