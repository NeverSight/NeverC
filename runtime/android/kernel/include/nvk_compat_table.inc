/* SPDX-License-Identifier: GPL-2.0 */
/*
 * nvk_compat_table.inc — GKI kernel version descriptor table.
 *
 * Included by nvk_compat.c.  Each row maps a compact kernel version code
 * (major*100 + minor) to the struct-layout constants that the runtime
 * needs for that GKI generation.
 *
 * Adding a new GKI kernel: append one row and bump
 * NEVERC_KRT_VERSION_TABLE_LEN.  No other source files need editing
 * for the basic offset / size data.
 */

struct neverc_krt_version_entry {
	int           kv;               /* major*100 + minor   */
	u32           major;
	u32           minor;
	u32           android_version;
	unsigned long module_size;      /* sizeof(struct module)         */
	unsigned long file_dentry_off;  /* offsetof(struct file, f_path.dentry) */
	unsigned long off_init;         /* offsetof(struct module, init) */
	unsigned long off_exit;         /* offsetof(struct module, exit) */
	unsigned long cred_uid_base;    /* offsetof(struct cred, uid)    */
};

#define NEVERC_KRT_VERSION_TABLE_LEN 6

static const struct neverc_krt_version_entry
_neverc_krt_version_table[NEVERC_KRT_VERSION_TABLE_LEN] = {
	/*  kv   maj min android  mod_size  dentry   init  exit  uid  */
	{  510,  5, 10,  12,      1024,     0x18,    400,  960,   4  },
	{  515,  5, 15,  13,       960,     0x18,    376,  888,   4  },
	{  601,  6,  1,  14,      1088,     0x18,    368,  984,   4  },
	{  606,  6,  6,  15,      1536,     0xB0,    392, 1464,   8  },
	{  612,  6, 12,  16,      1600,     0x48,    392, 1528,   8  },
	{  618,  6, 18,  17,      1664,     0x48,    376, 1536,   8  },
};
