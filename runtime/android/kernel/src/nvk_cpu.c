/* SPDX-License-Identifier: GPL-2.0 */
/* nvk_cpu.c — implementations extracted from nvk_cpu.h. */
#include <nvk.h>

int nvk_cpu_init(void)
{
	if (_nvk_cpu_inited) return 0;

	_nvk_nr_cpu_ids_ptr = (int *)NVK_LOOKUP("nr_cpu_ids");
	_nvk_cpu_online_mask =
		(unsigned long *)NVK_LOOKUP("__cpu_online_mask");
	if (!_nvk_cpu_online_mask)
		_nvk_cpu_online_mask =
			(unsigned long *)NVK_LOOKUP("cpu_online_mask");
	_nvk_cpu_online_check =
		(nvk_cpu_online_fn)NVK_LOOKUP("cpu_online");

	_nvk_cpu_inited = 1;
	return 0;
}

int nvk_num_possible_cpus(void)
{
	if (_nvk_nr_cpu_ids_ptr) return *_nvk_nr_cpu_ids_ptr;
	return 8;
}

int nvk_cpu_is_online(int cpu)
{
	if (_nvk_cpu_online_check)
		return _nvk_cpu_online_check((unsigned int)cpu);
	if (_nvk_cpu_online_mask) {
		int word = cpu / 64;
		int bit  = cpu % 64;
		unsigned long val;
		if (nvk_mem_read(&val, &_nvk_cpu_online_mask[word], 8))
			return 0;
		return (val >> bit) & 1;
	}
	return 1;
}

int nvk_num_online_cpus(void)
{
	int n = nvk_num_possible_cpus();
	int count = 0, i;
	for (i = 0; i < n; i++) {
		if (nvk_cpu_is_online(i))
			count++;
	}
	return count;
}


int nvk_smp_init(void)
{
	_nvk_on_each_cpu =
		(nvk_on_each_cpu_fn)NVK_LOOKUP("on_each_cpu");
	_nvk_smp_call_single =
		(nvk_smp_call_single_fn)NVK_LOOKUP("smp_call_function_single");
	return (_nvk_on_each_cpu || _nvk_smp_call_single) ? 0 : -1;
}

int nvk_smp_on_each(nvk_smp_call_fn func, void *info, int wait)
{
	if (!_nvk_on_each_cpu) return -1;
	_nvk_on_each_cpu(func, info, wait);
	return 0;
}

int nvk_smp_call_on(int cpu, nvk_smp_call_fn func,
			   void *info, int wait)
{
	if (!_nvk_smp_call_single) return -1;
	return _nvk_smp_call_single(cpu, func, info, wait);
}

