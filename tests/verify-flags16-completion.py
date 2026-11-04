#!/usr/bin/env python3
"""Compare compiled word POPF with original bytes and preserve AC/ID.

Execute each safe low-word image (TF clear) with all four AC/ID inputs. The
assembly witness restores host flags before returning to C. Arithmetic flags
are deliberately not compared across an ordinary compiler ABI. The protected
NeverD KVM oracle separately checks them at the original instruction boundary.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess


REFERENCE = r'''
.text
.p2align 4
.globl original_write
.type original_write,@function
original_write:
  pushw %di
  .byte 0x66,0x9d
  ret
.size original_write,.-original_write
.globl incorrect_wide_write
.type incorrect_wide_write,@function
incorrect_wide_write:
  pushq %rdi
  popfq
  ret
.size incorrect_wide_write,.-incorrect_wide_write
.globl capture_flags
.type capture_flags,@function
capture_flags:
  pushq %r12
  pushq %r13
  subq $8,%rsp
  movq %rdi,%r13
  movq %rsi,%rdi
  pushfq
  popq %r12
  movq %r12,%rax
  andq $-2360577,%rax
  orq %rdx,%rax
  pushq %rax
  popfq
  call *%r13
  pushfq
  popq %rax
  pushq %r12
  popfq
  leaq 8(%rsp),%rsp
  popq %r13
  popq %r12
  ret
.size capture_flags,.-capture_flags
.section .note.GNU-stack,"",@progbits
'''

SOURCE = r'''
typedef unsigned long long u64;
__attribute__((noinline))
void flags_write_test(unsigned short flags) {
  volatile u64 guard[4] = {0x0123456789abcdefULL, 0xfedcba9876543210ULL,
                            0x13579bdf2468ace0ULL, 0x0f1e2d3c4b5a6978ULL};
  __builtin_ia32_writeeflags_u16(flags);
  if (guard[0] != 0x0123456789abcdefULL || guard[1] != 0xfedcba9876543210ULL ||
      guard[2] != 0x13579bdf2468ace0ULL || guard[3] != 0x0f1e2d3c4b5a6978ULL)
    __builtin_trap();
}
'''

IR = r'''
declare void @llvm.x86.flags.write.u16(i16)
declare void @llvm.trap()
define void @flags_write_test(i16 %flags) {
  %p = alloca [4 x i64], align 16
  %q = getelementptr [4 x i64], ptr %p, i64 0, i64 3
  store volatile i64 81985529216486895, ptr %p, align 16
  store volatile i64 1089357896855742840, ptr %q, align 8
  call void @llvm.x86.flags.write.u16(i16 %flags)
  %a = load volatile i64, ptr %p, align 16
  %b = load volatile i64, ptr %q, align 8
  %x = icmp eq i64 %a, 81985529216486895
  %y = icmp eq i64 %b, 1089357896855742840
  %ok = and i1 %x, %y
  br i1 %ok, label %done, label %bad
bad:
  call void @llvm.trap()
  unreachable
done:
  ret void
}
'''

DRIVER = r'''
#include <stdint.h>
#include <stdio.h>
extern void original_write(unsigned short);
extern void incorrect_wide_write(unsigned short);
extern void tested_write(unsigned short);
extern uint64_t capture_flags(void (*)(unsigned short), unsigned, uint64_t);
int main(void) {
  unsigned comparisons = 0;
  for (unsigned upper = 0; upper != 4; ++upper)
    for (unsigned word = 0; word != 65536; ++word) {
      if (word & 0x100) continue;
      const uint64_t high = (upper & 1 ? 0x40000 : 0) | (upper & 2 ? 0x200000 : 0);
      const uint64_t expected = capture_flags(original_write, word, high);
      const uint64_t actual = capture_flags(tested_write, word, high);
      if (((expected ^ actual) & 0x247600) != 0) {
        printf("word=%04x upper=%x expected=%llx actual=%llx\n", word, upper,
               (unsigned long long)expected, (unsigned long long)actual);
        return 1;
      }
      ++comparisons;
    }
  printf("%u original word-POPF comparisons\n", comparisons);
  return 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--compiler", type=Path)
    group.add_argument("--llc", type=Path)
    parser.add_argument("--clang", default="clang")
    parser.add_argument("--source", type=Path,
                        help="An unchanged emitted C translation unit, compiled with --compiler")
    parser.add_argument("--symbol", default="flags_write_test")
    parser.add_argument("--keep", type=Path, required=True)
    args = parser.parse_args()
    if platform.system() != "Linux" or platform.machine() != "x86_64":
        parser.error("the original CPU witness requires Linux x86_64")
    if args.source and not args.compiler:
        parser.error("--source requires --compiler")
    root = args.keep.resolve()
    root.mkdir(parents=True, exist_ok=True)
    commands, results = [], []

    def run(command, stem, expected=0):
        completed = subprocess.run([str(a) for a in command], capture_output=True,
                                   text=True, timeout=120)
        (root / (stem + ".log")).write_text(completed.stdout + completed.stderr)
        commands.append({"command": [str(a) for a in command], "exit_code": completed.returncode})
        (root / "commands.json").write_text(json.dumps(commands, indent=2) + "\n")
        if completed.returncode != expected:
            raise RuntimeError(f"{stem}: exit {completed.returncode}: {completed.stdout}{completed.stderr}")
        return completed.stdout

    (root / "reference.s").write_text(REFERENCE)
    (root / "driver.c").write_text(DRIVER)
    # The emitted source owns its C signature. A separate assembly tail jump
    # adapts only the machine call; do not invent a conflicting C declaration.
    (root / "adapter.s").write_text(
        ".text\n.globl tested_write\n.type tested_write,@function\n"
        "tested_write: jmp " + args.symbol + "\n"
        '.section .note.GNU-stack,"",@progbits\n')
    run([args.clang, "-c", root / "adapter.s", "-o", root / "adapter.o"], "adapter")
    run([args.clang, "-c", root / "reference.s", "-o", root / "reference.o"], "reference")
    run([args.clang, "-O2", "-Dtested_write=incorrect_wide_write", root / "driver.c",
         root / "reference.o", "-o", root / "negative"], "negative-build")
    run([root / "negative"], "negative", expected=1)
    if args.llc:
        source = root / "input.ll"
        source.write_text(IR)
    else:
        source = root / "input.c"
        source.write_text(args.source.read_text() if args.source else SOURCE)
    for opt in ("O0", "O2"):
        obj, exe = root / (opt + ".o"), root / opt
        if args.llc:
            command = [args.llc, "-" + opt, "-mtriple=x86_64-unknown-linux-gnu",
                       "-verify-machineinstrs", "-filetype=obj", source, "-o", obj]
        else:
            command = [args.compiler, "-" + opt, "-std=gnu23", "-ffreestanding", "-fno-lto",
                       "-fno-pie", "-c", source, "-o", obj]
            if "neverc" in args.compiler.name:
                command += ["-fno-builtin-mimalloc", "-mllvm", "-verify-machineinstrs"]
        run(command, opt + "-compile")
        run([args.clang, "-O2", "-no-pie", root / "driver.c",
             root / "reference.o", root / "adapter.o", obj, "-o", exe], opt + "-link")
        output = run([exe], opt + "-run")
        assert output.strip() == "131072 original word-POPF comparisons", output
        results.append({"optimization": opt, "comparisons": 131072,
                        "object_sha256": hashlib.sha256(obj.read_bytes()).hexdigest(),
                        "executable_sha256": hashlib.sha256(exe.read_bytes()).hexdigest()})
    (root / "results.json").write_text(json.dumps({"results": results,
        "negative_control_rejected": True, "source_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
        "affinity": sorted(os.sched_getaffinity(0))}, indent=2) + "\n")
    print(json.dumps(results))


if __name__ == "__main__":
    main()
