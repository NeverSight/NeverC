/*===- Threading.c - Thread utilities (pure C) ------------------*- C -*-===*/
#include "include/csupport/lthreading.h"
#include "llvm/Config/config.h"
#include "llvm/Config/llvm-config.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if LLVM_ENABLE_THREADS == 1 && defined(HAVE_PTHREAD_H) && HAVE_PTHREAD_H
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

int csupport_set_thread_priority_val(csupport_thread_priority_t priority) {
#if defined(__linux__) && defined(SCHED_IDLE)
  struct sched_param param;
  param.sched_priority = 0;
  int policy = priority == CSUPPORT_THREAD_PRIORITY_DEFAULT ? SCHED_OTHER
                                                            : SCHED_IDLE;
  return pthread_setschedparam(pthread_self(), policy, &param) == 0 ? 0 : -1;
#elif defined(__APPLE__)
  int qos;
  switch (priority) {
  case CSUPPORT_THREAD_PRIORITY_BACKGROUND:
    qos = QOS_CLASS_BACKGROUND;
    break;
  case CSUPPORT_THREAD_PRIORITY_LOW:
    qos = QOS_CLASS_UTILITY;
    break;
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
  uint32_t count = 0;
  size_t len = sizeof(count);
  if (sysctlbyname("hw.logicalcpu", &count, &len, NULL, 0) == 0 && count > 0 &&
      count <= INT_MAX)
    return (int)count;
  count = 0;
  len = sizeof(count);
  if (sysctlbyname("hw.ncpu", &count, &len, NULL, 0) == 0 && count > 0 &&
      count <= INT_MAX)
    return (int)count;
#endif
#ifdef _SC_NPROCESSORS_ONLN
  {
    long val = sysconf(_SC_NPROCESSORS_ONLN);
    if (val > 0 && val <= INT_MAX) return (int)val;
  }
#endif
  return 1;
}

void csupport_apply_thread_strategy_noop(unsigned thread_pool_num) {
  (void)thread_pool_num;
}

unsigned csupport_get_cpus(void) { return 1; }

#if defined(__linux__) && defined(__x86_64__)
static int parse_proc_nonnegative_int(const char *text, int *value) {
  char *end = NULL;
  errno = 0;
  long parsed = strtol(text, &end, 10);
  if (errno == ERANGE || end == text || (*end != '\0' && *end != '\n') ||
      parsed < 0 || parsed > INT_MAX)
    return 0;
  *value = (int)parsed;
  return 1;
}

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

    if (strcmp(key, "processor") == 0) {
      cur_physical_id = -1;
      cur_siblings = -1;
      cur_core_id = -1;
      if (!parse_proc_nonnegative_int(val, &cur_processor))
        cur_processor = -1;
    } else if (strcmp(key, "physical id") == 0) {
      if (!parse_proc_nonnegative_int(val, &cur_physical_id))
        cur_physical_id = -1;
    } else if (strcmp(key, "siblings") == 0) {
      if (!parse_proc_nonnegative_int(val, &cur_siblings))
        cur_siblings = -1;
    }
    else if (strcmp(key, "core id") == 0) {
      if (!parse_proc_nonnegative_int(val, &cur_core_id))
        cur_core_id = -1;
      if (cur_processor >= 0 && cur_processor < CPU_SETSIZE &&
          CPU_ISSET(cur_processor, &affinity) &&
          cur_physical_id >= 0 && cur_siblings > 0 && cur_core_id >= 0) {
        const uint64_t idx = (uint64_t)(unsigned)cur_physical_id *
                                 (unsigned)cur_siblings +
                             (unsigned)cur_core_id;
        if (idx < CPU_SETSIZE)
          CPU_SET((int)idx, &enabled);
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
  {
    const long count = sysconf(_SC_NPROCESSORS_ONLN);
    return count > 0 && count <= INT_MAX ? (int)count : -1;
  }
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
  uint32_t count = 0;
  size_t len = sizeof(count);
  if (sysctlbyname("hw.physicalcpu", &count, &len, NULL, 0) != 0 ||
      count < 1) {
    int nm[2] = { CTL_HW, HW_AVAILCPU };
    count = 0;
    len = sizeof(count);
    if (sysctl(nm, 2, &count, &len, NULL, 0) != 0 || count < 1)
      return -1;
  }
  return count <= INT_MAX ? (int)count : -1;
#else
  return -1;
#endif
}

static pthread_once_t physical_cores_once = PTHREAD_ONCE_INIT;
static int physical_cores;

static void initialize_physical_cores(void) {
  physical_cores = compute_host_num_physical_cores();
}

int csupport_get_physical_cores(void) {
  pthread_once(&physical_cores_once, initialize_physical_cores);
  return physical_cores;
}

#elif defined(_WIN32)

/* Real Win32 threads.
 *
 * Windows has no <pthread.h> (config-ix.cmake forces HAVE_PTHREAD_H to 0 for
 * every non-Apple/Linux target), yet LLVM_ENABLE_THREADS stays 1 on Windows, so
 * llvm::thread (Support/thread.h) instantiates its *real* native-handle class
 * and routes every spawn through csupport_thread_execute.  The old no-pthread
 * fallback ran the routine synchronously and returned 0; thread.h reads a 0
 * handle as "the thread never started" and therefore SKIPS Callee.release(),
 * while the routine had already taken ownership of (and deleted) the
 * heap-allocated callee tuple -- a guaranteed double free on every llvm::thread
 * construction.  It corrupts the allocator's free list and resurfaces later as
 * an access violation in the next allocation-heavy pass (e.g. MemorySSA inside a
 * parallel LTO codegen worker: the symptom the user hit on Windows CI).
 *
 * Backing the layer with a genuine OS thread that returns a real, non-zero
 * HANDLE both eliminates the double free and restores actual parallelism on
 * Windows for every llvm::thread user -- bringing Windows to parity with the
 * Linux/macOS pthread path, which runs this identical code green.
 *
 * Use _beginthreadex (not CreateThread): it initializes the per-thread CRT
 * state the C/C++ runtime and the optimization pipeline rely on, exactly like
 * the std::thread these pools used before this layer was introduced. */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <process.h>
#include <windows.h>

static void report_win_fatal(const char *msg) {
  fprintf(stderr, "LLVM ERROR: %s (GetLastError=%lu)\n", msg,
          (unsigned long)GetLastError());
  abort();
}

/* Heap-allocated trampoline payload.  The callback uses CSupport's portable C
 * ABI while _beginthreadex requires an unsigned __stdcall(void *) entry point,
 * so this function is the one intentional calling-convention boundary. */
struct csupport_win_thread_start {
  csupport_thread_func_t func;
  void *arg;
};

static unsigned __stdcall csupport_win_thread_trampoline(void *raw) {
  struct csupport_win_thread_start start =
      *(struct csupport_win_thread_start *)raw;
  free(raw);
  start.func(start.arg);
  return 0;
}

uint64_t csupport_thread_execute(csupport_thread_func_t func, void *arg,
                                 unsigned stack_size) {
  struct csupport_win_thread_start *start =
      (struct csupport_win_thread_start *)malloc(sizeof(*start));
  uintptr_t handle;
  /* stack_size == 0 -> the image's default stack, matching std::thread.
   *
   * For a non-zero size, pass STACK_SIZE_PARAM_IS_A_RESERVATION so stack_size is
   * the RESERVED stack (address space only, pages committed on demand), exactly
   * like pthread_attr_setstacksize on Linux/macOS.  Without the flag Windows
   * treats stack_size as the up-front COMMIT: it both wastes physical memory
   * (the whole stack is committed per worker) and made large stacks impractical.
   * Reserving lets each llvm::thread worker cheaply get a big stack (thread.h's
   * DefaultStackSize) for the deeply recursive opt/codegen pipeline, matching
   * pthread semantics on the other hosts. */
  unsigned init_flags =
      (stack_size != 0) ? STACK_SIZE_PARAM_IS_A_RESERVATION : 0u;
  if (!start)
    report_win_fatal("csupport_thread_execute: out of memory");
  start->func = func;
  start->arg = arg;
  handle = _beginthreadex(NULL, stack_size, csupport_win_thread_trampoline,
                          start, init_flags, NULL);
  if (handle == 0) {
    free(start);
    report_win_fatal("_beginthreadex failed");
  }
  return (uint64_t)handle;
}

void csupport_thread_detach(uint64_t thread) {
  CloseHandle((HANDLE)(uintptr_t)thread);
}

void csupport_thread_join(uint64_t thread) {
  HANDLE h = (HANDLE)(uintptr_t)thread;
  if (WaitForSingleObject(h, INFINITE) == WAIT_FAILED)
    report_win_fatal("WaitForSingleObject failed");
  CloseHandle(h);
}

uint64_t csupport_thread_get_id(uint64_t thread) {
  return (uint64_t)GetThreadId((HANDLE)(uintptr_t)thread);
}

uint64_t csupport_thread_get_current_id(void) {
  return (uint64_t)GetCurrentThreadId();
}

uint64_t csupport_get_thread_id(void) { return (uint64_t)GetCurrentThreadId(); }

uint32_t csupport_get_max_thread_name_length(void) { return 0; }

typedef HRESULT(WINAPI *csupport_set_thread_description_fn)(HANDLE, PCWSTR);
typedef HRESULT(WINAPI *csupport_get_thread_description_fn)(HANDLE, PWSTR *);

static FARPROC get_thread_description_proc(const char *name) {
  HMODULE module = GetModuleHandleW(L"KernelBase.dll");
  if (!module)
    module = GetModuleHandleW(L"Kernel32.dll");
  return module ? GetProcAddress(module, name) : NULL;
}

int csupport_set_thread_name_cstr(const char *name) {
  if (!name)
    return -1;
  csupport_set_thread_description_fn set_description =
      (csupport_set_thread_description_fn)get_thread_description_proc(
          "SetThreadDescription");
  if (!set_description)
    return -1;
  int wide_length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name, -1,
                                        NULL, 0);
  if (wide_length <= 0 ||
      (size_t)wide_length > SIZE_MAX / sizeof(wchar_t))
    return -1;
  wchar_t *wide_name = (wchar_t *)malloc((size_t)wide_length * sizeof(wchar_t));
  if (!wide_name)
    return -1;
  if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name, -1, wide_name,
                          wide_length) != wide_length) {
    free(wide_name);
    return -1;
  }
  const HRESULT result = set_description(GetCurrentThread(), wide_name);
  free(wide_name);
  return SUCCEEDED(result) ? 0 : -1;
}

