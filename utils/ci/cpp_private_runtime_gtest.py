"""Link real current Translate/private owners with real GTest and an existing backend."""
from pathlib import Path
import hashlib
import shlex


def build_runtime_gtest(root, build, output, backend, archive, system_libraries,
                        generated_test, private_prefix, run):
    root, build, output = map(Path, [root, build, output])
    driver = build / 'private-frontend-runtime-driver'
    tests = build / 'private-frontend-runtime-gtests'
    hook = build / 'runtime-owner.cmake'
    driver_source = root / 'utils/ci/PrivateFrontendRuntimeDriver.cpp'
    options = ['-O0', '-g0', '-fno-fast-math', '-ffp-contract=off']
    includes = [root / 'neverc/include', root / 'llvm/include',
                root / 'neverc/lib/Translate/Cpp/Frontend']
    lines = ['find_package(GTest REQUIRED)',
             'add_executable(cpp-private-runtime-driver "' + driver_source.as_posix() + '")',
             'add_executable(cpp-private-runtime-gtests "' + Path(generated_test).as_posix() + '"',
             '  "' + (root / 'tests/neverc/NeverCTestFixture.cpp').as_posix() + '")']
    for target, name in [('cpp-private-runtime-driver', driver.name), ('cpp-private-runtime-gtests', tests.name)]:
        lines += ['set_target_properties(' + target + ' PROPERTIES CXX_STANDARD 17',
                  '  CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF OUTPUT_NAME "' + name + '"',
                  '  RUNTIME_OUTPUT_DIRECTORY "' + build.as_posix() + '")',
                  'target_include_directories(' + target + ' PRIVATE']
        lines += ['  "' + path.as_posix() + '"' for path in includes]
        lines += ['  "${CMAKE_BINARY_DIR}/include" "${CMAKE_BINARY_DIR}/tools/neverc/include")',
                  'target_compile_options(' + target + ' PRIVATE ' + ' '.join(options) + ')']
    lines += ['target_compile_definitions(cpp-private-runtime-driver PRIVATE',
              '  NEVERC_AUTHENTICATED_C23_BACKEND=\\"' + Path(backend).as_posix() + '\\"',
              '  NEVERC_PRIVATE_RUNTIME_TRACE_ROOT=\\"' + (output / 'invocations').as_posix() + '\\")',
              'target_link_libraries(cpp-private-runtime-driver PRIVATE nevercTranslate',
              '  "-Wl,--start-group" "' + Path(archive).as_posix() + '" "-Wl,--end-group"']
    lines += ['  "' + flag + '"' for flag in shlex.split(system_libraries)]
    lines += [')', 'target_compile_definitions(cpp-private-runtime-gtests PRIVATE',
              '  NEVERC_BINARY=\\"' + driver.as_posix() + '\\"',
              '  TEST_SOURCE_DIR=\\"' + (root / 'tests/neverc').as_posix() + '\\"',
              '  TEST_C_DIR=\\"' + (root / 'tests/c').as_posix() + '\\")',
              'target_link_libraries(cpp-private-runtime-gtests PRIVATE',
              '  GTest::gtest_main GTest::gtest nevercTranslate LLVMSupport)',
              'add_custom_command(TARGET cpp-private-runtime-driver PRE_LINK',
              '  COMMAND "${Python3_EXECUTABLE}" "' + (root / 'neverc/lib/Translate/Cpp/Frontend/AuditArchive.py').as_posix() + '"',
              '    --nm /usr/bin/llvm-nm-22 --archive "' + Path(archive).as_posix() + '"',
              '    --prefix-header "' + Path(private_prefix).as_posix() + '"',
              '    --host-lib-dir "$<TARGET_FILE_DIR:LLVMCore>" --host-nm /usr/bin/llvm-nm-22 VERBATIM)',
              'add_dependencies(cpp-private-runtime-gtests cpp-private-runtime-driver)']
    hook.write_text('\n'.join(lines) + '\n')
    owner = build / 'root-library-owners'
    config = ['cmake', '-S', root / 'llvm', '-B', owner, '-G', 'Ninja',
        '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_C_COMPILER=/usr/bin/clang-22',
        '-DCMAKE_CXX_COMPILER=/usr/bin/clang++-22', '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON',
        '-DCMAKE_PROJECT_LLVM_INCLUDE=' + str(hook),
        '-DLLVM_ENABLE_PROJECTS=neverc', '-DLLVM_TARGETS_TO_BUILD=X86;AArch64',
        '-DLLVM_INCLUDE_TESTS=OFF', '-DNEVERC_INCLUDE_TESTS=OFF',
        '-DNEVERC_ENABLE_PYTHON_PLUGINS=OFF', '-DNEVERC_BUNDLE_PYTHON_RUNTIME=OFF',
        '-DLLVM_INCLUDE_EXAMPLES=OFF', '-DLLVM_INCLUDE_BENCHMARKS=OFF', '-DLLVM_INCLUDE_UTILS=OFF',
        '-DLLVM_ENABLE_ASSERTIONS=OFF', '-DLLVM_ENABLE_ZLIB=OFF', '-DLLVM_ENABLE_ZSTD=OFF',
        '-DLLVM_ENABLE_LIBXML2=OFF', '-DLLVM_ENABLE_CURL=OFF', '-DLLVM_ENABLE_TERMINFO=OFF']
    assert run(config, 'runtime-build/configure-real-owners', 180) == 0
    assert run(['cmake', '--build', owner, '--target', 'cpp-private-runtime-gtests',
                '--verbose', '--parallel', '4'], 'runtime-build/real-owners', 1800) == 0
    assert driver.is_file() and tests.is_file()
    return driver, tests, {'hook': str(hook), 'hook_sha256': hashlib.sha256(hook.read_bytes()).hexdigest(),
        'driver': str(driver), 'gtest': str(tests), 'cmake_owner': str(owner),
        'libraries': ['actual audited private archive', 'nevercTranslate', 'LLVMSupport', 'real GTest'],
        'root_llvm_support_owner': 'llvm/lib/CSupport', 'library_stubs_used': False,
        'host_compiler_artifact_rebuilt': False, 'full_compiler_ci_coverage': False}
