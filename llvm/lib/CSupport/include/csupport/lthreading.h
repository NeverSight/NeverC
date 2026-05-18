#ifndef CSUPPORT_LTHREADING_H
#define CSUPPORT_LTHREADING_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

int csupport_get_physical_cores(void);
uint64_t csupport_get_thread_id(void);
uint32_t csupport_get_max_thread_name_length(void);
int csupport_set_thread_name_cstr(const char *name);
int csupport_get_thread_name_buf(char *buf, size_t buflen);
int csupport_set_thread_priority_val(int priority);
int csupport_compute_host_num_hardware_threads(void);
unsigned csupport_get_cpus(void);
void csupport_apply_thread_strategy_noop(unsigned thread_pool_num);

typedef void *(*csupport_thread_func_t)(void *);
uint64_t csupport_thread_execute(csupport_thread_func_t func, void *arg,
                                 unsigned stack_size);
void csupport_thread_detach(uint64_t thread);
void csupport_thread_join(uint64_t thread);
uint64_t csupport_thread_get_id(uint64_t thread);
uint64_t csupport_thread_get_current_id(void);

#ifdef __cplusplus
}
#endif
#endif
