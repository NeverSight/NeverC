#!/usr/bin/env python3
"""Check module-entry failure semantics and the non-sleeping current call graph."""
import argparse
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile
import unittest

TOOLS = Path(__file__).resolve().parent
KERNEL = TOOLS.parent
COMPILER = None


def functions(ir):
    return dict(re.findall(r'^define[^\n]*@([\w.$]+)\([^\n]*\)[^\n]*\{\n(.*?)^}',
                           ir, re.MULTILINE | re.DOTALL))


class CurrentInitializationTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="neverc-current-init-")
        self.root = Path(self.temporary.name)
        self.addCleanup(self.temporary.cleanup)

    def emit_ir(self, source, profile=618):
        cfile, output = self.root / "test.c", self.root / "test.ll"
        cfile.write_text(source)
        subprocess.run([
            str(COMPILER), "--target=aarch64-linux-android",
            "-fandroid-kernel-driver-mode", f"-DNVK_KERNEL={profile}",
            f"-I{KERNEL / 'arm64/include'}", f"-I{KERNEL / 'include'}",
            "-O0", "-fno-lto", "-S", "-emit-llvm", str(cfile), "-o", str(output),
        ], check=True, capture_output=True, text=True)
        return output.read_text()

    def test_failure_blocks_user_entry_and_success_preserves_its_result(self):
        for alias in (False, True):
            with self.subTest(module_init_alias=alias):
                name = "user_init" if alias else "init_module"
                source = ("#include <nvkmod.h>\nint user_entered;\n"
                          f"int {name}(void) {{ user_entered++; return 17; }}\n")
                if alias:
                    source += "module_init(user_init);\n"
                body = functions(self.emit_ir(source))[name]
                # Replace ARM64 symbol-address materialization with the same
                # host pointer. Keep the actual KCFI prefix check executable.
                body = re.sub(r'call ptr asm[^\n]*\(ptr @neverc_krt_prepare_current\)',
                              'freeze ptr @neverc_krt_prepare_current', body)
                type_check = re.search(r'icmp eq i32 %nvk.current.type, (-?[0-9]+)', body)
                prefix = type_check.group(1) if type_check else "0"
                # Execute the actual emitted entry CFG on the host, replacing
                # only the sleepable kernel initializer with a controlled one.
                # No target-specific instructions occur in this entry fixture.
                ir = '''
@user_entered = global i32 0
@hook_status = global i32 -22
@hook_profile = global i32 0
@hook_calls = global i32 0
define i32 @neverc_krt_prepare_current(i32 %profile) prefix i32 PREFIX {
  store i32 %profile, ptr @hook_profile
  %calls = load i32, ptr @hook_calls
  %next = add i32 %calls, 1
  store i32 %next, ptr @hook_calls
  %status = load i32, ptr @hook_status
  ret i32 %status
}
define i32 @tested_entry() {
''' + body + '''}
define i32 @main() {
  %first = call i32 @tested_entry()
  %entered0 = load i32, ptr @user_entered
  %profile = load i32, ptr @hook_profile
  %failed = icmp eq i32 %first, -22
  %skipped = icmp eq i32 %entered0, 0
  %family = icmp eq i32 %profile, 618
  store i32 0, ptr @hook_status
  %second = call i32 @tested_entry()
  %entered1 = load i32, ptr @user_entered
  %calls = load i32, ptr @hook_calls
  %success = icmp eq i32 %second, 17
  %entered = icmp eq i32 %entered1, 1
  %twice = icmp eq i32 %calls, 2
  %a = and i1 %failed, %skipped
  %b = and i1 %a, %family
  %c = and i1 %b, %success
  %d = and i1 %c, %entered
  %ok = and i1 %d, %twice
  %exit = select i1 %ok, i32 0, i32 1
  ret i32 %exit
}
'''
                ir = ir.replace("PREFIX", prefix)
                fixture, binary = self.root / "entry.ll", self.root / "entry"
                fixture.write_text(ir)
                subprocess.run(shlex.split(os.environ.get("CLANG", "clang")) + [
                    "-Wno-override-module", "-x", "ir", str(fixture), "-o", str(binary)
                ], check=True, capture_output=True, text=True)
                result = subprocess.run([str(binary)])
                self.assertEqual(result.returncode, 0,
                                 "entry must pass the family, propagate failure, and skip user code")

    def test_current_call_graph_cannot_initialize_or_call_an_external_service(self):
        ir = self.emit_ir("#include <linux/sched.h>\n"
                          "struct task_struct *probe_current(void) { return current; }\n")
        bodies = functions(ir)
        pending, visited = ["probe_current"], set()
        while pending:
            name = pending.pop()
            if name in visited:
                continue
            visited.add(name)
            self.assertNotIn(name, {"neverc_krt_bootstrap", "neverc_krt_prepare_current"},
                             "current must not enter a sleepable initialization path")
            self.assertIn(name, bodies, f"current reaches external service {name}")
            for line in bodies[name].splitlines():
                if not re.search(r'\b(?:call|invoke)\s', line):
                    continue
                if ' asm ' in line:
                    self.assertRegex(line, r'\bmrs\b', "unexpected current inline assembly")
                    continue
                callees = re.findall(r'@([\w.$]+)\(', line)
                self.assertTrue(callees, f"current reaches an indirect call: {line}")
                pending.extend(c for c in callees if not c.startswith("llvm."))
        self.assertIn("neverc_krt_current_task", visited)
        self.assertIn("neverc_krt_prepare_current", bodies,
                      "callback-only TU must retain the entry hook for the other TU")

    def test_stable_register_families_do_not_require_kprobe_exports(self):
        source = ("#include <linux/sched.h>\n"
                  "int init_module(void) { return current ? 0 : -1; }\n")
        for profile in (510, 51013, 515, 51514, 601, 606, 612, 703):
            with self.subTest(profile=profile):
                ir = self.emit_ir(source, profile)
                for symbol in ("neverc_krt_prepare_current", "neverc_krt_bootstrap",
                               "register_kprobe", "unregister_kprobe"):
                    self.assertFalse("@" + symbol + "(" in ir,
                                     f"SP_EL0 current must not require {symbol}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, required=True)
    args, rest = parser.parse_known_args()
    COMPILER = args.compiler.resolve()
    unittest.main(argv=[__file__, *rest])
