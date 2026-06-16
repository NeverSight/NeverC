/* SPDX-License-Identifier: GPL-2.0 */
/* nvk_syscall.c — implementations extracted from nvk_syscall.h. */
#include <nvk.h>

int nvk_syscall_init(void)
{
	if (_nvk_syscall_inited) return 0;

	if (!_nvk_mem_inited)
		nvk_mem_init();

	_nvk_sys_call_table =
		(nvk_syscall_fn_t *)NVK_LOOKUP("sys_call_table");
	if (!_nvk_sys_call_table)
		return -1;

	_nvk_syscall_inited = 1;
	return 0;
}

int nvk_syscall_replace(int nr, nvk_syscall_fn_t new_fn,
			       nvk_syscall_fn_t *orig)
{
	unsigned long entry;

	if (!_nvk_sys_call_table) return -1;
	if (nr < 0) return -1;

	*orig = _nvk_sys_call_table[nr];
	entry = (unsigned long)&_nvk_sys_call_table[nr];

	if (nvk_mem_make_rw(entry) < 0)
		return -2;

	_nvk_sys_call_table[nr] = new_fn;
	nvk_mem_make_ro(entry);

	__asm__ __volatile__("dsb ish" ::: "memory");
	return 0;
}

int nvk_syscall_restore(int nr, nvk_syscall_fn_t orig)
{
	unsigned long entry;

	if (!_nvk_sys_call_table || !orig) return -1;
	if (nr < 0) return -1;

	entry = (unsigned long)&_nvk_sys_call_table[nr];

	if (nvk_mem_make_rw(entry) < 0)
		return -2;

	_nvk_sys_call_table[nr] = orig;
	nvk_mem_make_ro(entry);

	__asm__ __volatile__("dsb ish" ::: "memory");
	return 0;
}

