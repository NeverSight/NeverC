/* SPDX-License-Identifier: AGPL-3.0-only */
/* Independently authored raw instruction and PUSHFQ reference. */
.text
.globl original_0
.type original_0,@function
original_0:
  mov %rdi,%r10
  mov %rsi,%r11
  mov 0(%r10),%rax
  .byte 0xf3,0x0f,0x01,0xff
  mov %rax,0(%r10)
  pushfq
  pop %rax
  and $2260,%rax
  mov (%r11),%rdx
  and $-2261,%rdx
  or %rax,%rdx
  mov %rdx,(%r11)
  ret
.size original_0,.-original_0
.globl original_1
.type original_1,@function
original_1:
  mov %rdi,%r10
  mov %rsi,%r11
  mov 0(%r10),%rax
  mov 8(%r10),%rcx
  mov 16(%r10),%rdx
  .byte 0xf2,0x0f,0x01,0xff
  mov %rax,0(%r10)
  pushfq
  pop %rax
  and $2261,%rax
  mov (%r11),%rdx
  and $-2262,%rdx
  or %rax,%rdx
  mov %rdx,(%r11)
  ret
.size original_1,.-original_1
.globl original_2
.type original_2,@function
original_2:
  mov %rdi,%r10
  mov %rsi,%r11
  mov 0(%r10),%rax
  mov 8(%r10),%rcx
  mov 16(%r10),%rdx
  .byte 0xf3,0x0f,0x01,0xfe
  mov %rax,0(%r10)
  pushfq
  pop %rax
  and $2260,%rax
  mov (%r11),%rdx
  and $-2261,%rdx
  or %rax,%rdx
  mov %rdx,(%r11)
  ret
.size original_2,.-original_2
.globl original_3
.type original_3,@function
original_3:
  mov %rdi,%r10
  mov %rsi,%r11
  mov 0(%r10),%rax
  mov 8(%r10),%rcx
  .byte 0xf2,0x0f,0x01,0xfe
  mov %rax,0(%r10)
  pushfq
  pop %rax
  and $2260,%rax
  mov (%r11),%rdx
  and $-2261,%rdx
  or %rax,%rdx
  mov %rdx,(%r11)
  ret
.size original_3,.-original_3
.globl original_4
.type original_4,@function
original_4:
  mov %rdi,%r10
  mov %rsi,%r11
  mov 0(%r10),%rax
  mov 16(%r10),%rdx
  .byte 0xf3,0x0f,0x01,0xfd
  mov %rax,0(%r10)
  mov %rcx,8(%r10)
  mov %rdx,16(%r10)
  pushfq
  pop %rax
  and $2260,%rax
  mov (%r11),%rdx
  and $-2261,%rdx
  or %rax,%rdx
  mov %rdx,(%r11)
  ret
.size original_4,.-original_4
.globl original_5
.type original_5,@function
original_5:
  mov %rdi,%r10
  mov %rsi,%r11
  mov 0(%r10),%rax
  mov 8(%r10),%rcx
  .byte 0xf2,0x0f,0x01,0xfd
  mov %rax,0(%r10)
  pushfq
  pop %rax
  and $2260,%rax
  mov (%r11),%rdx
  and $-2261,%rdx
  or %rax,%rdx
  mov %rdx,(%r11)
  ret
.size original_5,.-original_5
.globl original_6
.type original_6,@function
original_6:
  mov %rdi,%r10
  mov %rsi,%r11
  mov 0(%r10),%rax
  mov 8(%r10),%rcx
  .byte 0xf2,0x0f,0x01,0xfc
  pushfq
  pop %rax
  and $1,%rax
  mov (%r11),%rdx
  and $-2,%rdx
  or %rax,%rdx
  mov %rdx,(%r11)
  ret
.size original_6,.-original_6
.globl original_7
.type original_7,@function
original_7:
  mov %rdi,%r10
  mov %rsi,%r11
  mov 0(%r10),%rax
  mov 8(%r10),%rcx
  .byte 0xf3,0x0f,0x01,0xfc
  mov %rax,0(%r10)
  mov %rcx,8(%r10)
  pushfq
  pop %rax
  and $2261,%rax
  mov (%r11),%rdx
  and $-2262,%rdx
  or %rax,%rdx
  mov %rdx,(%r11)
  ret
.size original_7,.-original_7
.globl original_8
.type original_8,@function
original_8:
  mov %rdi,%r10
  mov %rsi,%r11
  mov 0(%r10),%rax
  mov 8(%r10),%rcx
  mov 16(%r10),%rdx
  .byte 0x67
  .byte 0xf2,0x0f,0x01,0xff
  mov %rax,0(%r10)
  pushfq
  pop %rax
  and $2261,%rax
  mov (%r11),%rdx
  and $-2262,%rdx
  or %rax,%rdx
  mov %rdx,(%r11)
  ret
.size original_8,.-original_8
.globl original_11
.type original_11,@function
original_11:
  mov %rdi,%r10
  mov %rsi,%r11
  mov 0(%r10),%rax
  mov 8(%r10),%rcx
  .byte 0x65
  .byte 0xf2,0x0f,0x01,0xfd
  mov %rax,0(%r10)
  pushfq
  pop %rax
  and $2260,%rax
  mov (%r11),%rdx
  and $-2261,%rdx
  or %rax,%rdx
  mov %rdx,(%r11)
  ret
.size original_11,.-original_11
.globl original_9
.type original_9,@function
original_9:
  mov (%rdi),%rax
  .byte 0xf2,0x0f,0x01,0xd9
  ret
.size original_9,.-original_9
.globl original_10
.type original_10,@function
original_10:
  mov (%rdi),%rax
  .byte 0xf3,0x0f,0x01,0xd9
  ret
.size original_10,.-original_10
.section .note.GNU-stack,"",@progbits
