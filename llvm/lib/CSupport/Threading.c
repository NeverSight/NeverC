/*===- Threading.c - Thread utilities (pure C) ------------------*- C -*-===*/
#include "include/csupport/lthreading.h"
#include "llvm/Config/config.h"
#include "llvm/Config/llvm-config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(HAVE_PTHREAD_H)
#include <pthread.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <mach/mach_init.h>
#include <mach/mach_port.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#endif

#if defined(__FreeBSD__) || defined(__OpenBSD__)
#include <pthread_np.h>
#endif

#if defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
#include <errno.h>
#include <sys/cpuset.h>
#include <sys/sysctl.h>
#include <sys/user.h>
#endif

#if defined(__NetBSD__)
#include <lwp.h>
#endif

#if defined(__linux__)
#include <sched.h>
#include <sys/syscall.h>
#endif

static void report_errno_fatal(const char *msg, int errnum) {
  fprintf(stderr, "LLVM ERROR: %s: %s\n", msg, strerror(errnum));
  abort();
}

uint64_t csupport_thread_execute(csupport_thread_func_t func, void *arg,
                                 unsigned stack_size) {
  int errnum;
  pthread_attr_t attr;
  if ((errnum = pthread_attr_init(&attr)) != 0)
    report_errno_fatal("pthread_attr_init failed", errnum);

  if (stack_size > 0) {
    if ((errnum = pthread_attr_setstacksize(&attr, stack_size)) != 0) {
      pthread_attr_destroy(&attr);
      report_errno_fatal("pthread_attr_setstacksize failed", errnum);
    }
  }

  pthread_t thread;
  if ((errnum = pthread_create(&thread, &attr, func, arg)) != 0) {
    pthread_attr_destroy(&attr);
    report_errno_fatal("pthread_create failed", errnum);
  }

  if ((errnum = pthread_attr_destroy(&attr)) != 0)
    report_errno_fatal("pthread_attr_destroy failed", errnum);

  return (uint64_t)(uintptr_t)thread;
}

void csupport_thread_detach(uint64_t thread) {
  int errnum;
  if ((errnum = pthread_detach((pthread_t)(uintptr_t)thread)) != 0)
    report_errno_fatal("pthread_detach failed", errnum);
}

void csupport_thread_join(uint64_t thread) {
  int errnum;
  if ((errnum = pthread_join((pthread_t)(uintptr_t)thread, NULL)) != 0)
    report_errno_fatal("pthread_join failed", errnum);
}

uint64_t csupport_thread_get_id(uint64_t thread) {
  return thread;
}

uint64_t csupport_thread_get_current_id(void) {
  return (uint64_t)(uintptr_t)pthread_self();
}

uint64_t csupport_get_thread_id(void) {
#if defined(__APPLE__)
  thread_port_t self = mach_thread_self();
  mach_port_deallocate(mach_task_self(), self);
  return (uint64_t)self;
#elif defined(__FreeBSD__)
  return (uint64_t)pthread_getthreadid_np();
#elif defined(__NetBSD__)
  return (uint64_t)_lwp_self();
#elif defined(__OpenBSD__)
  return (uint64_t)getthrid();
#elif defined(__ANDROID__)
  return (uint64_t)gettid();
#elif defined(__linux__)
  return (uint64_t)syscall(SYS_gettid);
#else
  return (uint64_t)(uintptr_t)pthread_self();
#endif
}

uint32_t csupport_get_max_thread_name_length(void) {
#if defined(__NetBSD__)
  return PTHREAD_MAX_NAMELEN_NP;
#elif defined(__APPLE__)
  return 64;
#elif defined(__linux__)
#if HAVE_PTHREAD_SETNAME_NP
  return 16;
#else
  return 0;
#endif
#elif defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
  return 16;
#elif defined(__OpenBSD__)
  return 32;
#else
  return 0;
#endif
}

int csupport_set_thread_name_cstr(const char *name) {
  if (!name) return -1;
  const char *use_name = name;
  uint32_t max_len = csupport_get_max_thread_name_length();
  if (max_len > 0) {
    size_t len = strlen(name);
    if (len >= max_len)
      use_name = name + len - (max_len - 1);
  }
#if defined(__linux__)
#if (defined(__GLIBC__) && defined(_GNU_SOURCE)) || defined(__ANDROID__)
#if HAVE_PTHREAD_SETNAME_NP
  return pthread_setname_np(pthread_self(), use_name);
#endif
#endif
#elif defined(__FreeBSD__) || defined(__OpenBSD__)
  pthread_set_name_np(pthread_self(), use_name);
  return 0;
#elif defined(__NetBSD__)
  return pthread_setname_np(pthread_self(), "%s", (void *)(uintptr_t)use_name);
#elif defined(__APPLE__)
  return pthread_setname_np(use_name);
#endif
  return -1;
}

