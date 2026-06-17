/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NEVERC_KRT_HIDE_H
#define NEVERC_KRT_HIDE_H

#include <linux/types.h>
#include <nvk_rt.h>
#include <linux/compiler.h>
#include <linux/list.h>
#include <linux/kallsyms.h>
#include <nvkmod_version.h>
#include <nvk_hook.h>

typedef void *(*neverc_krt_find_module_fn)(const char *name);

struct neverc_krt_hide_state {
	struct list_head *saved_next;
	struct list_head *saved_prev;
	int               hidden;
	struct neverc_krt_hook   find_module_hook;
	neverc_krt_find_module_fn orig_find_module;
	const char       *module_name;
	int               sysfs_removed;
	int               kallsyms_filtered;
	void             *saved_kobj;
	struct neverc_krt_hook   seq_show_hook;
	int               seq_show_hooked;
};

#define NEVERC_KRT_HIDE_INIT_STATE { .saved_next = 0, .saved_prev = 0,  \
			      .hidden = 0, .module_name = 0,     \
			      .sysfs_removed = 0,                 \
			      .kallsyms_filtered = 0,             \
			      .saved_kobj = 0,                    \
			      .seq_show_hooked = 0 }

int neverc_krt_hide_init(void);

void neverc_krt_mod_hide(struct neverc_krt_hide_state *state,
			 struct neverc_krt_this_module *mod);

void neverc_krt_mod_show(struct neverc_krt_hide_state *state,
			 struct neverc_krt_this_module *mod);

static __always_inline int neverc_krt_mod_is_hidden(struct neverc_krt_hide_state *state)
{
	return state->hidden;
}

void neverc_krt_mod_sysfs_remove(struct neverc_krt_hide_state *state,
				 struct neverc_krt_this_module *mod);

int neverc_krt_mod_proc_filter(struct neverc_krt_hide_state *state,
			       const char *module_name);

int neverc_krt_mod_kallsyms_filter(struct neverc_krt_hide_state *state,
				   const char *module_name);

void neverc_krt_mod_full_hide(struct neverc_krt_hide_state *state,
			      struct neverc_krt_this_module *mod,
			      const char *module_name);

/* --- /proc/pid hiding --- */

#define NEVERC_KRT_HIDE_PID_MAX 32

struct neverc_krt_pid_hide_state {
	int              pids[NEVERC_KRT_HIDE_PID_MAX];
	int              count;
	struct neverc_krt_hook_ctx ctx_hook;
	int              active;
};

NEVERC_KRT_RT_VAR struct neverc_krt_pid_hide_state _neverc_krt_pid_state;

int _neverc_krt_pid_is_hidden(int pid);

int neverc_krt_pid_hide_add(int pid);
int neverc_krt_pid_hide_remove(int pid);
int neverc_krt_pid_hide_install(void);

static __always_inline int neverc_krt_pid_should_hide(int pid)
{
	if (!_neverc_krt_pid_state.active) return 0;
	return _neverc_krt_pid_is_hidden(pid);
}

void neverc_krt_pid_hide_cleanup(void);


/* --- /proc/mounts path filter --- */

#define NEVERC_KRT_MOUNT_FILTER_MAX 8
#define NEVERC_KRT_MOUNT_PATH_MAX   64

int neverc_krt_mount_filter_add(const char *path);
int neverc_krt_mount_filter_install(void);
void neverc_krt_mount_filter_cleanup(void);


/* --- /proc/pid/maps module region filter --- */

#define NEVERC_KRT_MAPS_FILTER_MAX 4

struct neverc_krt_maps_filter_region {
	unsigned long start;
	unsigned long end;
};

NEVERC_KRT_RT_VAR struct neverc_krt_maps_filter_region _neverc_krt_maps_regions[NEVERC_KRT_MAPS_FILTER_MAX];
NEVERC_KRT_RT_VAR int _neverc_krt_maps_region_count;

int neverc_krt_maps_filter_add(unsigned long start, unsigned long end);

static __always_inline int neverc_krt_maps_should_hide(unsigned long addr)
{
	int i;
	for (i = 0; i < _neverc_krt_maps_region_count; i++) {
		if (addr >= _neverc_krt_maps_regions[i].start &&
		    addr < _neverc_krt_maps_regions[i].end)
			return 1;
	}
	return 0;
}

void neverc_krt_maps_filter_clear(void);
void neverc_krt_maps_filter_add_self(void);

void neverc_krt_mod_wipe_modinfo(struct neverc_krt_this_module *mod);


/* --- /proc/vmallocinfo filter --- */

int neverc_krt_mod_vmalloc_filter(void);


/* --- dmesg / kmsg log suppression --- */

#define NEVERC_KRT_DMESG_FILTER_MAX 4
#define NEVERC_KRT_DMESG_FILTER_LEN 32

int neverc_krt_dmesg_filter_add(const char *keyword);
int neverc_krt_dmesg_suppress_install(const char *module_name);
void neverc_krt_dmesg_suppress_cleanup(void);

int neverc_krt_kmsg_read_filter_install(void);
void neverc_krt_kmsg_read_filter_cleanup(void);


/* --- /proc/pid/status UID spoofing --- */

int neverc_krt_proc_status_filter_install(u32 fake_uid, u32 fake_gid);
void neverc_krt_proc_status_filter_cleanup(void);


/* --- /proc/pid/attr SELinux context filter --- */

int neverc_krt_proc_attr_filter_install(const char *fake_context);
void neverc_krt_proc_attr_filter_cleanup(void);


/* --- /proc/net/tcp{,6} port hiding --- */

#define NEVERC_KRT_NET_HIDE_PORT_MAX 16

int neverc_krt_net_hide_add_port(u16 port);
int neverc_krt_net_hide_install(void);
void neverc_krt_net_hide_cleanup(void);


/* --- /proc/pid/cmdline content filter --- */

#define NEVERC_KRT_CMDLINE_FILTER_MAX 4
#define NEVERC_KRT_CMDLINE_FILTER_LEN 32

int neverc_krt_cmdline_filter_add(const char *keyword);
int neverc_krt_cmdline_filter_install(void);
void neverc_krt_cmdline_filter_cleanup(void);


/* --- File read interception (build.prop, /proc/version spoofing) --- */

#define NEVERC_KRT_FILE_SPOOF_MAX 4
#define NEVERC_KRT_FILE_PATH_MAX  64
#define NEVERC_KRT_FILE_SPOOF_MAX_LEN 128

int neverc_krt_file_spoof_add(const char *path,
			      const char *search, int slen,
			      const char *replace, int rlen);

int neverc_krt_file_spoof_install(void);
void neverc_krt_file_spoof_cleanup(void);


#endif /* NEVERC_KRT_HIDE_H */
