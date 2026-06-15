// SPDX-License-Identifier: GPL-2.0
/*
 * gen_struct_module_offsets.c - emit exact struct module offsets for a target
 * kernel so nvkmod_version.h can load on a real device.
 *
 * WHY: the kernel module loader reads `name`, `init` and `exit` from the
 * .gnu.linkonce.this_module blob at the offsets defined by *that kernel's*
 * struct module.  Those offsets depend on CONFIG_* options (CFI_CLANG, SYSFS,
 * GENERIC_BUG, KALLSYMS, TRACEPOINTS, ...), so they must be computed against the
 * real kernel headers, not guessed.
 *
 * HOW: compile this against a *prepared* GKI tree (one that already has
 * include/generated/autoconf.h).  offsetof()/sizeof() are compile-time
 * constants for the target ABI, emitted into the assembly via the same
 * "asm-offsets" trick the kernel itself uses, so nothing has to run on-target:
 *
 *   neverc --target=aarch64-linux-android -fno-lto -nostdlibinc -std=gnu11 \
 *     -D__KERNEL__ -DNVK_GEN_KSRC=1 \
 *     -U__GNUC__ -D__GNUC__=12 -U__GNUC_MINOR__ -D__GNUC_MINOR__=0 \
 *     -U__GNUC_PATCHLEVEL__ -D__GNUC_PATCHLEVEL__=0 -Wno-unknown-attributes \
 *     -I<k>/arch/arm64/include -I<k>/arch/arm64/include/generated \
 *     -I<k>/include -I<k>/arch/arm64/include/uapi \
 *     -I<k>/arch/arm64/include/generated/uapi -I<k>/include/uapi \
 *     -I<k>/include/generated/uapi \
 *     -include <k>/include/linux/kconfig.h \
 *     -include <k>/include/generated/autoconf.h \
 *     -S -o - gen_struct_module_offsets.c | grep '==NVK=='
 *
 * Then drop the values into nvkmod_version.h (or pass them via
 * -DNVK_OFF_INIT=.. -DNVK_OFF_EXIT=.. -DNVK_MODULE_SIZE=..).
 */
#ifdef NVK_GEN_KSRC
#include <linux/module.h>
#include <linux/stddef.h>

/* asm-offsets style: function-scope asm can take operands; %0 is substituted
 * with the compile-time constant, captured from the generated assembly. */
#define NVK_EMIT(name, val)                                                    \
	__asm__ __volatile__("\n.ascii \"==NVK== " #name " %0 ==\"\n"           \
			     :                                                 \
			     : "i"((long)(val)))

void nvk_gen_offsets(void)
{
	NVK_EMIT(NVK_OFF_NAME, offsetof(struct module, name));
	NVK_EMIT(NVK_OFF_INIT, offsetof(struct module, init));
	NVK_EMIT(NVK_OFF_EXIT, offsetof(struct module, exit));
	NVK_EMIT(NVK_MODULE_SIZE, sizeof(struct module));
}
#else
const unsigned long nvk_gen_placeholder = 0; /* compile with -DNVK_GEN_KSRC=1 */
#endif
