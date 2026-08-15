#!/usr/bin/env python3
"""Contract: task_user_state_snapshot locates pt_regs like GKI task_pt_regs.

ARM64 GKI:
  #define task_pt_regs(p) \\
    ((struct pt_regs *)(THREAD_SIZE + task_stack_page(p)) - 1)

That is stack + THREAD_SIZE - sizeof(pt_regs).  A leftover 16-byte
THREAD_START_SP pad (userspace initial SP, not the exception frame)
shifts the read by one GP register: PC (offset 256) aliases x30/LR
(offset 240).  SIGNAL then compares LR against the handshake stub and
silently fail-opens.
"""

from pathlib import Path
import sys

TOOLS_ROOT = Path(__file__).resolve().parent
RUNTIME_ROOT = TOOLS_ROOT.parent
SOURCE = RUNTIME_ROOT / "src" / "nvk_process.c"

# user_pt_regs: regs[31] at 0, sp at 248, pc at 256, pstate at 264
PT_REGS_PC = 256
PT_REGS_X30 = 30 * 8
THREAD_START_SP_PAD = 16


def main():
    src = SOURCE.read_text(encoding="utf-8")
    if "_NEVERC_KRT_ARM64_THREAD_START_GAP" in src:
        raise SystemExit(
            "test-task-user-state-pt-regs: RED -- THREAD_START_GAP pad is back"
        )
    if "_neverc_krt_task_pt_regs_addr" not in src:
        raise SystemExit(
            "test-task-user-state-pt-regs: RED -- missing pt_regs addr helper"
        )
    if "stack + stack_size - pt_regs_size" not in src:
        raise SystemExit(
            "test-task-user-state-pt-regs: RED -- helper does not match "
            "GKI task_pt_regs"
        )

    if PT_REGS_PC - THREAD_START_SP_PAD != PT_REGS_X30:
        raise SystemExit(
            "test-task-user-state-pt-regs: RED -- fixture offsets drifted"
        )

    stack = 0xFFFF800040000000
    stack_size = 16384
    pt_regs_size = 336
    kernel_macro = stack + stack_size - pt_regs_size
    wrong_gap = stack + stack_size - THREAD_START_SP_PAD - pt_regs_size
    if kernel_macro - wrong_gap != THREAD_START_SP_PAD:
        raise SystemExit(
            "test-task-user-state-pt-regs: RED -- gap arithmetic fixture"
        )

    print("test-task-user-state-pt-regs: OK")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except SystemExit:
        raise
    except Exception as error:
        print(f"test-task-user-state-pt-regs: RED -- {error}", file=sys.stderr)
        sys.exit(1)