int csupport_get_thread_name_buf(char *buf, size_t buflen) {
  if (!buf || buflen == 0)
    return -1;
  buf[0] = '\0';
  csupport_get_thread_description_fn get_description =
      (csupport_get_thread_description_fn)get_thread_description_proc(
          "GetThreadDescription");
  if (!get_description)
    return 0;

  PWSTR wide_name = NULL;
  if (FAILED(get_description(GetCurrentThread(), &wide_name)) || !wide_name)
    return 0;
  int utf8_length =
      WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide_name, -1, NULL, 0,
                          NULL, NULL);
  if (utf8_length <= 0) {
    LocalFree(wide_name);
    return 0;
  }
  char *utf8_name = (char *)malloc((size_t)utf8_length);
  if (!utf8_name) {
    LocalFree(wide_name);
    return -1;
  }
  if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide_name, -1,
                          utf8_name, utf8_length, NULL, NULL) != utf8_length) {
    free(utf8_name);
    LocalFree(wide_name);
    return 0;
  }
  LocalFree(wide_name);

  size_t length = (size_t)utf8_length - 1;
  if (length >= buflen) {
    length = buflen - 1;
    while (length != 0 &&
           ((unsigned char)utf8_name[length] & 0xc0) == 0x80)
      --length;
  }
  memcpy(buf, utf8_name, length);
  buf[length] = '\0';
  free(utf8_name);
  return (int)length;
}

