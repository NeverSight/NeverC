/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_SET_MEMORY_H
#define _NEVERC_KRT_LINUX_SET_MEMORY_H

/*
 * These architecture helpers are not part of the official GKI KMI.
 * Runtime code must resolve them dynamically through NEVERC_KRT_LOOKUP.
 */
#ifdef NEVERC_KRT_NON_KMI_API
int set_memory_ro(unsigned long addr, int numpages);
int set_memory_rw(unsigned long addr, int numpages);
int set_memory_nx(unsigned long addr, int numpages);
int set_memory_x(unsigned long addr, int numpages);
#endif

#endif
