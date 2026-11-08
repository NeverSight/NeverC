#!/usr/bin/env python3
"""Compare compiled FLAGS accesses with original PUSHF/POPF bytes.

Execute each safe low-word image (TF clear) with all four AC/ID inputs. The
assembly witness restores host flags before returning to C. Arithmetic flags
are deliberately not compared across an ordinary compiler ABI. The protected
NeverD KVM oracle separately checks them at the original instruction boundary.
The default word image checks preserved AC/ID. --image-bits 32/64 checks the
native code-mode width, including all combinations of old/new AC/ID. Builtin
probes also compare returned snapshots. --source retains the existing write
contract for unchanged generated C. NeverC i386 requires --backend-llc; the
32-bit driver requires multilib libc. This is a Linux CPL3 compiler ABI check,
not a privileged, fault or asynchronous-event execution profile.
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

REFERENCE32 = r'''
.text
.p2align 4
.globl original_write
.type original_write,@function
original_write:
  movw 4(%esp),%ax
  pushw %ax
  .byte 0x66,0x9d
  ret
.size original_write,.-original_write
.globl incorrect_wide_write
.type incorrect_wide_write,@function
incorrect_wide_write:
  pushl 4(%esp)
  popfl
  ret
.size incorrect_wide_write,.-incorrect_wide_write
.globl capture_flags
.type capture_flags,@function
capture_flags:
  pushl %ebx
  pushl %esi
  pushl %edi
  pushl %ebp
  subl $12,%esp
  movl 32(%esp),%esi
  movl 36(%esp),%edi
  pushfl
  popl %ebx
  movl %ebx,%eax
  andl $-2360577,%eax
  orl 40(%esp),%eax
  movl %edi,(%esp)
  pushl %eax
  popfl
  call *%esi
  pushfl
  popl %eax
  pushl %ebx
  popfl
  movl $0,%edx
  leal 12(%esp),%esp
  popl %ebp
  popl %edi
  popl %esi
  popl %ebx
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


def read_witness(bits, image_bits):
    """Restore the caller's flags before C observes a returned snapshot."""
    narrow = image_bits == 16
    if bits == 64:
        original = ".byte 0x66,0x9c\n popw %ax\n movzwl %ax,%eax" if narrow else ".byte 0x9c\n popq %rax"
        normalize = "movzwl %ax,%eax" if narrow else ""
        capture = f'''
  pushq %r12
  pushq %r13
  subq $8,%rsp
  movq %rdi,%r13
  pushfq
  popq %r12
  pushq %rsi
  popfq
  call *%r13
  {normalize}
  pushq %r12
  popfq
  addq $8,%rsp
  popq %r13
  popq %r12
  ret
'''
    else:
        original = ".byte 0x66,0x9c\n popw %ax\n movzwl %ax,%eax" if narrow else ".byte 0x9c\n popl %eax"
        normalize = "movzwl %ax,%eax" if narrow else ""
        capture = f'''
  pushl %ebx
  pushl %esi
  subl $4,%esp
  movl 16(%esp),%esi
  pushfl
  popl %ebx
  pushl 20(%esp)
  popfl
  call *%esi
  {normalize}
  movl $0,%edx
  pushl %ebx
  popfl
  addl $4,%esp
  popl %esi
  popl %ebx
  ret
'''
    return ("\n.text\n.globl original_read\noriginal_read:\n" + original +
            "\n ret\n.globl incorrect_read\nincorrect_read:\n movl $0,%eax" +
            "\n ret\n.globl capture_read\ncapture_read:\n" + capture +
            '.section .note.GNU-stack,"",@progbits\n')


