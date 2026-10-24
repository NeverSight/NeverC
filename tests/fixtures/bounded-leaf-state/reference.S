// Independent SysV x64 reference. POPFQ restores the six arithmetic flags;
// instruction bytes and PUSHFQ observe the original machine boundary.
.text
.globl original
.type original,@function
original:
  pushq %rbx
  pushq %r12
  movq %rsi,%r12
  movq 8(%rdi),%rbx
  movq 16(%rdi),%rcx
  movq 24(%rdi),%rdx
  movq 32(%rdi),%r8
  movq (%rdi),%rax
  movq %r8,%r10
  andq $0xd500,%r8
  shrq $8,%r8
  andq $1,%r10
  shlq $11,%r10
  orq %r10,%r8
  pushfq
  popq %r9
  andq $-2262,%r9 // ~(CF|PF|AF|ZF|SF|OF)
  orq %r8,%r9
  pushq %r9
  popfq
#ifdef NEVERC_TEST_ENCLU
  .byte 0x0f,0x01,0xd7
#else
  .byte 0x0f,0x37
#endif
  pushfq
  popq %r8
  movq %rax,(%r12)
  movq %rbx,8(%r12)
  movq %rcx,16(%r12)
  movq %rdx,24(%r12)
  movq %r8,%r9
  andq $0xd5,%r8
  shlq $8,%r8
  shrq $11,%r9
  andq $1,%r9
  orq %r9,%r8
  movq %r8,32(%r12)
  popq %r12
  popq %rbx
  ret
.size original,.-original
.section .note.GNU-stack,"",@progbits