int csupport_set_thread_priority_val(csupport_thread_priority_t priority) {
  int windows_priority;
  switch (priority) {
  case CSUPPORT_THREAD_PRIORITY_BACKGROUND:
    windows_priority = THREAD_PRIORITY_LOWEST;
    break;
  case CSUPPORT_THREAD_PRIORITY_LOW:
    windows_priority = THREAD_PRIORITY_BELOW_NORMAL;
    break;
  default:
    windows_priority = THREAD_PRIORITY_NORMAL;
    break;
  }
  return SetThreadPriority(GetCurrentThread(), windows_priority) ? 0 : -1;
}

static unsigned count_bits_uintptr(uintptr_t value) {
  unsigned count = 0;
  while (value != 0) {
    value &= value - 1;
    ++count;
  }
  return count;
}

int csupport_compute_host_num_hardware_threads(void) {
  DWORD_PTR process_mask;
  DWORD_PTR system_mask;
  if (GetProcessAffinityMask(GetCurrentProcess(), &process_mask, &system_mask)) {
    const unsigned affinity_count = count_bits_uintptr((uintptr_t)process_mask);
    if (affinity_count != 0)
      return (int)affinity_count;
  }
  const DWORD active = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
  return active != 0 && active <= INT_MAX ? (int)active : 1;
}
void csupport_apply_thread_strategy_noop(unsigned n) { (void)n; }
unsigned csupport_get_cpus(void) {
  return (unsigned)csupport_compute_host_num_hardware_threads();
}

int csupport_get_physical_cores(void) {
  DWORD length = 0;
  GetLogicalProcessorInformationEx(RelationProcessorCore, NULL, &length);
  if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || length == 0)
    return -1;
  SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *info =
      (SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *)malloc(length);
  if (!info)
    return -1;
  if (!GetLogicalProcessorInformationEx(RelationProcessorCore, info, &length)) {
    free(info);
    return -1;
  }

  DWORD offset = 0;
  int cores = 0;
  while (offset < length) {
    SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *entry =
        (SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *)((char *)info + offset);
    if (entry->Size == 0 || entry->Size > length - offset) {
      cores = -1;
      break;
    }
    if (entry->Relationship == RelationProcessorCore) {
      if (cores == INT_MAX) {
        cores = -1;
        break;
      }
      ++cores;
    }
    offset += entry->Size;
  }
  free(info);
  return cores > 0 ? cores : -1;
}

#else /* no pthread, non-Windows: degrade to synchronous execution */

uint64_t csupport_thread_execute(csupport_thread_func_t func, void *arg,
                                 unsigned stack_size) {
  (void)stack_size;
  func(arg);
  /* Return a non-zero sentinel so llvm::thread::thread() takes the
   * Callee.release() path: the routine above already consumed (and freed) the
   * callee tuple, so a 0 return would make the constructor free it a second
   * time.  join()/detach() below are no-ops on this handle. */
  return 1;
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
int csupport_set_thread_priority_val(csupport_thread_priority_t p) {
  (void)p;
  return -1;
}
int csupport_compute_host_num_hardware_threads(void) { return 1; }
void csupport_apply_thread_strategy_noop(unsigned n) { (void)n; }
unsigned csupport_get_cpus(void) { return 1; }
int csupport_get_physical_cores(void) { return -1; }

#endif
