.section .text.start,"ax"
.code64
.global stage64
stage64:
 mov $0x1f0000, %rsp
 mov $__bss_start, %edi
 mov $__bss_end, %ecx
 sub %edi, %ecx
 xor %eax, %eax
 rep stosb
 call guest_main
 ud2

.section .text,"ax"
.global branch_control_call
branch_control_call:
 push %r12
 push %r13
 push %r14
 mov %rdi, %r13
 mov %rdx, %r12
 mov %rcx, %r14
 # The guest loads with BNDPRESERVE=1. Install the requested config only
 # after loading all four registers, then call through a preserving branch.
 # Raw bytes keep the oracle independent of assemblers that removed MPX.
 .byte 0x66, 0x0f, 0x1a, 0x06             # bndmov 0(%rsi), %bnd0
 .byte 0x66, 0x0f, 0x1a, 0x4e, 0x10       # bndmov 16(%rsi), %bnd1
 .byte 0x66, 0x0f, 0x1a, 0x56, 0x20       # bndmov 32(%rsi), %bnd2
 .byte 0x66, 0x0f, 0x1a, 0x5e, 0x30       # bndmov 48(%rsi), %bnd3
 mov %r14d, %eax
 xor %edx, %edx
 mov $0xd90, %ecx
 wrmsr
 .byte 0xf2, 0x41, 0xff, 0xd5             # bnd call *%r13
 # Restore enabled/preserving mode before observing disabled-mode state.
 mov $3, %eax
 xor %edx, %edx
 mov $0xd90, %ecx
 wrmsr
 .byte 0x66, 0x41, 0x0f, 0x1b, 0x04, 0x24             # bndmov %bnd0, 0(%r12)
 .byte 0x66, 0x41, 0x0f, 0x1b, 0x4c, 0x24, 0x10       # bndmov %bnd1, 16(%r12)
 .byte 0x66, 0x41, 0x0f, 0x1b, 0x54, 0x24, 0x20       # bndmov %bnd2, 32(%r12)
 .byte 0x66, 0x41, 0x0f, 0x1b, 0x5c, 0x24, 0x30       # bndmov %bnd3, 48(%r12)
 pop %r14
 pop %r13
 pop %r12
 ret

.global fault5, fault6, fault13, fault14
fault5:
 pushq $0
 pushq $5
 jmp trap
fault6:
 pushq $0
 pushq $6
 jmp trap
fault13:
 pushq $13
 jmp trap
fault14:
 pushq $14
trap:
 push %rax
 push %rcx
 push %rdx
 push %rbx
 push %rbp
 push %rsi
 push %rdi
 push %r8
 push %r9
 push %r10
 push %r11
 push %r12
 push %r13
 push %r14
 push %r15
 lea 120(%rsp), %rdi
 call guest_trap
 pop %r15
 pop %r14
 pop %r13
 pop %r12
 pop %r11
 pop %r10
 pop %r9
 pop %r8
 pop %rdi
 pop %rsi
 pop %rbp
 pop %rbx
 pop %rdx
 pop %rcx
 pop %rax
 add $16, %rsp
 iretq
.section .note.GNU-stack,"",@progbits
