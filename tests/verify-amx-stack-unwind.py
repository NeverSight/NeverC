#!/usr/bin/env python3
"""Exercise AMX stack-leaf signal unwinding and resumption on Linux x64.

Replace only independently decoded AMX instruction slots with UD2 and NOPs.
The CPU executes the original compiler prologues, calls, signal delivery,
unwinding and epilogues. This validates frame ownership, not AMX hardware.
"""

import argparse
import importlib.util
import itertools
from pathlib import Path
import subprocess

import lief

SPEC = importlib.util.spec_from_file_location(
    "amx_controls", Path(__file__).with_name("verify-amx-explicit-controls.py")
)
AMX = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(AMX)


def run(command):
    result = subprocess.run(list(map(str, command)), capture_output=True, text=True)
    assert result.returncode == 0, (command, result.stdout, result.stderr)
    return result.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--clang", type=Path, required=True)
    parser.add_argument("--llc", type=Path)
    parser.add_argument("--keep", type=Path, required=True)
    args = parser.parse_args()
    args.keep.mkdir(parents=True, exist_ok=True)
    cases = list(itertools.product(
        ("tileloadd64", "tileloaddt164", "tileloaddrs64", "tileloaddrst164", "tilestored64"),
        (0x100, 0x126, 0x12e, 0x136, 0x13e)))
    source = "#include <stdint.h>\nextern volatile uintptr_t expected_frame;\n"
    for i, (name, prefix) in enumerate(cases):
        source += f'''
__attribute__((noinline, target("amx-tile,amx-movrs")))
uint64_t probe_{i}(uint64_t p, uint64_t stride, unsigned n) {{
  _Alignas(64) volatile uint64_t fixed[8];
  volatile unsigned char *dynamic = __builtin_alloca(n);
  fixed[0] = UINT64_C(0x123456789abcdef0);
  fixed[7] = UINT64_C(0xfedcba9876543210);
  dynamic[0] = 0xa5; dynamic[n - 1] = 0x5a;
  expected_frame = (uintptr_t)__builtin_frame_address(0);
  __builtin_ia32_{name}_address({i % 8}, p, stride, 64, {prefix}, 0);
  return fixed[0] ^ fixed[7] ^ dynamic[0] ^ dynamic[n - 1];
}}
'''
    declarations = "\n".join(
        f"uint64_t probe_{i}(uint64_t, uint64_t, unsigned);" for i in range(len(cases)))
    entries = ", ".join(f"probe_{i}" for i in range(len(cases)))
    harness = r'''
#define _GNU_SOURCE
#include <stdint.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <ucontext.h>
#include <unwind.h>
typedef uint64_t (*probe_fn)(uint64_t, uint64_t, unsigned);
volatile uintptr_t expected_frame;
static uint64_t expected_base, expected_stride;
static probe_fn current_probe;
static volatile sig_atomic_t errors, seen, traps;
static _Unwind_Reason_Code visit(struct _Unwind_Context *context, void *unused) {
  (void)unused;
  if (_Unwind_GetRegionStart(context) == (uintptr_t)current_probe) {
    ++seen;
    if (_Unwind_GetGR(context, 6) != expected_frame) ++errors;
  }
  return _URC_NO_REASON;
}
static void trap(int signal, siginfo_t *info, void *raw) {
  (void)info;
  ucontext_t *context = raw;
  greg_t *regs = context->uc_mcontext.gregs;
  const unsigned char *pc = (void *)(uintptr_t)regs[REG_RIP];
  if (signal != SIGILL || pc[0] != 0x0f || pc[1] != 0x0b) _Exit(90);
  if ((uint64_t)regs[REG_RBP] != expected_base ||
      (uint64_t)regs[REG_RCX] != expected_stride) ++errors;
  seen = 0;
  _Unwind_Backtrace(visit, NULL);
  if (seen != 1) ++errors;
  ++traps;
  regs[REG_RIP] += 2;
}
'''
    harness += declarations + "\nstatic probe_fn probes[] = {" + entries + "};\n"
    harness += r'''
int main(void) {
  struct sigaction action = {0};
  action.sa_sigaction = trap; action.sa_flags = SA_SIGINFO;
  sigemptyset(&action.sa_mask);
  if (sigaction(SIGILL, &action, NULL)) return 1;
  const uint64_t bases[] = {0, 0x12345678, 0x100000000, 0xface000000001234, UINT64_MAX};
  const uint64_t strides[] = {0, 64, 0x8765432100000040};
  const unsigned allocations[] = {65, 129, 513};
  unsigned calls = 0;
  for (unsigned p = 0; p < sizeof(probes) / sizeof(probes[0]); ++p)
    for (unsigned b = 0; b < sizeof(bases) / sizeof(bases[0]); ++b)
      for (unsigned s = 0; s < sizeof(strides) / sizeof(strides[0]); ++s)
        for (unsigned n = 0; n < sizeof(allocations) / sizeof(allocations[0]); ++n) {
          current_probe = probes[p];
          expected_base = bases[b]; expected_stride = strides[s];
          uint64_t result = current_probe(expected_base, expected_stride, allocations[n]);
          if (result != (UINT64_C(0x123456789abcdef0) ^ UINT64_C(0xfedcba9876543210) ^ 0xff)) ++errors;
          ++calls;
        }
  printf("%u calls, %d traps, %d unwind/frame errors\n", calls, traps, errors);
  return errors != 0 || (unsigned)traps != calls;
}
'''
    src, driver = args.keep / "probe.c", args.keep / "driver.c"
    src.write_text(source)
    driver.write_text(harness)
    for opt in ("O0", "O2"):
        obj, patched = args.keep / f"{opt}.o", args.keep / f"{opt}-trap.o"
        command = [args.compiler, "--target=x86_64-unknown-linux-gnu", "-" + opt,
                   "-fno-lto", "-fno-builtin-mimalloc", "-fno-function-sections",
                   "-fno-data-sections", "-fno-stack-protector", "-fasynchronous-unwind-tables",
                   "-mllvm", "-verify-machineinstrs", src]
        if args.llc:
            ir = args.keep / f"{opt}.ll"
            run(command + ["-S", "-emit-llvm", "-o", ir])
            assert " asm " not in ir.read_text()
            run([args.llc, "-" + opt, "-verify-machineinstrs", "-filetype=obj", ir, "-o", obj])
        else:
            run(command + ["-c", "-o", obj])
        binary = lief.parse(str(obj))
        section = next(s for s in binary.sections if s.name == ".text")
        native = AMX.memory_decode(bytes(section.content))
        assert len(native) == len(cases)
        raw = bytearray(obj.read_bytes())
        for start, decoded in native.items():
            end = decoded[-1]
            assert decoded[-2] == 5 and end - start >= 2
            raw[section.offset + start:section.offset + end] = b"\x0f\x0b" + b"\x90" * (end - start - 2)
        patched.write_bytes(raw)
        exe = args.keep / opt
        run([args.clang, "-O2", "-no-pie", "-fasynchronous-unwind-tables", driver, patched, "-o", exe])
        print(opt + ": " + run([exe]).strip())
    print("PASS: instrumented native signal unwinding and resumption; AMX execution not qualified")


if __name__ == "__main__":
    main()
