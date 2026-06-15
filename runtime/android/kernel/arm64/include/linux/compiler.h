/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NVK_LINUX_COMPILER_H
#define _NVK_LINUX_COMPILER_H

#ifndef __always_inline
#define __always_inline inline __attribute__((always_inline))
#endif
#define noinline __attribute__((noinline))
#define __maybe_unused __attribute__((unused))
#define __used __attribute__((used))
#define __unused __attribute__((unused))
#define __packed __attribute__((packed))
#define __aligned(n) __attribute__((aligned(n)))
#define __must_check __attribute__((warn_unused_result))
#define __printf(a, b) __attribute__((format(printf, a, b)))
#define __noreturn __attribute__((noreturn))
#define __weak __attribute__((weak))
#define __section(s) __attribute__((section(s)))

#ifndef __init
#define __init __attribute__((section(".init.text")))
#endif
#ifndef __exit
#define __exit __attribute__((section(".exit.text")))
#endif
#define __initdata __attribute__((section(".init.data")))

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#define barrier() __asm__ __volatile__("" : : : "memory")
#define READ_ONCE(x) (*(const volatile __typeof__(x) *)&(x))
#define WRITE_ONCE(x, val) (*(volatile __typeof__(x) *)&(x) = (val))

#ifndef __always_unused
#define __always_unused __attribute__((unused))
#endif

#endif /* _NVK_LINUX_COMPILER_H */
