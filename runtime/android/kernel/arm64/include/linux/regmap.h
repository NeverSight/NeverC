/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_REGMAP_H
#define _NEVERC_KRT_LINUX_REGMAP_H

#include <linux/types.h>
#include <linux/compiler.h>
#include <nvkmod_version.h>

struct regmap; /* opaque */
struct device;

/*
 * Minimal source-compatible view of struct regmap_config.  The kernel consumes
 * this object by field offset, and the upstream layout changed in 5.15, 6.1,
 * and 6.12.  android14-5.15 dropped the 8-byte reg_update_bits slot that
 * android13-5.15 inserted before fast_io, so that KMI matches 5.10 again.
 * Keep only the commonly useful fields named; zero-filled opaque ranges
 * represent fields that drivers using this SDK do not initialize.
 */
struct regmap_config {
#if NEVERC_KRT_LINUX_IS_SERIES(5, 10) || \
    (NEVERC_KRT_LINUX_IS_SERIES(5, 15) && \
     NEVERC_KRT_PROFILE_ANDROID_RELEASE >= 14)
	const char *name;
	int reg_bits;
	int reg_stride;
	u8 __register_layout[4];
	int val_bits;
	u8 __before_fast_io[96];
	bool fast_io;
	u8 __before_max_register[3];
	unsigned int max_register;
	u8 __before_reg_defaults[48];
	const void *reg_defaults;
	unsigned int num_reg_defaults;
	u8 __tail[92];
#elif NEVERC_KRT_LINUX_IS_SERIES(5, 15)
	const char *name;
	int reg_bits;
	int reg_stride;
	u8 __register_layout[4];
	int val_bits;
	u8 __before_fast_io[104];
	bool fast_io;
	u8 __before_max_register[3];
	unsigned int max_register;
	u8 __before_reg_defaults[48];
	const void *reg_defaults;
	unsigned int num_reg_defaults;
	u8 __tail[92];
#elif NEVERC_KRT_LINUX_IS_SERIES(6, 1) || NEVERC_KRT_LINUX_IS_SERIES(6, 6)
	const char *name;
	int reg_bits;
	int reg_stride;
	u8 __register_layout[12];
	int val_bits;
	u8 __before_fast_io[136];
	bool fast_io;
	u8 __before_max_register[3];
	unsigned int max_register;
	u8 __before_reg_defaults[48];
	const void *reg_defaults;
	unsigned int num_reg_defaults;
	u8 __tail[92];
#else /* Android 16 / 6.12 and Android 17 / 6.18 */
	const char *name;
	int reg_bits;
	int reg_stride;
	u8 __register_layout[12];
	int val_bits;
	u8 __before_fast_io[105];
	bool fast_io;
	u8 __before_max_register[30];
	unsigned int max_register;
	u8 __before_reg_defaults[52];
	const void *reg_defaults;
	unsigned int num_reg_defaults;
	u8 __tail[84];
#endif
};

#if NEVERC_KRT_LINUX_IS_SERIES(5, 10) || \
    (NEVERC_KRT_LINUX_IS_SERIES(5, 15) && \
     NEVERC_KRT_PROFILE_ANDROID_RELEASE >= 14)