int csupport_get_thread_name_buf(char *buf, size_t buflen) {
  if (!buf || buflen == 0) return -1;
  buf[0] = '\0';

#if defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
  int pid = getpid();
  uint64_t tid = csupport_get_thread_id();
  struct kinfo_proc *kp = NULL, *nkp;
  size_t len = 0;
  int error;
  int ctl[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PID | KERN_PROC_INC_THREAD,
                (int)pid};
  for (;;) {
    error = sysctl(ctl, 4, kp, &len, NULL, 0);
    if (kp == NULL || (error != 0 && errno == ENOMEM)) {
      len += sizeof(*kp) + len / 10;
      nkp = (struct kinfo_proc *)realloc(kp, len);
      if (nkp == NULL) { free(kp); return -1; }
      kp = nkp;
      continue;
    }
    if (error != 0) len = 0;
    break;
  }
  for (size_t i = 0; i < len / sizeof(*kp); i++) {
    if (kp[i].ki_tid == (lwpid_t)tid) {
      size_t nlen = strlen(kp[i].ki_tdname);
      if (nlen >= buflen) nlen = buflen - 1;
      memcpy(buf, kp[i].ki_tdname, nlen);
      buf[nlen] = '\0';
      free(kp);
      return (int)nlen;
    }
  }
  free(kp);
  return 0;
#elif defined(__NetBSD__)
  uint32_t maxlen = csupport_get_max_thread_name_length();
  char tmp[256];
  if (maxlen > sizeof(tmp)) maxlen = sizeof(tmp);
  if (pthread_getname_np(pthread_self(), tmp, maxlen) == 0) {
    size_t nlen = strlen(tmp);
    if (nlen >= buflen) nlen = buflen - 1;
    memcpy(buf, tmp, nlen);
    buf[nlen] = '\0';
    return (int)nlen;
  }
  return 0;
#elif defined(__OpenBSD__)
  uint32_t maxlen = csupport_get_max_thread_name_length();
  char tmp[256];
  if (maxlen > sizeof(tmp)) maxlen = sizeof(tmp);
  pthread_get_name_np(pthread_self(), tmp, maxlen);
  size_t nlen = strlen(tmp);
  if (nlen >= buflen) nlen = buflen - 1;
  memcpy(buf, tmp, nlen);
  buf[nlen] = '\0';
  return (int)nlen;
#elif defined(__linux__)
#if HAVE_PTHREAD_GETNAME_NP
  uint32_t maxlen = csupport_get_max_thread_name_length();
  char tmp[256];
  memset(tmp, 0, sizeof(tmp));
  if (maxlen > sizeof(tmp)) maxlen = sizeof(tmp);
  if (pthread_getname_np(pthread_self(), tmp, maxlen) == 0) {
    size_t nlen = strlen(tmp);
    if (nlen >= buflen) nlen = buflen - 1;
    memcpy(buf, tmp, nlen);
    buf[nlen] = '\0';
    return (int)nlen;
  }
#endif
  return 0;
#else
  return 0;
#endif
}

int csupport_set_thread_priority_val(int priority) {
#if defined(__linux__) && defined(SCHED_IDLE)
  struct sched_param param;
  param.sched_priority = 0;
  int policy = (priority == 2) ? SCHED_OTHER : SCHED_IDLE;
  return pthread_setschedparam(pthread_self(), policy, &param) == 0 ? 0 : -1;
#elif defined(__APPLE__)
  int qos;
  switch (priority) {
  case 0: qos = QOS_CLASS_BACKGROUND; break;
  case 1: qos = QOS_CLASS_UTILITY; break;
  default: qos = QOS_CLASS_DEFAULT; break;
  }
  return pthread_set_qos_class_self_np(qos, 0) == 0 ? 0 : -1;
#else
  (void)priority;
  return -1;
#endif
}

int csupport_compute_host_num_hardware_threads(void) {
#if defined(__FreeBSD__)
  cpuset_t mask;
  CPU_ZERO(&mask);
  if (cpuset_getaffinity(CPU_LEVEL_WHICH, CPU_WHICH_TID, -1, sizeof(mask),
                         &mask) == 0)
    return CPU_COUNT(&mask);
#elif defined(__linux__)
  cpu_set_t set;
  if (sched_getaffinity(0, sizeof(set), &set) == 0)
    return CPU_COUNT(&set);
#elif defined(__APPLE__)
  uint32_t count;
  size_t len = sizeof(count);
  if (sysctlbyname("hw.logicalcpu", &count, &len, NULL, 0) == 0 && count > 0)
    return (int)count;
  if (sysctlbyname("hw.ncpu", &count, &len, NULL, 0) == 0 && count > 0)
    return (int)count;
#endif
#ifdef _SC_NPROCESSORS_ONLN
  {
    long val = sysconf(_SC_NPROCESSORS_ONLN);
    if (val > 0) return (int)val;
  }
#endif
  return 1;
}

void csupport_apply_thread_strategy_noop(unsigned thread_pool_num) {
  (void)thread_pool_num;
}

