/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NVK_LINUX_REGMAP_H
#define _NVK_LINUX_REGMAP_H

#include <linux/types.h>

struct regmap; /* opaque */
struct device;

struct regmap_config {
	int reg_bits;
	int val_bits;
	int reg_stride;
	unsigned int max_register;
	bool fast_io;
	const void *reg_defaults;
	unsigned int num_reg_defaults;
	unsigned char __opaque[64];
};

struct regmap *devm_regmap_init_mmio(struct device *dev, void *regs,
				     const struct regmap_config *config);
int regmap_read(struct regmap *map, unsigned int reg, unsigned int *val);
int regmap_write(struct regmap *map, unsigned int reg, unsigned int val);
int regmap_update_bits(struct regmap *map, unsigned int reg,
		       unsigned int mask, unsigned int val);

#endif /* _NVK_LINUX_REGMAP_H */
