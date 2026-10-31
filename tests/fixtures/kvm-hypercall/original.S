# Independent original bytes; the compiler's native intrinsic is not used.
# SPDX-License-Identifier: AGPL-3.0-only
.text
.macro ORIGINAL name, opcode
.global \name
.type \name,@function
\name:
 push %rbx
 mov %rdi,%rax
 mov %rsi,%rbx
 mov %rcx,%r9
 mov %rdx,%rcx
 mov %r9,%rdx
 mov %r8,%rsi
 .byte 0x0f,0x01,\opcode
 pop %rbx
 ret
.size \name,.-\name
.endm
ORIGINAL original_vmcall,0xc1
ORIGINAL original_vmmcall,0xd9
.section .note.GNU-stack,"",@progbits
