# Independent Hyper-V page ABI wrapper; no compiler intrinsic.
# SPDX-License-Identifier: AGPL-3.0-only
.text
.globl original_hyperv
.type original_hyperv,@function
original_hyperv:
 push %r12
 push %r13
 push %r14
 mov %rdi,%r12
 mov %rsi,%r13
 mov %rdx,%r14
 mov 8(%r13),%rcx
 mov 16(%r13),%rdx
 mov 64(%r13),%r8
 movups 0(%r14),%xmm0
 movups 64(%r14),%xmm1
 movups 128(%r14),%xmm2
 movups 192(%r14),%xmm3
 movups 256(%r14),%xmm4
 movups 320(%r14),%xmm5
 call *%r12
 mov %rax,0(%r13)
 mov %rcx,8(%r13)
 mov %rdx,16(%r13)
 mov %r8,64(%r13)
 movups %xmm0,0(%r14)
 movups %xmm1,64(%r14)
 movups %xmm2,128(%r14)
 movups %xmm3,192(%r14)
 movups %xmm4,256(%r14)
 movups %xmm5,320(%r14)
 pop %r14
 pop %r13
 pop %r12
 ret
.size original_hyperv,.-original_hyperv
.section .note.GNU-stack,"",@progbits
