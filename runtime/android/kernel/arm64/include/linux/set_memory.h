/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NVK_LINUX_SET_MEMORY_H
#define _NVK_LINUX_SET_MEMORY_H

int set_memory_ro(unsigned long addr, int numpages);
int set_memory_rw(unsigned long addr, int numpages);
int set_memory_nx(unsigned long addr, int numpages);
int set_memory_x(unsigned long addr, int numpages);

#endif
