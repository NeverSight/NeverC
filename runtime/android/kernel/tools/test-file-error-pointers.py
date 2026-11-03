#!/usr/bin/env python3
"""Exercise the production file-open error path with real kernel pointer values."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "src/nvk_file.c").read_text()
start = source.index("int neverc_krt_file_open(")
end = source.index("void neverc_krt_file_close(", start)
function = source[start:end]
program = r'''
#include <assert.h>
#undef __used
#undef __unused
#include <linux/err.h>
struct neverc_krt_file { void *filp; };
static void *next;
static void *open_stub(const char *path, int flags, unsigned short mode) {
    (void)path; (void)flags; (void)mode; return next;
}
static void *(*_neverc_krt_filp_open)(const char *, int, unsigned short) = open_stub;
'''+function+r'''
int main(void) {
    struct neverc_krt_file file = {0};
    next = (void *)0xffffffc012345000UL;
    assert(neverc_krt_file_open(&file, "/proc/self/maps", 0) == 0);
    assert(file.filp == next);
    next = ERR_PTR(-2);
    assert(neverc_krt_file_open(&file, "/absent", 0) == -2);
    assert(file.filp == 0);
    next = 0;
    assert(neverc_krt_file_open(&file, "/null", 0) == -1);
    assert(file.filp == 0);
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="neverc-file-error-pointers-") as temporary:
    directory = Path(temporary)
    cfile, binary = directory / "test.c", directory / "test"
    cfile.write_text(program)
    subprocess.run(shlex.split(os.environ.get("CC", "cc"))+[
        "-std=c11", "-Wall", "-Wextra", "-Werror", "-U__weak",
        "-I"+str(root / "arm64/include"), str(cfile), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("test-file-error-pointers: OK")
