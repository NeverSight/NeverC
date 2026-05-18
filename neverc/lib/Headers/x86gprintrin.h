#ifndef __X86GPRINTRIN_H
#define __X86GPRINTRIN_H

#if !(defined(_MSC_VER) || defined(__SCE__)) || __has_feature(modules) ||      \
    defined(__CRC32__)
#include <crc32intrin.h>
#endif

#if !(defined(_MSC_VER) || defined(__SCE__)) || __has_feature(modules) ||      \
    defined(__PRFCHI__)
#include <prfchiintrin.h>
#endif

// On x86_64 the 32-bit operand "mov x %ebx" zero-extends and clobbers the
// upper half of rbx, so we save/restore the full 64-bit register.
#define __SAVE_GPRBX "mov {%%rbx, %%rax |rax, rbx};"
#define __RESTORE_GPRBX "mov {%%rax, %%rbx |rbx, rax};"
#define __TMPGPR "rax"

#define __SSC_MARK(__Tag)                                                      \
  __asm__ __volatile__(__SAVE_GPRBX                                            \
                       "mov {%0, %%ebx|ebx, %0}; "                             \
                       ".byte 0x64, 0x67, 0x90; " __RESTORE_GPRBX ::"i"(__Tag) \
                       : __TMPGPR);

#endif /* __X86GPRINTRIN_H */
