# Independent original instruction bytes, cdecl inputs and result.
# SPDX-License-Identifier: AGPL-3.0-only
.text
.code32
.macro ORIGINAL name, opcode
.globl original_\name
original_\name:
  push %ebx
  push %esi
  mov 12(%esp),%eax
  mov 16(%esp),%ebx
  mov 20(%esp),%ecx
  mov 24(%esp),%edx
  mov 28(%esp),%esi
  .byte 0x0f,0x01,\opcode
  pop %esi
  pop %ebx
  ret
.endm
ORIGINAL vmcall,0xc1
ORIGINAL vmmcall,0xd9
.section .note.GNU-stack,"",@progbits
