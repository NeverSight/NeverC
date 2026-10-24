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
