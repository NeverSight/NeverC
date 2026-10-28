#!/usr/bin/env python3
"""Compare native FNSTCW/FLDCW with an independent byte oracle on Linux x64.

Retain state, exact two-byte memory extent, waiting and page-fault observations.
Faults run in children; the harness never resumes through generated instructions.
"""

import argparse
import hashlib
import json
from pathlib import Path
import platform
import subprocess


HARNESS = r'''
#define _GNU_SOURCE
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <ucontext.h>
#include <unistd.h>

typedef void (*operation)(void *, unsigned *);
extern void original_store(void *, unsigned *), original_load(void *, unsigned *);
extern void generated_store(void *, unsigned *), generated_load(void *, unsigned *);
extern void original_unmask(void *, unsigned *), generated_unmask(void *, unsigned *);
extern void invoke(operation, void *, const void *, void *, unsigned *);
__asm__(
    ".text\n"
    ".globl original_store\noriginal_store:\n"
    "movl $17,(%rsi)\n.byte 0xd9,0x3f\nmovl $23,(%rsi)\nret\n"
    ".globl original_load\noriginal_load:\n"
    "movl $17,(%rsi)\n.byte 0xd9,0x2f\nmovl $23,(%rsi)\nret\n"
    ".globl original_unmask\noriginal_unmask:\n"
    "movl $17,(%rsi)\n.byte 0xd9,0x2f\nmovl $23,(%rsi)\n"
    ".byte 0xd9,0xd0\nmovl $29,(%rsi)\nret\n"
    ".globl invoke\ninvoke:\n"
    "push %rbx\npush %r12\npush %r13\nsub $512,%rsp\n"
    "mov %rdx,%rbx\nmov %rcx,%r12\nmov %rdi,%r13\nmov %rsi,%rdi\n"
    "mov %r8,%rsi\nfxsave64 (%rsp)\nfxrstor64 (%rbx)\ncall *%r13\n"
    "fxsave64 (%r12)\nfxrstor64 (%rsp)\n"
    "add $512,%rsp\npop %r13\npop %r12\npop %rbx\nret\n");

static uintptr_t expected_begin;
static unsigned expected_kind, expected_pending, expected_signal;
static unsigned *fault_marker;
static void on_fault(int number, siginfo_t *info, void *opaque) {
  (void)info;
  const ucontext_t *context = opaque;
  const uintptr_t pc = context->uc_mcontext.gregs[REG_RIP];
  const unsigned trap = context->uc_mcontext.gregs[REG_TRAPNO];
  if (number != expected_signal || trap != (number == SIGFPE ? 16u : 14u) ||
      pc < expected_begin || pc >= expected_begin + 128 ||
      *fault_marker != (expected_kind == 2 ? 23u : 17u))
    _exit(90);
  const unsigned char *instruction = (const unsigned char *)pc;
  if ((*instruction & 0xf0) == 0x40) ++instruction;
  if (instruction[0] != 0xd9 ||
      (expected_kind == 2 ? instruction[1] != 0xd0 :
       (instruction[1] >> 6) == 3 ||
       ((instruction[1] >> 3) & 7) != (expected_kind ? 5 : 7)) ||
      context->uc_mcontext.fpregs->cwd != (expected_pending ? 0x37e : 0x37f) ||
      (context->uc_mcontext.fpregs->swd & 0x81) != (expected_pending ? 0x81 : 0))
    _exit(91);
  _exit(0);
}

static void word(uint8_t *p, uint16_t value) {
  p[0] = value;
  p[1] = value >> 8;
}
static uint16_t read_word(const uint8_t *p) {
  return p[0] | (uint16_t)p[1] << 8;
}
static int fault_case(operation fn, unsigned pending, unsigned bad, unsigned kind,
                      void *inaccessible) {
  const pid_t child = fork();
  if (child < 0) return 1;
  if (child == 0) {
    unsigned marker = 0;
    expected_begin = (uintptr_t)fn;
    expected_kind = kind;
    expected_pending = pending;
    expected_signal = pending && kind ? SIGFPE : bad ? SIGSEGV : 0;
    fault_marker = &marker;
    struct sigaction action = {.sa_sigaction = on_fault, .sa_flags = SA_SIGINFO};
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGFPE, &action, 0) || sigaction(SIGSEGV, &action, 0))
      _exit(92);
    _Alignas(16) uint8_t before[512] = {0}, after[512] = {0};
    uint16_t value = kind == 2 ? 0x37e : 0x37f;
    word(before, pending == 1 ? 0x37e : 0x37f);
    word(before + 2, pending == 2 ? 1 : pending ? 0x81 : 0);
    word(before + 24, 0x1f80);
    invoke(fn, bad ? inaccessible : &value, before, after, &marker);
    _exit(expected_signal || marker != 23 ||
          (!kind && value != read_word(before)) ? 93 : 0);
  }
  int status = 0;
  return waitpid(child, &status, 0) != child || !WIFEXITED(status) ||
         WEXITSTATUS(status) != 0;
}

int main(void) {
  const operation functions[2][2] = {
      {original_store, original_load}, {generated_store, generated_load}};
  unsigned observations = 0, boundary_observations = 0, fault_cases = 0, faults = 0;
  for (unsigned top = 0; top != 8; ++top)
    for (unsigned tags = 0; tags != 3; ++tags)
      for (unsigned pc = 0; pc != 4; ++pc) {
        if (pc == 1) continue;
        for (unsigned rc = 0; rc != 4; ++rc)
          for (unsigned kind = 0; kind != 2; ++kind)
            for (unsigned implementation = 0; implementation != 2; ++implementation) {
              _Alignas(16) uint8_t before[512] = {0}, after[512] = {0};
              uint8_t buffer[8];
              memset(buffer, 0xa5, sizeof(buffer));
              const uint16_t initial = 0x7f | pc << 8 | rc << 10;
              const uint16_t target = 0x7f | (pc == 3 ? 0 : 3) << 8 | (3 - rc) << 10;
              word(before, initial);
              word(before + 2, 0x4725 | top << 11);
              before[4] = (uint8_t[]){0, 0x55, 0xff}[tags];
              word(before + 24, 0x1f80);
              for (unsigned reg = 0; reg != 8; ++reg) {
                before[32 + reg * 16] = reg + 1;
                before[39 + reg * 16] = 0x80;
                word(before + 40 + reg * 16, 0x3fff);
              }
              word(buffer + 3, target);
              unsigned marker = 0;
              invoke(functions[implementation][kind], buffer + 3, before, after, &marker);
              if (marker != 23 || read_word(after) != (kind ? target : initial) ||
                  ((read_word(after + 2) ^ read_word(before + 2)) & 0xb8ff) ||
                  after[4] != before[4] ||
                  read_word(buffer + 3) != (kind ? target : initial)) return 2;
              for (unsigned index = 0; index != sizeof(buffer); ++index)
                if (index != 3 && index != 4 && buffer[index] != 0xa5) return 3;
              for (unsigned reg = 0; reg != 8; ++reg)
                if (memcmp(before + 32 + reg * 16, after + 32 + reg * 16, 10)) return 4;
              printf("{\"kind\":%u,\"generated\":%u,\"top\":%u,\"tags\":%u,"
                     "\"pc\":%u,\"rc\":%u,\"cw\":%u,\"sw_defined\":%u}\n",
                     kind, implementation, top, before[4], pc, rc, read_word(after),
                     read_word(after + 2) & 0xb8ff);
              ++observations;
            }
      }
  const long pagesize = sysconf(_SC_PAGESIZE);
  if (pagesize <= 0) return 5;
  uint8_t *pages = mmap(0, pagesize * 2, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (pages == MAP_FAILED || mprotect(pages + pagesize, pagesize, PROT_NONE)) return 6;
  for (unsigned implementation = 0; implementation != 2; ++implementation)
    for (unsigned kind = 0; kind != 2; ++kind) {
      _Alignas(16) uint8_t before[512] = {0}, after[512] = {0};
      word(before, 0x37f);
      word(before + 24, 0x1f80);
      word(pages + pagesize - 2, 0xb7f);
      pages[pagesize - 3] = 0xa5;
      unsigned marker = 0;
      invoke(functions[implementation][kind], pages + pagesize - 2,
             before, after, &marker);
      if (marker != 23 || read_word(after) != (kind ? 0xb7f : 0x37f) ||
          read_word(pages + pagesize - 2) != (kind ? 0xb7f : 0x37f) ||
          pages[pagesize - 3] != 0xa5) return 8;
      printf("{\"boundary_case\":true,\"generated\":%u,\"kind\":%u}\n",
             implementation, kind);
      ++boundary_observations;
    }
  for (unsigned implementation = 0; implementation != 2; ++implementation)
    for (unsigned pending = 0; pending != 2; ++pending)
      for (unsigned bad = 0; bad != 3; ++bad)
        for (unsigned kind = 0; kind != 2; ++kind) {
          void *inaccessible = pages + pagesize - (bad == 2);
          if (fault_case(functions[implementation][kind], pending, bad, kind, inaccessible)) {
            fprintf(stderr, "fault mismatch: generated=%u pending=%u bad=%u kind=%u\n",
                    implementation, pending, bad, kind);
            return 7;
          }
          const unsigned signal = pending && kind ? SIGFPE : bad ? SIGSEGV : 0;
          printf("{\"fault_case\":true,\"generated\":%u,\"pending\":%u,"
                 "\"bad_operand\":%u,\"kind\":%u,\"signal\":%u}\n",
                 implementation, pending, bad, kind, signal);
          faults += !!signal;
          ++fault_cases;
        }
  const operation unmask[] = {original_unmask, generated_unmask};
  for (unsigned implementation = 0; implementation != 2; ++implementation) {
    if (fault_case(unmask[implementation], 2, 0, 2, pages + pagesize)) return 9;
    printf("{\"deferred_unmask\":true,\"generated\":%u,\"signal\":%u}\n",
           implementation, SIGFPE);
    ++faults;
    ++fault_cases;
  }
  munmap(pages, pagesize * 2);
  printf("{\"state_observations\":%u,\"fault_observations\":%u,"
         "\"fault_and_nonfault_cases\":%u,\"boundary_observations\":%u}\n",
         observations, faults, fault_cases, boundary_observations);
  return 0;
}
'''


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--llc", type=Path)
    parser.add_argument("--opt", type=Path,
                        help="LLVM optimizer; defaults to the sibling of --llc")
    parser.add_argument("--neverc", type=Path)
    parser.add_argument("--clang", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    if not args.llc and not args.neverc:
        parser.error("provide --llc and/or --neverc")
    if platform.system() != "Linux" or platform.machine() != "x86_64":
        parser.error("native Linux x64 is required")
    if args.llc and not args.opt:
        args.opt = args.llc.with_name("opt")
    tools = {str(path.resolve()): digest(path.resolve())
             for path in (args.llc, args.opt, args.neverc, args.clang) if path}
    script_hash = digest(Path(__file__))
    root = args.output_dir.resolve()
    root.mkdir(parents=True, exist_ok=False)
    (root / "cpuinfo.txt").write_text(Path("/proc/cpuinfo").read_text())
    harness = root / "harness.c"
    harness.write_text(HARNESS)
    commands, observations = [], []

    def run(command, name):
        command = list(map(str, command))
        result = subprocess.run(command, capture_output=True, text=True, timeout=120)
        (root / (name + ".stdout")).write_text(result.stdout)
        (root / (name + ".stderr")).write_text(result.stderr)
        commands.append({"argv": command, "returncode": result.returncode})
        (root / "commands.json").write_text(json.dumps(commands, indent=2) + "\n")
        result.check_returncode()
        return result.stdout

    profiles = [name for name in ("llc", "neverc") if getattr(args, name)]
    if args.llc and args.neverc:
        profiles.append("neverc-llvm")
    for compiler in profiles:
        tool = (args.llc if compiler == "llc" else args.neverc).resolve()
        source = root / (compiler + (".ll" if compiler == "llc" else ".c"))
        source.write_text("\n".join(
            (f"declare void @llvm.x86.{instruction}(ptr)\n"
             f"define void @generated_{name}(ptr %p, ptr %marker) {{\n"
             f"store i32 17, ptr %marker\ncall void @llvm.x86.{instruction}(ptr %p)\n"
             "store i32 23, ptr %marker\nret void\n}\n")
            if compiler == "llc" else
            (f"void generated_{name}(void *p, unsigned *marker) {{\n"
             f"*marker=17; __builtin_ia32_{instruction}(p); *marker=23;\n}}\n")
            for name, instruction in (("store", "fnstcw"), ("load", "fldcw"))) +
            ("declare void @llvm.x86.fnop()\n"
             "define void @generated_unmask(ptr %p, ptr %marker) {\n"
             "store i32 17, ptr %marker\ncall void @llvm.x86.fldcw(ptr %p)\n"
             "store i32 23, ptr %marker\ncall void @llvm.x86.fnop()\n"
             "store i32 29, ptr %marker\nret void\n}\n"
             if compiler == "llc" else
             "void generated_unmask(void *p, unsigned *marker) {\n"
             "*marker=17; __builtin_ia32_fldcw(p); *marker=23;\n"
             "__builtin_ia32_fnop(); *marker=29;\n}\n"))
        for opt in (0, 2):
            name = f"{compiler}-O{opt}"
            obj, binary = root / (name + ".o"), root / name
            if compiler == "llc":
                ir = source
                if opt:
                    ir = root / (name + ".ll")
                    run([args.opt.resolve(), "-S", "-passes=default<O2>",
                         source, "-o", ir], name + "-optimize")
                command = [tool, "-mtriple=x86_64-unknown-linux-gnu",
                           "-verify-machineinstrs", "-filetype=obj", f"-O{opt}", ir, "-o", obj]
            elif compiler == "neverc":
                command = [tool, "--target=x86_64-unknown-linux-gnu", "-fno-lto",
                           "-fno-builtin-mimalloc", "-c", f"-O{opt}", source, "-o", obj]
            else:
                ir = root / (name + ".ll")
                run([tool, "--target=x86_64-unknown-linux-gnu", "-fno-lto",
                     "-fno-builtin-mimalloc", "-S", "-emit-llvm", f"-O{opt}", source,
                     "-o", ir], name + "-frontend")
                command = [args.llc.resolve(), "-mtriple=x86_64-unknown-linux-gnu",
                           "-verify-machineinstrs", "-filetype=obj", f"-O{opt}", ir, "-o", obj]
            run(command, name + "-compile")
            run([args.clang.resolve(), "-O2", "-mno-red-zone", harness, obj,
                 "-o", binary], name + "-link")
            rows = [json.loads(row) for row in run([binary], name + "-execute").splitlines()]
            result = rows[-1]
            expected = {"state_observations": 1152, "fault_observations": 20,
                        "fault_and_nonfault_cases": 26, "boundary_observations": 4}
            if result != expected or len(rows) != 1183:
                raise ValueError("incomplete native observation inventory")
            observations.append({"profile": name, **result, "object_sha256": digest(obj),
                                 "binary_sha256": digest(binary)})
    if tools != {path: digest(Path(path)) for path in tools} or \
            script_hash != digest(Path(__file__)):
        raise ValueError("compiler or qualification script changed during execution")
    record = {"passed": True, "profiles": observations,
              "harness_sha256": digest(harness),
              "script_sha256": script_hash, "stable_inputs": True,
              "tools": tools, "platform": platform.platform(),
              "files": {str(path.relative_to(root)): {"sha256": digest(path),
                                                       "bytes": path.stat().st_size}
                        for path in sorted(root.rglob("*")) if path.is_file()},
              "exclusions": ["other CPU models", "i386 runtime", "privileged #NM",
                             "alignment-check exceptions", "general x87 state equivalence"],
              "reference": "independently authored D9 /7 and D9 /5 opcodes"}
    (root / "qualification.json").write_text(json.dumps(record, indent=2) + "\n")
    print(json.dumps({"passed": True, "profiles": len(observations)}, indent=2))


if __name__ == "__main__":
    main()