#define __NVK_REGMAP_SIZE             280
#define __NVK_REGMAP_VAL_BITS_OFF     20
#define __NVK_REGMAP_FAST_IO_OFF      120
#define __NVK_REGMAP_MAX_REGISTER_OFF 124
#define __NVK_REGMAP_DEFAULTS_OFF     176
#define __NVK_REGMAP_NUM_DEFAULTS_OFF 184
#elif NEVERC_KRT_LINUX_IS_SERIES(5, 15)
#define __NVK_REGMAP_SIZE             288
#define __NVK_REGMAP_VAL_BITS_OFF     20
#define __NVK_REGMAP_FAST_IO_OFF      128
#define __NVK_REGMAP_MAX_REGISTER_OFF 132
#define __NVK_REGMAP_DEFAULTS_OFF     184
#define __NVK_REGMAP_NUM_DEFAULTS_OFF 192
#elif NEVERC_KRT_LINUX_IS_SERIES(6, 1) || NEVERC_KRT_LINUX_IS_SERIES(6, 6)
#define __NVK_REGMAP_SIZE             328
#define __NVK_REGMAP_VAL_BITS_OFF     28
#define __NVK_REGMAP_FAST_IO_OFF      168
#define __NVK_REGMAP_MAX_REGISTER_OFF 172
#define __NVK_REGMAP_DEFAULTS_OFF     224
#define __NVK_REGMAP_NUM_DEFAULTS_OFF 232
#else
#define __NVK_REGMAP_SIZE             320
#define __NVK_REGMAP_VAL_BITS_OFF     28
#define __NVK_REGMAP_FAST_IO_OFF      137
#define __NVK_REGMAP_MAX_REGISTER_OFF 168
#define __NVK_REGMAP_DEFAULTS_OFF     224
#define __NVK_REGMAP_NUM_DEFAULTS_OFF 232
#endif

_Static_assert(sizeof(struct regmap_config) == __NVK_REGMAP_SIZE,
	       "unexpected GKI regmap_config size");
_Static_assert(__builtin_offsetof(struct regmap_config, name) == 0,
	       "unexpected GKI regmap_config name offset");
_Static_assert(__builtin_offsetof(struct regmap_config, reg_bits) == 8,
	       "unexpected GKI regmap_config reg_bits offset");
_Static_assert(__builtin_offsetof(struct regmap_config, reg_stride) == 12,
	       "unexpected GKI regmap_config reg_stride offset");
_Static_assert(__builtin_offsetof(struct regmap_config, val_bits) ==
		       __NVK_REGMAP_VAL_BITS_OFF,
	       "unexpected GKI regmap_config val_bits offset");
_Static_assert(__builtin_offsetof(struct regmap_config, fast_io) ==
		       __NVK_REGMAP_FAST_IO_OFF,
	       "unexpected GKI regmap_config fast_io offset");
_Static_assert(__builtin_offsetof(struct regmap_config, max_register) ==
		       __NVK_REGMAP_MAX_REGISTER_OFF,
	       "unexpected GKI regmap_config max_register offset");
_Static_assert(__builtin_offsetof(struct regmap_config, reg_defaults) ==
		       __NVK_REGMAP_DEFAULTS_OFF,
	       "unexpected GKI regmap_config reg_defaults offset");
_Static_assert(__builtin_offsetof(struct regmap_config, num_reg_defaults) ==
		       __NVK_REGMAP_NUM_DEFAULTS_OFF,
	       "unexpected GKI regmap_config num_reg_defaults offset");

#undef __NVK_REGMAP_SIZE
#undef __NVK_REGMAP_VAL_BITS_OFF
#undef __NVK_REGMAP_FAST_IO_OFF
#undef __NVK_REGMAP_MAX_REGISTER_OFF
#undef __NVK_REGMAP_DEFAULTS_OFF
#undef __NVK_REGMAP_NUM_DEFAULTS_OFF

struct lock_class_key;
struct regmap *__devm_regmap_init_mmio_clk(
	struct device *dev, const char *clk_id, void *regs,
	const struct regmap_config *config, struct lock_class_key *lock_key,
	const char *lock_name);

#define devm_regmap_init_mmio(dev, regs, config)                              \
	__devm_regmap_init_mmio_clk((dev), (const char *)0, (regs),           \
				     (config),                                \
				     (struct lock_class_key *)0, #config)

int regmap_read(struct regmap *map, unsigned int reg, unsigned int *val);
int regmap_write(struct regmap *map, unsigned int reg, unsigned int val);
int regmap_update_bits_base(struct regmap *map, unsigned int reg,
			    unsigned int mask, unsigned int val,
			    bool *change, bool async, bool force);
static __always_inline int
regmap_update_bits(struct regmap *map, unsigned int reg,
		   unsigned int mask, unsigned int val)
{
	return regmap_update_bits_base(map, reg, mask, val,
				       (bool *)0, false, false);
}

#endif /* _NEVERC_KRT_LINUX_REGMAP_H */
