/* SPDX-License-Identifier: GPL-2.0 */
#include <nvk_cpu.h>

#include <linux/kallsyms.h>
#include <nvk_compat.h>
#include <nvk_mem.h>

#define NEVERC_KRT_CPU_FORCE_INLINE __attribute__((always_inline))

typedef void (*neverc_krt_on_each_cpu_fn)(neverc_krt_smp_call_fn func, void *info,
					  int wait);
typedef int  (*neverc_krt_smp_call_function_fn)(neverc_krt_smp_call_fn func,
						void *info, int wait);
typedef int  (*neverc_krt_smp_call_single_fn)(int cpu, neverc_krt_smp_call_fn func,
					      void *info, int wait);

static unsigned long               *_neverc_krt_cpu_online_mask;
static unsigned int                *_neverc_krt_nr_cpu_ids_ptr;
static int                          _neverc_krt_cpu_inited;
static neverc_krt_on_each_cpu_fn    _neverc_krt_on_each_cpu;
static neverc_krt_smp_call_function_fn _neverc_krt_smp_call_function;
static neverc_krt_smp_call_single_fn _neverc_krt_smp_call_single;

static __always_inline u64 _neverc_krt_cpu_read_mpidr(void)
{
	u64 value;

	__asm__ __volatile__("mrs %0, mpidr_el1" : "=r"(value));
	return value;
}

static __always_inline u64 _neverc_krt_cpu_read_midr(void)
{
	u64 value;

	__asm__ __volatile__("mrs %0, midr_el1" : "=r"(value));
	return value;
}

static __always_inline u64 _neverc_krt_cpu_read_isar0(void)
{
	u64 value;

	__asm__ __volatile__("mrs %0, id_aa64isar0_el1" : "=r"(value));
	return value;
}

static __always_inline u64 _neverc_krt_cpu_read_isar1(void)
{
	u64 value;

	__asm__ __volatile__("mrs %0, id_aa64isar1_el1" : "=r"(value));
	return value;
}

static __always_inline u64 _neverc_krt_cpu_read_pfr0(void)
{
	u64 value;

	__asm__ __volatile__("mrs %0, id_aa64pfr0_el1" : "=r"(value));
	return value;
}

static __always_inline u64 _neverc_krt_cpu_read_pfr1(void)
{
	u64 value;

	__asm__ __volatile__("mrs %0, id_aa64pfr1_el1" : "=r"(value));
	return value;
}

static __always_inline unsigned int
_neverc_krt_cpu_feature_field(u64 value, unsigned int shift)
{
	return (unsigned int)((value >> shift) & 0xFUL);
}

NEVERC_KRT_CPU_FORCE_INLINE u32 neverc_krt_cpu_cluster(void)
{
	return (u32)((_neverc_krt_cpu_read_mpidr() >> 8) & 0xFFUL);
}

NEVERC_KRT_CPU_FORCE_INLINE u64 neverc_krt_cpu_midr(void)
{
	return _neverc_krt_cpu_read_midr();
}

NEVERC_KRT_CPU_FORCE_INLINE int neverc_krt_cpu_has_crc32(void)
{
	return _neverc_krt_cpu_feature_field(_neverc_krt_cpu_read_isar0(), 16) >= 1;
}

NEVERC_KRT_CPU_FORCE_INLINE int neverc_krt_cpu_has_sha256(void)
{
	return _neverc_krt_cpu_feature_field(_neverc_krt_cpu_read_isar0(), 12) >= 1;
}

NEVERC_KRT_CPU_FORCE_INLINE int neverc_krt_cpu_has_aes(void)
{
	return _neverc_krt_cpu_feature_field(_neverc_krt_cpu_read_isar0(), 4) >= 1;
}

NEVERC_KRT_CPU_FORCE_INLINE int neverc_krt_cpu_has_atomics(void)
{
	return _neverc_krt_cpu_feature_field(_neverc_krt_cpu_read_isar0(), 20) >= 2;
}

NEVERC_KRT_CPU_FORCE_INLINE int neverc_krt_cpu_has_sve(void)
{
	return _neverc_krt_cpu_feature_field(_neverc_krt_cpu_read_pfr0(), 32) >= 1;
}

NEVERC_KRT_CPU_FORCE_INLINE int neverc_krt_has_pac(void)
{
	u64 isar1 = _neverc_krt_cpu_read_isar1();
	unsigned int apa = _neverc_krt_cpu_feature_field(isar1, 4);
	unsigned int api = _neverc_krt_cpu_feature_field(isar1, 8);

	return (apa | api) != 0;
}

NEVERC_KRT_CPU_FORCE_INLINE int neverc_krt_has_bti(void)
{
	return _neverc_krt_cpu_feature_field(_neverc_krt_cpu_read_pfr1(), 0) != 0;
}

NEVERC_KRT_CPU_FORCE_INLINE int neverc_krt_has_mte(void)
{
	return _neverc_krt_cpu_feature_field(_neverc_krt_cpu_read_pfr1(), 8) >= 2;
}

