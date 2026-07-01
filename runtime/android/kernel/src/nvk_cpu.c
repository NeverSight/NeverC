/* SPDX-License-Identifier: GPL-2.0 */
#include <nvk.h>

typedef int  (*neverc_krt_nr_cpu_ids_fn)(void);
typedef int  (*neverc_krt_cpu_online_fn)(unsigned int cpu);
typedef void (*neverc_krt_on_each_cpu_fn)(neverc_krt_smp_call_fn func, void *info,
					  int wait);
typedef int  (*neverc_krt_smp_call_function_fn)(neverc_krt_smp_call_fn func,
						void *info, int wait);
typedef int  (*neverc_krt_smp_call_single_fn)(int cpu, neverc_krt_smp_call_fn func,
					      void *info, int wait);

static neverc_krt_nr_cpu_ids_fn     _neverc_krt_nr_cpus;
static neverc_krt_cpu_online_fn     _neverc_krt_cpu_online_check;
static unsigned long               *_neverc_krt_cpu_online_mask;
static int                         *_neverc_krt_nr_cpu_ids_ptr;
static int                          _neverc_krt_cpu_inited;
static neverc_krt_on_each_cpu_fn    _neverc_krt_on_each_cpu;
static neverc_krt_smp_call_function_fn _neverc_krt_smp_call_function;
static neverc_krt_smp_call_single_fn _neverc_krt_smp_call_single;

int neverc_krt_cpu_init(void)
{
	if (_neverc_krt_cpu_inited) return 0;

	_neverc_krt_nr_cpu_ids_ptr = (int *)NEVERC_KRT_LOOKUP("nr_cpu_ids");
	_neverc_krt_cpu_online_mask =
		(unsigned long *)NEVERC_KRT_LOOKUP("__cpu_online_mask");
	if (!_neverc_krt_cpu_online_mask)
		_neverc_krt_cpu_online_mask =
			(unsigned long *)NEVERC_KRT_LOOKUP("cpu_online_mask");
	_neverc_krt_cpu_online_check =
		(neverc_krt_cpu_online_fn)NEVERC_KRT_LOOKUP("cpu_online");

	_neverc_krt_cpu_inited = 1;
	return 0;
}

int neverc_krt_num_possible_cpus(void)
{
	if (_neverc_krt_nr_cpu_ids_ptr) return *_neverc_krt_nr_cpu_ids_ptr;
	return 8;
}

int neverc_krt_cpu_is_online(int cpu)
{
	if (_neverc_krt_cpu_online_check)
		return _neverc_krt_cpu_online_check((unsigned int)cpu);
	if (_neverc_krt_cpu_online_mask) {
		int word = cpu / 64;
		int bit  = cpu % 64;
		unsigned long val;
		if (neverc_krt_mem_read(&val, &_neverc_krt_cpu_online_mask[word], 8))
			return 0;
		return (val >> bit) & 1;
	}
	return 1;
}

int neverc_krt_num_online_cpus(void)
{
	int n = neverc_krt_num_possible_cpus();
	int count = 0, i;
	for (i = 0; i < n; i++) {
		if (neverc_krt_cpu_is_online(i))
			count++;
	}
	return count;
}


int neverc_krt_smp_init(void)
{
	_neverc_krt_on_each_cpu =
		(neverc_krt_on_each_cpu_fn)NEVERC_KRT_LOOKUP("on_each_cpu");
	if (!_neverc_krt_on_each_cpu)
		_neverc_krt_smp_call_function =
			(neverc_krt_smp_call_function_fn)NEVERC_KRT_LOOKUP(
				"smp_call_function");
	_neverc_krt_smp_call_single =
		(neverc_krt_smp_call_single_fn)NEVERC_KRT_LOOKUP("smp_call_function_single");
	return (_neverc_krt_on_each_cpu || _neverc_krt_smp_call_function ||
		_neverc_krt_smp_call_single) ? 0 : -1;
}

int neverc_krt_smp_on_each(neverc_krt_smp_call_fn func, void *info, int wait)
{
	if (_neverc_krt_on_each_cpu) {
		_neverc_krt_on_each_cpu(func, info, wait);
		return 0;
	}
	if (_neverc_krt_smp_call_function) {
		_neverc_krt_smp_call_function(func, info, wait);
		func(info);
		return 0;
	}
	return -1;
}

int neverc_krt_smp_call_on(int cpu, neverc_krt_smp_call_fn func,
			   void *info, int wait)
{
	if (!_neverc_krt_smp_call_single) return -1;
	return _neverc_krt_smp_call_single(cpu, func, info, wait);
}

void neverc_krt_detect_hw_caps(struct neverc_krt_hw_caps *caps)
{
	if (!caps) return;
	caps->pac  = neverc_krt_has_pac();
	caps->epac = neverc_krt_has_epac();
	caps->fpac = neverc_krt_has_fpac();
	caps->bti  = neverc_krt_has_bti();
	caps->mte  = neverc_krt_has_mte();
	caps->sve  = neverc_krt_has_sve();
	caps->cfi  = neverc_krt_has_cfi();
}

