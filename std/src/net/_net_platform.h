#ifndef NEVERC_NET_PLATFORM_H
#define NEVERC_NET_PLATFORM_H

/*
 * Shared platform primitives for NeverC networking internals.
 *
 * This header owns socket, thread, mutex, condition-variable, clock, and
 * event-backend selection. Higher-level internal modules include it instead
 * of repeating platform branches.
 */

#include <stddef.h>
#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <windows.h>
  #pragma comment(lib, "ws2_32.lib")

  typedef SOCKET nc_sock_t;
  #define NC_INVALID_SOCK  INVALID_SOCKET
  #define NC_SOCK_ERR      SOCKET_ERROR
  #define nc_sock_close(s) closesocket(s)
  #define nc_sock_errno    WSAGetLastError()
  #define nc_sock_set_errno(e) WSASetLastError(e)

  typedef HANDLE nc_thread_t;
  typedef CRITICAL_SECTION nc_mutex_t;
  typedef CONDITION_VARIABLE nc_cond_t;

  #define nc_mutex_init(m)    InitializeCriticalSection(m)
  #define nc_mutex_destroy(m) DeleteCriticalSection(m)
  #define nc_mutex_lock(m)    EnterCriticalSection(m)
  #define nc_mutex_unlock(m)  LeaveCriticalSection(m)
  #define nc_cond_init(c)     InitializeConditionVariable(c)
  #define nc_cond_destroy(c)  ((void)(c))
  #define nc_cond_signal(c)   WakeConditionVariable(c)
  #define nc_cond_broadcast(c) WakeAllConditionVariable(c)
  #define nc_cond_wait(c, m)  SleepConditionVariableCS(c, m, INFINITE)

  static inline int nc_cond_wait_ms(nc_cond_t *cond, nc_mutex_t *mutex,
                                    uint32_t timeout_ms) {
      if (SleepConditionVariableCS(cond, mutex, (DWORD)timeout_ms)) return 0;
      return GetLastError() == ERROR_TIMEOUT ? 1 : -1;
  }

  static inline uint64_t nc_monotonic_ms(void) {
      return (uint64_t)GetTickCount64();
  }
  static inline int nc_cpu_count(void) {
      SYSTEM_INFO si;
      GetSystemInfo(&si);
      return (int)si.dwNumberOfProcessors;
  }

#else /* POSIX */
  #include <sys/socket.h>
  #include <sys/types.h>
  #include <netinet/in.h>
  #include <netinet/tcp.h>
  #include <arpa/inet.h>
  #include <netdb.h>
  #include <unistd.h>
  #include <fcntl.h>
  #include <errno.h>
  #include <signal.h>
  #include <pthread.h>
  #include <time.h>

  typedef int nc_sock_t;
  #define NC_INVALID_SOCK  (-1)
  #define NC_SOCK_ERR      (-1)
  #define nc_sock_close(s) close(s)
  #define nc_sock_errno    errno
  #define nc_sock_set_errno(e) (errno = (e))

  typedef pthread_t nc_thread_t;
  typedef pthread_mutex_t nc_mutex_t;
  typedef pthread_cond_t nc_cond_t;

  #define nc_mutex_init(m)    pthread_mutex_init(m, NULL)
  #define nc_mutex_destroy(m) pthread_mutex_destroy(m)
  #define nc_mutex_lock(m)    pthread_mutex_lock(m)
  #define nc_mutex_unlock(m)  pthread_mutex_unlock(m)
  #define nc_cond_init(c)     pthread_cond_init(c, NULL)
  #define nc_cond_destroy(c)  pthread_cond_destroy(c)
  #define nc_cond_signal(c)   pthread_cond_signal(c)
  #define nc_cond_broadcast(c) pthread_cond_broadcast(c)
  #define nc_cond_wait(c, m)  pthread_cond_wait(c, m)

  static inline int nc_cond_wait_ms(nc_cond_t *cond, nc_mutex_t *mutex,
                                    uint32_t timeout_ms) {
      struct timespec deadline;
      if (clock_gettime(CLOCK_REALTIME, &deadline) != 0) return -1;
      deadline.tv_sec += (time_t)(timeout_ms / 1000U);
      deadline.tv_nsec += (long)(timeout_ms % 1000U) * 1000000L;
      if (deadline.tv_nsec >= 1000000000L) {
          deadline.tv_sec++;
          deadline.tv_nsec -= 1000000000L;
      }
      int result = pthread_cond_timedwait(cond, mutex, &deadline);
      return result == 0 ? 0 : result == ETIMEDOUT ? 1 : -1;
  }

  static inline uint64_t nc_monotonic_ms(void) {
      struct timespec ts;
      clock_gettime(CLOCK_MONOTONIC, &ts);
      return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
  }
  static inline int nc_cpu_count(void) {
      long n = sysconf(_SC_NPROCESSORS_ONLN);
      return n > 0 ? (int)n : 1;
  }
