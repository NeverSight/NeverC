#!/usr/bin/env python3
"""Execute generated FNOP and an independent byte oracle on Linux x64.

Keep every input, compiler command and observation. No FEMMS is executed.
The native FIP check requires an Intel CPU; portable profile observations
and compiler transport are separate evidence.
"""

import argparse
import hashlib
import json
import platform
import subprocess
from pathlib import Path


HARNESS = r'''
#define _GNU_SOURCE
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <ucontext.h>
#include <unistd.h>

extern void generated_fnop(void);
static uintptr_t expected_fault_begin;
__attribute__((noinline)) static void original_fnop(void) {
  __asm__ volatile(".byte 0xd9, 0xd0" : : : "memory");
}
static void fault(int signal, siginfo_t *info, void *context) {
  (void)info;
  const ucontext_t *uc = context;
  const uintptr_t ip = uc->uc_mcontext.gregs[REG_RIP];
  if (signal != SIGFPE || uc->uc_mcontext.gregs[REG_TRAPNO] != 16 ||
      ip < expected_fault_begin || ip >= expected_fault_begin + 64) _exit(98);
  const unsigned char *instruction = (const unsigned char *)ip;
  _exit(instruction[0] == 0xd9 && instruction[1] == 0xd0 ? 0 : 98);
}
static int pending(void (*operation)(void)) {
  pid_t child = fork();
  if (child < 0) return 1;
  if (child == 0) {
    expected_fault_begin = (uintptr_t)operation;
    struct sigaction action = {.sa_sigaction = fault, .sa_flags = SA_SIGINFO};
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGFPE, &action, 0)) _exit(97);
    _Alignas(16) unsigned char state[512];
    __asm__ volatile("fxsave64 %0" : "=m"(state) : : "memory");
    state[0] = 0x7e; state[1] = 3; /* Unmask invalid. */
    state[2] = 0x81; state[3] = 0x80; /* Pending IE/ES/B. */
    __asm__ volatile("fxrstor64 %0" : : "m"(state) : "memory", "st", "st(1)",
                     "st(2)", "st(3)", "st(4)", "st(5)", "st(6)", "st(7)",
                     "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7",
                     "xmm8", "xmm9", "xmm10", "xmm11", "xmm12", "xmm13", "xmm14", "xmm15");
    operation();
    _exit(99);
  }
  int status = 0;
  return waitpid(child, &status, 0) != child || !WIFEXITED(status) ||
         WEXITSTATUS(status) != 0;
}
static unsigned word(const unsigned char *p) { return p[0] | p[1] << 8; }
static int check(void (*operation)(void), unsigned top, unsigned tags) {
  _Alignas(16) unsigned char host[512], before[512], after[512];
  __asm__ volatile("fxsave64 %0" : "=m"(host) : : "memory");
  memcpy(before, host, sizeof(before));
  before[0] = 0x7f; before[1] = 3;
  before[2] = 0x25; before[3] = 0x47 | (top << 3);
  before[4] = tags;
  for (unsigned i = 32; i < 160; ++i) before[i] = i ^ 0xa5;
  __asm__ volatile("fxrstor64 %0" : : "m"(before) : "memory", "st", "st(1)",
                   "st(2)", "st(3)", "st(4)", "st(5)", "st(6)", "st(7)",
                   "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7",
                   "xmm8", "xmm9", "xmm10", "xmm11", "xmm12", "xmm13", "xmm14", "xmm15");
  operation();
  __asm__ volatile("fxsave64 %0\n\tfxrstor64 %1" : "=m"(after) : "m"(host)
                   : "memory", "st", "st(1)", "st(2)", "st(3)", "st(4)",
                     "st(5)", "st(6)", "st(7)",
                     "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7",
                     "xmm8", "xmm9", "xmm10", "xmm11", "xmm12", "xmm13", "xmm14", "xmm15");
  if (word(before) != word(after) || before[4] != after[4] ||
      ((word(before + 2) ^ word(after + 2)) & ~0x4700U)) return 1;
  for (unsigned i = 0; i < 8; ++i)
    if (memcmp(before + 32 + 16 * i, after + 32 + 16 * i, 10)) return 2;
  uint64_t ip;
  memcpy(&ip, after + 8, sizeof(ip));
  if (ip < (uintptr_t)operation || ip >= (uintptr_t)operation + 64) return 3;
  const unsigned char *instruction = (const unsigned char *)(uintptr_t)ip;
  return instruction[0] != 0xd9 || instruction[1] != 0xd0;
}
int main(void) {
  unsigned observations = 0;
  const unsigned tags[] = {0, 0x55, 0xff};
  void (*operations[])(void) = {original_fnop, generated_fnop};
  for (unsigned op = 0; op < 2; ++op)
    for (unsigned top = 0; top < 8; ++top)
      for (unsigned t = 0; t < 3; ++t) {
        int result = check(operations[op], top, tags[t]);
        if (result) { fprintf(stderr, "state %u/%u/%u: %d\n", op, top, t, result); return 1; }
        ++observations;
      }
  if (pending(original_fnop) || pending(generated_fnop)) return 2;
  printf("{\"state_observations\":%u,\"waiting_faults\":2}\n", observations);
  return 0;
}
'''


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--llc", type=Path)
    parser.add_argument("--neverc", type=Path)
    parser.add_argument("--clang", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    if not args.llc and not args.neverc:
        parser.error("provide --llc and/or --neverc")
    if platform.system() != "Linux" or platform.machine() != "x86_64":
        parser.error("native Linux x64 is required")
    cpu = Path("/proc/cpuinfo").read_text()
    if "GenuineIntel" not in cpu:
        parser.error("the FIP observation requires the Intel profile")
    root = args.output_dir.resolve()
    root.mkdir(parents=True, exist_ok=False)
    (root / "cpuinfo.txt").write_text(cpu)
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
        tool = args.llc if compiler == "llc" else args.neverc
        tool = tool.resolve()
        source = root / (compiler + (".ll" if compiler == "llc" else ".c"))
        source.write_text(
            "declare void @llvm.x86.fnop()\n"
            "define void @generated_fnop() { call void @llvm.x86.fnop()\nret void }\n"
            if compiler == "llc" else
            "void generated_fnop(void) { __builtin_ia32_fnop(); }\n")
        for opt in (0, 2):
            name = f"{compiler}-O{opt}"
            obj, binary = root / (name + ".o"), root / name
            if compiler == "llc":
                command = [tool, "-mtriple=x86_64-unknown-linux-gnu",
                           "-verify-machineinstrs", "-filetype=obj", f"-O{opt}", source, "-o", obj]
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
            run([args.clang.resolve(), "-O0", "-mno-red-zone", harness, obj, "-o", binary], name + "-link")
            result = json.loads(run([binary], name + "-execute"))
            if result != {"state_observations": 48, "waiting_faults": 2}:
                raise ValueError("incomplete native observation inventory")
            observations.append({"profile": name, **result, "object_sha256": digest(obj),
                                 "executable_sha256": digest(binary), "compiler_sha256": digest(tool)})
            if compiler == "neverc-llvm":
                observations[-1]["backend_sha256"] = digest(args.llc.resolve())
    (root / "qualification.json").write_text(json.dumps({
        "passed": True, "host": platform.platform(), "observations": observations,
        "scope": "Intel FNOP CW, defined SW, TOP, tags, payloads, FIP and pending #MF; no FEMMS execution",
        "harness_sha256": digest(harness)}, indent=2) + "\n")
    print(json.dumps(observations, indent=2))


if __name__ == "__main__":
    main()
