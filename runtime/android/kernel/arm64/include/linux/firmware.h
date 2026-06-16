/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_FIRMWARE_H
#define _NEVERC_KRT_LINUX_FIRMWARE_H

#include <linux/types.h>

struct firmware {
	size_t size;
	const u8 *data;
	void *priv;
};

struct device;

int request_firmware(const struct firmware **fw, const char *name,
		     struct device *device);
int request_firmware_nowait(struct module *module, bool uevent,
			    const char *name, struct device *device,
			    gfp_t gfp, void *context,
			    void (*cont)(const struct firmware *fw, void *ctx));
void release_firmware(const struct firmware *fw);

#endif /* _NEVERC_KRT_LINUX_FIRMWARE_H */
