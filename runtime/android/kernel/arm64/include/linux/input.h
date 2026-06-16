/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_INPUT_H
#define _NEVERC_KRT_LINUX_INPUT_H

#include <linux/types.h>

struct input_dev; /* opaque */

struct input_dev *input_allocate_device(void);
void input_free_device(struct input_dev *dev);
int input_register_device(struct input_dev *dev);
void input_unregister_device(struct input_dev *dev);
void input_event(struct input_dev *dev, unsigned int type,
		 unsigned int code, int value);
void input_sync(struct input_dev *dev);

/* Event types. */
#define EV_SYN 0x00
#define EV_KEY 0x01
#define EV_REL 0x02
#define EV_ABS 0x03

/* Sync codes. */
#define SYN_REPORT    0
#define SYN_MT_REPORT 2

void input_set_capability(struct input_dev *dev, unsigned int type,
			  unsigned int code);

#endif /* _NEVERC_KRT_LINUX_INPUT_H */