NEVERC_KRT_CPU_FORCE_INLINE int neverc_krt_has_epac(void)
{
	u64 isar1 = _neverc_krt_cpu_read_isar1();
	unsigned int apa = _neverc_krt_cpu_feature_field(isar1, 4);
	unsigned int api = _neverc_krt_cpu_feature_field(isar1, 8);

	return apa >= 2 || api >= 2;
}

NEVERC_KRT_CPU_FORCE_INLINE int neverc_krt_has_fpac(void)
{
	u64 isar1 = _neverc_krt_cpu_read_isar1();
	unsigned int apa = _neverc_krt_cpu_feature_field(isar1, 4);
	unsigned int api = _neverc_krt_cpu_feature_field(isar1, 8);

	return apa >= 3 || api >= 3;
}

int neverc_krt_cpu_is_big_core(void)
{
	u32 part = (u32)((neverc_krt_cpu_midr() >> 4) & 0xFFF);

	/*
	 * Preserve the SDK's broad ARM big-core classification.  Consumers that
	 * require scheduler-capacity semantics should query kernel topology
	 * instead of inferring it from MIDR.
	 */
	if (part >= 0xD08 && part <= 0xD0D) return 1;
	if (part >= 0xD40) return 1;
	return 0;
}

int neverc_krt_cpu_init(void)
{
	if (_neverc_krt_cpu_inited)
		return (_neverc_krt_nr_cpu_ids_ptr &&
			_neverc_krt_cpu_online_mask) ? 0 : -1;

	_neverc_krt_nr_cpu_ids_ptr =
		(unsigned int *)NEVERC_KRT_LOOKUP("nr_cpu_ids");
	_neverc_krt_cpu_online_mask =
		(unsigned long *)NEVERC_KRT_LOOKUP("__cpu_online_mask");
	if (!_neverc_krt_cpu_online_mask)
		_neverc_krt_cpu_online_mask =
			(unsigned long *)NEVERC_KRT_LOOKUP("cpu_online_mask");

	if (!_neverc_krt_nr_cpu_ids_ptr || !_neverc_krt_cpu_online_mask)
		return -1;
	_neverc_krt_cpu_inited = 1;
	return 0;
}

int neverc_krt_num_possible_cpus(void)
{
	unsigned int nr;

	if (!_neverc_krt_cpu_inited) neverc_krt_cpu_init();
	if (_neverc_krt_nr_cpu_ids_ptr &&
	    !neverc_krt_mem_read(&nr, _neverc_krt_nr_cpu_ids_ptr, sizeof(nr)) &&
	    nr > 0 && nr <= NEVERC_KRT_MAX_CPUS)
		return (int)nr;
	return NEVERC_KRT_MAX_CPUS;
}

int neverc_krt_cpu_is_online(int cpu)
{
	int nr = neverc_krt_num_possible_cpus();

	if (cpu < 0 || cpu >= nr) return 0;
	if (_neverc_krt_cpu_online_mask) {
		int word = cpu / (int)(8 * sizeof(unsigned long));
		int bit  = cpu % (int)(8 * sizeof(unsigned long));
		unsigned long val;
		if (neverc_krt_mem_read(&val, &_neverc_krt_cpu_online_mask[word],
				       sizeof(val)))
			return 0;
		return (val >> bit) & 1;
	}
	return cpu == (int)neverc_krt_cpu_id();
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
	if (!func) return -1;
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
	if (!_neverc_krt_smp_call_single || !func ||
	    cpu < 0 || cpu >= neverc_krt_num_possible_cpus())
		return -1;
	return _neverc_krt_smp_call_single(cpu, func, info, wait);
}

void neverc_krt_detect_hw_caps(struct neverc_krt_hw_caps *caps)
{
	u64 isar1;
	u64 pfr0;
	u64 pfr1;
	unsigned int apa;
	unsigned int api;

	if (!caps) return;

	isar1 = _neverc_krt_cpu_read_isar1();
	pfr0 = _neverc_krt_cpu_read_pfr0();
	pfr1 = _neverc_krt_cpu_read_pfr1();
	apa = _neverc_krt_cpu_feature_field(isar1, 4);
	api = _neverc_krt_cpu_feature_field(isar1, 8);

	caps->pac  = (apa | api) != 0;
	caps->epac = apa >= 2 || api >= 2;
	caps->fpac = apa >= 3 || api >= 3;
	caps->bti  = _neverc_krt_cpu_feature_field(pfr1, 0) != 0;
	caps->mte  = _neverc_krt_cpu_feature_field(pfr1, 8) >= 2;
	caps->sve  = _neverc_krt_cpu_feature_field(pfr0, 32) >= 1;
	caps->cfi  = neverc_krt_has_cfi();
}

#undef NEVERC_KRT_CPU_FORCE_INLINE