#endif

/* Sequentially consistent atomics shared by poller and connection modules. */
#if defined(__GNUC__) || defined(__clang__)
  #define nc_atomic_inc(ptr) __atomic_add_fetch(ptr, 1, __ATOMIC_SEQ_CST)
  #define nc_atomic_dec(ptr) __atomic_sub_fetch(ptr, 1, __ATOMIC_SEQ_CST)
  #define nc_atomic_load(ptr) __atomic_load_n(ptr, __ATOMIC_SEQ_CST)
  #define nc_atomic_store(ptr, val) \
      __atomic_store_n(ptr, val, __ATOMIC_SEQ_CST)
  #define nc_atomic_cas(ptr, expected, desired) \
      __sync_bool_compare_and_swap(ptr, expected, desired)
  #define nc_atomic_ptr_load(ptr) \
      __atomic_load_n(ptr, __ATOMIC_SEQ_CST)
  #define nc_atomic_ptr_store(ptr, val) \
      __atomic_store_n(ptr, val, __ATOMIC_SEQ_CST)
  #define nc_atomic_ptr_exchange(ptr, val) \
      __atomic_exchange_n(ptr, val, __ATOMIC_SEQ_CST)
  #define nc_atomic_ptr_cas(ptr, expected, desired) \
      __sync_bool_compare_and_swap(ptr, expected, desired)
#elif defined(_WIN32)
  #define nc_atomic_inc(ptr) InterlockedIncrement((volatile LONG *)(ptr))
  #define nc_atomic_dec(ptr) InterlockedDecrement((volatile LONG *)(ptr))
  #define nc_atomic_load(ptr) \
      InterlockedCompareExchange((volatile LONG *)(ptr), 0, 0)
  #define nc_atomic_store(ptr, val) \
      InterlockedExchange((volatile LONG *)(ptr), val)
  #define nc_atomic_cas(ptr, expected, desired) \
      (InterlockedCompareExchange((volatile LONG *)(ptr), desired, expected) == \
       (LONG)(expected))
  #define nc_atomic_ptr_load(ptr) \
      InterlockedCompareExchangePointer((PVOID volatile *)(ptr), NULL, NULL)
  #define nc_atomic_ptr_store(ptr, val) \
      ((void)InterlockedExchangePointer((PVOID volatile *)(ptr), (PVOID)(val)))
  #define nc_atomic_ptr_exchange(ptr, val) \
      InterlockedExchangePointer((PVOID volatile *)(ptr), (PVOID)(val))
  #define nc_atomic_ptr_cas(ptr, expected, desired) \
      (InterlockedCompareExchangePointer((PVOID volatile *)(ptr), \
                                         (PVOID)(desired), \
                                         (PVOID)(expected)) == \
       (PVOID)(expected))
#endif

/*
 * Event polling backend detection.
 * Priority: io_uring > epoll > kqueue > IOCP > poll.
 */
#if defined(NC_FORCE_POLL) && NC_FORCE_POLL && !defined(_WIN32)
  #define NC_USE_POLL 1
  #include <poll.h>
#elif defined(NC_USE_IO_URING) && NC_USE_IO_URING && defined(__linux__)
  #include <sys/mman.h>
  #include <sys/syscall.h>
  #include <poll.h>
  #include <linux/time_types.h>
  #include <linux/io_uring.h>
#elif defined(__linux__) || defined(__ANDROID__)
  #define NC_USE_EPOLL 1
  #include <sys/epoll.h>
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || \
      defined(__NetBSD__) || defined(__DragonFly__)
  #define NC_USE_KQUEUE 1
  #include <sys/event.h>
#elif defined(_WIN32)
  #define NC_USE_IOCP 1
#else
  #define NC_USE_POLL 1
  #include <poll.h>
#endif

#endif /* NEVERC_NET_PLATFORM_H */