unsigned csupport_get_cpus(void) { return 1; }

#if defined(__linux__) && defined(__x86_64__)
static int compute_physical_cores_linux_x86(void) {
  cpu_set_t affinity, enabled;
  if (sched_getaffinity(0, sizeof(affinity), &affinity) != 0)
    return -1;
  CPU_ZERO(&enabled);

  FILE *f = fopen("/proc/cpuinfo", "r");
  if (!f) return -1;

  char line[512];
  int cur_processor = -1, cur_physical_id = -1;
  int cur_siblings = -1, cur_core_id = -1;

  while (fgets(line, sizeof(line), f)) {
    char *colon = strchr(line, ':');
    if (!colon) continue;
    *colon = '\0';
    char *key = line;
    char *val = colon + 1;
    while (*key == ' ' || *key == '\t') key++;
    size_t klen = strlen(key);
    while (klen > 0 && (key[klen-1] == ' ' || key[klen-1] == '\t'))
      key[--klen] = '\0';
    while (*val == ' ' || *val == '\t') val++;
    size_t vlen = strlen(val);
    while (vlen > 0 && (val[vlen-1] == '\n' || val[vlen-1] == '\r' ||
                        val[vlen-1] == ' '))
      val[--vlen] = '\0';

    if (strcmp(key, "processor") == 0)
      cur_processor = atoi(val);
    else if (strcmp(key, "physical id") == 0)
      cur_physical_id = atoi(val);
    else if (strcmp(key, "siblings") == 0)
      cur_siblings = atoi(val);
    else if (strcmp(key, "core id") == 0) {
      cur_core_id = atoi(val);
      if (cur_processor >= 0 && cur_processor < CPU_SETSIZE &&
          CPU_ISSET(cur_processor, &affinity) &&
          cur_physical_id >= 0 && cur_siblings > 0 && cur_core_id >= 0) {
        int idx = cur_physical_id * cur_siblings + cur_core_id;
        if (idx >= 0 && idx < CPU_SETSIZE)
          CPU_SET(idx, &enabled);
      }
    }
  }
  fclose(f);
  return CPU_COUNT(&enabled);
}
#endif

static int compute_host_num_physical_cores(void) {
#if defined(__linux__) && defined(__x86_64__)
  return compute_physical_cores_linux_x86();
#elif (defined(__linux__) && defined(__s390x__)) || defined(_AIX)
  return sysconf(_SC_NPROCESSORS_ONLN);
#elif defined(__linux__) && !defined(__ANDROID__)
  cpu_set_t affinity;
  if (sched_getaffinity(0, sizeof(affinity), &affinity) == 0)
    return CPU_COUNT(&affinity);
  cpu_set_t *dyn = CPU_ALLOC(2048);
  if (dyn && sched_getaffinity(0, CPU_ALLOC_SIZE(2048), dyn) == 0) {
    int n = CPU_COUNT(dyn);
    CPU_FREE(dyn);
    return n;
  }
  if (dyn) CPU_FREE(dyn);
  return -1;
#elif defined(__APPLE__)
  uint32_t count;
  size_t len = sizeof(count);
  sysctlbyname("hw.physicalcpu", &count, &len, NULL, 0);
  if (count < 1) {
    int nm[2] = { CTL_HW, HW_AVAILCPU };
    sysctl(nm, 2, &count, &len, NULL, 0);
    if (count < 1) return -1;
  }
  return (int)count;
#else
  return -1;
#endif
}

int csupport_get_physical_cores(void) {
  static int num_cores = 0;
  static int initialized = 0;
  if (!initialized) {
    num_cores = compute_host_num_physical_cores();
    initialized = 1;
  }
  return num_cores;
}

#else /* no pthread */

uint64_t csupport_thread_execute(csupport_thread_func_t func, void *arg,
                                 unsigned stack_size) {
  (void)stack_size;
  func(arg);
  return 0;
}
void csupport_thread_detach(uint64_t t) { (void)t; }
void csupport_thread_join(uint64_t t) { (void)t; }
uint64_t csupport_thread_get_id(uint64_t t) { return t; }
uint64_t csupport_thread_get_current_id(void) { return 0; }
uint64_t csupport_get_thread_id(void) { return 0; }
uint32_t csupport_get_max_thread_name_length(void) { return 0; }
int csupport_set_thread_name_cstr(const char *n) { (void)n; return -1; }
int csupport_get_thread_name_buf(char *b, size_t l) {
  if (b && l > 0) b[0] = '\0'; return 0;
}
int csupport_set_thread_priority_val(int p) { (void)p; return -1; }
int csupport_compute_host_num_hardware_threads(void) { return 1; }
void csupport_apply_thread_strategy_noop(unsigned n) { (void)n; }
unsigned csupport_get_cpus(void) { return 1; }
int csupport_get_physical_cores(void) { return -1; }

#endif