def driver(image_bits, reads):
    if image_bits == 16 and not reads:
        return DRIVER, 131072, 0
    ctype = {16: "unsigned short", 32: "unsigned", 64: "unsigned long long"}[image_bits]
    new_high_count = 1 if image_bits == 16 else 4
    text = f'''
#include <stdint.h>
#include <stdio.h>
extern void original_write({ctype});
extern void tested_write({ctype});
extern uint64_t capture_flags(void (*)({ctype}), unsigned, uint64_t);
extern void original_read(void), tested_read(void);
extern uint64_t capture_read(void (*)(void), unsigned);
static unsigned high(unsigned v) {{
  return (v & 1 ? 0x40000 : 0) | (v & 2 ? 0x200000 : 0);
}}
int main(void) {{
  unsigned writes = 0, reads = 0;
  for (unsigned old = 0; old != 4; ++old)
    for (unsigned word = 0; word != 65536; ++word) {{
      if (word & 0x100) continue;
      for (unsigned next = 0; next != {new_high_count}; ++next) {{
        const unsigned value = word | high(next);
        const uint64_t expected = capture_flags(original_write, value, high(old));
        const uint64_t actual = capture_flags(tested_write, value, high(old));
        if (((expected ^ actual) & 0x247600) != 0) {{
          printf("write value=%x old=%x expected=%llx actual=%llx\\n",
                 value, old, (unsigned long long)expected, (unsigned long long)actual);
          return 1;
        }}
        ++writes;
      }}
'''
    if reads:
        text += '''
      const unsigned value = word | high(old);
      const uint64_t expected = capture_read(original_read, value);
      const uint64_t actual = capture_read(tested_read, value);
      if (((expected ^ actual) & ~UINT64_C(0x8d5)) != 0) {
        printf("read value=%x expected=%llx actual=%llx\\n", value,
               (unsigned long long)expected, (unsigned long long)actual);
        return 2;
      }
      ++reads;
'''
    text += '''
    }
  printf("%u original POPF comparisons, %u original PUSHF comparisons\\n", writes, reads);
  return 0;
}
'''
    return text, 131072 * new_high_count, 131072 if reads else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--compiler", type=Path)
    group.add_argument("--llc", type=Path)
    parser.add_argument("--clang", default="clang")
    parser.add_argument("--bits", type=int, choices=(32, 64), default=64)
    parser.add_argument("--image-bits", type=int, choices=(16, 32, 64), default=16)
    parser.add_argument("--spelling", choices=("builtin", "generic"), default="builtin")
    parser.add_argument("--backend-llc", type=Path,
                        help="Full LLVM backend for NeverC, required for i386 objects")
    parser.add_argument("--source", type=Path,
                        help="An unchanged emitted C translation unit, compiled with --compiler")
    parser.add_argument("--symbol", default="flags_write_test")
    parser.add_argument("--keep", type=Path, required=True)
    args = parser.parse_args()
    if platform.system() != "Linux" or platform.machine() != "x86_64":
        parser.error("the original CPU witness requires Linux x86_64")
    if args.source and not args.compiler:
        parser.error("--source requires --compiler")
    if args.image_bits not in (16, args.bits):
        parser.error("32-bit images require i386; 64-bit images require x86-64")
    if args.spelling == "generic" and (not args.compiler or args.source or args.image_bits != args.bits):
        parser.error("generic spelling requires a built-in source probe at code-mode width")
    if args.backend_llc and (not args.compiler or "neverc" not in args.compiler.name):
        parser.error("--backend-llc requires NeverC")
    if args.bits == 32 and args.compiler and "neverc" in args.compiler.name and not args.backend_llc:
        parser.error("NeverC i386 objects require --backend-llc")
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

    abi = ["-m" + str(args.bits)]
    triple = ("i386" if args.bits == 32 else "x86_64") + "-unknown-linux-gnu"
    reference = REFERENCE32 if args.bits == 32 else REFERENCE
    if args.image_bits != 16:
        # Exchange the original word operation and the deliberately wrong-width
        # control. Only the former receives the architecture's native image.
        reference = reference.replace("original_write", "swap_write").replace(
            "incorrect_wide_write", "original_write").replace("swap_write", "incorrect_wide_write")
    reference += read_witness(args.bits, args.image_bits)
    reads = not args.source
    driver_text, writes_count, reads_count = driver(args.image_bits, reads)
    (root / "reference.s").write_text(reference)
    (root / "driver.c").write_text(driver_text)
    # The emitted source owns its C signature. A separate assembly tail jump
    # adapts only the machine call; do not invent a conflicting C declaration.
    (root / "adapter.s").write_text(
        ".text\n.globl tested_write\n.type tested_write,@function\n"
        "tested_write: jmp " + args.symbol + "\n" +
        (".globl tested_read\ntested_read: jmp flags_read_test\n" if reads else "") +
        '.section .note.GNU-stack,"",@progbits\n')
    run([args.clang, *abi, "-c", root / "adapter.s", "-o", root / "adapter.o"], "adapter")
    run([args.clang, *abi, "-c", root / "reference.s", "-o", root / "reference.o"], "reference")
    run([args.clang, *abi, "-O2", "-Dtested_write=incorrect_wide_write",
         "-Dtested_read=original_read", root / "driver.c",
         root / "reference.o", "-o", root / "negative"], "negative-build")
    run([root / "negative"], "negative", expected=1)
    if reads:
        run([args.clang, *abi, "-O2", "-Dtested_write=original_write",
             "-Dtested_read=incorrect_read", root / "driver.c", root / "reference.o",
             "-o", root / "negative-read"], "negative-read-build")
        run([root / "negative-read"], "negative-read", expected=2)
    if args.llc:
        source = root / "input.ll"
        source_text = IR.replace("i16", f"i{args.image_bits}").replace(".u16", f".u{args.image_bits}")
        source_text += (f"\ndeclare i{args.image_bits} @llvm.x86.flags.read.u{args.image_bits}()\n"
                        f"define i{args.image_bits} @flags_read_test() {{\n"
                        f" %value = call i{args.image_bits} @llvm.x86.flags.read.u{args.image_bits}()\n"
                        f" ret i{args.image_bits} %value\n}}\n")
    else:
        source = root / "input.c"
        ctype = {16: "unsigned short", 32: "unsigned", 64: "unsigned long long"}[args.image_bits]
        source_text = (args.source.read_text() if args.source else
                       SOURCE.replace("unsigned short", ctype).replace("_u16", f"_u{args.image_bits}") +
                       f"\n{ctype} flags_read_test(void) {{ return __builtin_ia32_readeflags_u{args.image_bits}(); }}\n")
        if args.spelling == "generic":
            for operation in ("readeflags", "writeeflags"):
                source_text = source_text.replace(f"__builtin_ia32_{operation}_u{args.image_bits}", "__" + operation)
    source.write_text(source_text)
    for opt in ("O0", "O2"):
        obj, exe = root / (opt + ".o"), root / opt
        if args.llc:
            command = [args.llc, "-" + opt, "-mtriple=" + triple,
                       "-verify-machineinstrs", "-filetype=obj", source, "-o", obj]
        else:
            command = [args.compiler, "--target=" + triple, "-" + opt, "-std=gnu23", "-ffreestanding", "-fno-lto",
                       "-fno-pie", "-c", source, "-o", obj]
            if "neverc" in args.compiler.name:
                command += ["-fno-builtin-mimalloc"]
                if args.backend_llc:
                    command += ["-fllvm-backend=" + str(args.backend_llc.resolve())]
                else:
                    command += ["-mllvm", "-verify-machineinstrs"]
        run(command, opt + "-compile")
        run([args.clang, *abi, "-O2", "-no-pie", root / "driver.c",
             root / "reference.o", root / "adapter.o", obj, "-o", exe], opt + "-link")
        output = run([exe], opt + "-run")
        expected = ("131072 original word-POPF comparisons" if args.image_bits == 16 and not reads else
                    f"{writes_count} original POPF comparisons, {reads_count} original PUSHF comparisons")
        assert output.strip() == expected, output
        results.append({"optimization": opt, "comparisons": writes_count + reads_count,
                        "write_comparisons": writes_count, "read_comparisons": reads_count,
                        "object_sha256": hashlib.sha256(obj.read_bytes()).hexdigest(),
                        "executable_sha256": hashlib.sha256(exe.read_bytes()).hexdigest()})
    (root / "results.json").write_text(json.dumps({"results": results,
        "bits": args.bits,
        "image_bits": args.image_bits, "spelling": args.spelling,
        "negative_control_rejected": True, "source_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
        "negative_read_control_rejected": reads,
        "affinity": sorted(os.sched_getaffinity(0))}, indent=2) + "\n")
    print(json.dumps(results))


if __name__ == "__main__":
    main()
