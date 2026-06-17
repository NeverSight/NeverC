/* SPDX-License-Identifier: GPL-2.0 */
/* neverc_krt_syscall.c — implementations extracted from neverc_krt_syscall.h. */
#include <nvk.h>

int neverc_krt_syscall_init(void)
{
	if (_neverc_krt_syscall_inited) return 0;

	if (!_neverc_krt_mem_inited)
		_neverc_krt_mem_init();

	_neverc_krt_sys_call_table =
		(neverc_krt_syscall_fn_t *)NEVERC_KRT_LOOKUP("sys_call_table");
	if (!_neverc_krt_sys_call_table)
		return -1;

	_neverc_krt_syscall_inited = 1;
	return 0;
}

int neverc_krt_syscall_replace(int nr, neverc_krt_syscall_fn_t new_fn,
			       neverc_krt_syscall_fn_t *orig)
{
	unsigned long entry;

	if (!_neverc_krt_sys_call_table) return -1;
	if (nr < 0) return -1;

	*orig = _neverc_krt_sys_call_table[nr];
	entry = (unsigned long)&_neverc_krt_sys_call_table[nr];

	if (neverc_krt_mem_make_rw(entry) < 0)
		return -2;

	_neverc_krt_sys_call_table[nr] = new_fn;
	neverc_krt_mem_make_ro(entry);

	__asm__ __volatile__("dsb ish" ::: "memory");
	return 0;
}

int neverc_krt_syscall_restore(int nr, neverc_krt_syscall_fn_t orig)
{
	unsigned long entry;

	if (!_neverc_krt_sys_call_table || !orig) return -1;
	if (nr < 0) return -1;

	entry = (unsigned long)&_neverc_krt_sys_call_table[nr];

	if (neverc_krt_mem_make_rw(entry) < 0)
		return -2;

	_neverc_krt_sys_call_table[nr] = orig;
	neverc_krt_mem_make_ro(entry);

	__asm__ __volatile__("dsb ish" ::: "memory");
	return 0;
}

