/*===- Process.c - Process utilities (pure C) -------------------*- C -*-===*/
#include "include/csupport/lprocess.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Config/config.h"
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#ifndef LLVM_ENABLE_CRASH_DUMPS
#define LLVM_ENABLE_CRASH_DUMPS 0
#endif
int csupport_core_files_prevented = !LLVM_ENABLE_CRASH_DUMPS;

#define COLOR(FGBG, CODE, BOLD) "\033[0;" BOLD FGBG CODE "m"
#define ALLCOLORS(FGBG, BOLD) \
  {COLOR(FGBG, "0", BOLD), COLOR(FGBG, "1", BOLD), COLOR(FGBG, "2", BOLD), \
   COLOR(FGBG, "3", BOLD), COLOR(FGBG, "4", BOLD), COLOR(FGBG, "5", BOLD), \
   COLOR(FGBG, "6", BOLD), COLOR(FGBG, "7", BOLD)}

static const char s_colorcodes[2][2][8][10] = {
    {ALLCOLORS("3", ""), ALLCOLORS("3", "1;")},
    {ALLCOLORS("4", ""), ALLCOLORS("4", "1;")}};

#ifdef LLVM_ON_UNIX
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#if LLVM_ENABLE_THREADS == 1 && defined(HAVE_PTHREAD_H) && HAVE_PTHREAD_H
#include <pthread.h>
#endif
#ifdef HAVE_SYS_RESOURCE_H
#include <sys/resource.h>
#endif
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#ifdef HAVE_MALLOC_MALLOC_H
#include <malloc/malloc.h>
#endif
#if defined(HAVE_MALLINFO) || defined(HAVE_MALLINFO2)
#include <malloc.h>
#endif
#if defined(HAVE_MALLCTL)
#include <malloc_np.h>
#endif
#ifdef HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif
#if defined(HAVE_MACH_MACH_H) && !defined(__GNU__)
#include <mach/mach.h>
#endif

unsigned csupport_get_page_size(void) {
  long ps = sysconf(_SC_PAGESIZE);
  return ps > 0 ? (unsigned)ps : 4096;
}

int csupport_get_process_id(void) {
  return (int)getpid();
}

const char *csupport_get_env(const char *name) {
  return getenv(name);
}

int csupport_safely_close_fd(int fd) {
  sigset_t FullSet, SavedSet;
  if (sigfillset(&FullSet) < 0 || sigfillset(&SavedSet) < 0)
    return errno;

#if LLVM_ENABLE_THREADS == 1 && defined(HAVE_PTHREAD_H) && HAVE_PTHREAD_H
  {
    int mask_ec = pthread_sigmask(SIG_SETMASK, &FullSet, &SavedSet);
    if (mask_ec)
      return mask_ec;
  }
#else
  if (sigprocmask(SIG_SETMASK, &FullSet, &SavedSet) < 0)
    return errno;
#endif
  int ErrnoFromClose = 0;
  if (close(fd) < 0)
    ErrnoFromClose = errno;
  int EC = 0;
#if LLVM_ENABLE_THREADS == 1 && defined(HAVE_PTHREAD_H) && HAVE_PTHREAD_H
  EC = pthread_sigmask(SIG_SETMASK, &SavedSet, NULL);
#else
  if (sigprocmask(SIG_SETMASK, &SavedSet, NULL) < 0)
    EC = errno;
#endif
  if (ErrnoFromClose)
    return ErrnoFromClose;
  return EC;
}

int csupport_color_needs_flush(void) { return 0; }

const char *csupport_output_color(char code, int bold, int bg) {
  return s_colorcodes[bg ? 1 : 0][bold ? 1 : 0][code & 7];
}

const char *csupport_output_bold(int bg) {
  (void)bg;
  return "\033[1m";
}

const char *csupport_output_reverse(void) { return "\033[7m"; }
const char *csupport_reset_color(void) { return "\033[0m"; }

static int terminal_has_colors(int fd) {
  const char *term = getenv("TERM");
  if (!term) return 0;
  return isatty(fd) &&
         (strstr(term, "color") || strstr(term, "xterm") ||
          strstr(term, "screen") || strstr(term, "tmux") ||
          strstr(term, "vt100") || strstr(term, "linux") ||
          strstr(term, "ansi") || strcmp(term, "cygwin") == 0 ||
          strcmp(term, "rxvt") == 0);
}

int csupport_fd_has_colors(int fd) {
  return isatty(fd) && terminal_has_colors(fd);
}

int csupport_fd_write(int fd, const char *ptr, size_t size) {
  int32_t max_write = 0x7FFFFFFF;
#if defined(__linux__)
  max_write = 1024 * 1024 * 1024;
#endif
  while (size > 0) {
    size_t chunk = size < (size_t)max_write ? size : (size_t)max_write;
    ssize_t ret = write(fd, ptr, chunk);
    if (ret < 0) {
      if (errno == EINTR || errno == EAGAIN
#ifdef EWOULDBLOCK
          || errno == EWOULDBLOCK
#endif
      )
        continue;
      return -errno;
    }
    ptr += ret;
    size -= (size_t)ret;
  }
  return 0;
}

uint64_t csupport_fd_seek(int fd, uint64_t off) {
  return (uint64_t)lseek(fd, (off_t)off, SEEK_SET);
}

size_t csupport_fd_preferred_buffer_size(int fd, int is_displayed) {
  struct stat statbuf;
  if (fstat(fd, &statbuf) != 0)
    return 0;
  if (S_ISCHR(statbuf.st_mode) && is_displayed)
    return 0;
  return (size_t)statbuf.st_blksize;
}

int csupport_fd_is_regular_file(int fd) {
  struct stat statbuf;
  if (fstat(fd, &statbuf) != 0) return 0;
  return S_ISREG(statbuf.st_mode);
}

int64_t csupport_fd_tell(int fd) {
  return (int64_t)lseek(fd, 0, SEEK_CUR);
}

int csupport_stdout_fileno(void) { return STDOUT_FILENO; }
int csupport_stderr_fileno(void) { return STDERR_FILENO; }

void csupport_change_stdout_mode(csupport_open_flags_t flags) { (void)flags; }

int csupport_fd_open(const char *filename, size_t filename_len,
                     csupport_creation_disposition_t create_disp,
                     csupport_file_access_t access,
                     csupport_open_flags_t flags, int *err_out) {
  (void)filename_len;
  int oflags = 0;
  if ((access & CSUPPORT_FA_READ) && (access & CSUPPORT_FA_WRITE))
    oflags = O_RDWR;
  else if (access & CSUPPORT_FA_READ)
    oflags = O_RDONLY;
  else if (access & CSUPPORT_FA_WRITE)
    oflags = O_WRONLY;
  else {
    if (err_out) *err_out = EINVAL;
    return -1;
  }

  int append = (flags & CSUPPORT_OF_APPEND) != 0;

  /* Callers have always assumed append implies opening an existing file, so
     honour that over the requested disposition instead of truncating the very
     content the append was meant to extend. */
  if (append)
    create_disp = CSUPPORT_CD_OPEN_ALWAYS;

  if (create_disp > CSUPPORT_CD_OPEN_ALWAYS) {
    if (err_out) *err_out = EINVAL;
    return -1;
  }
  switch (create_disp) {
  case CSUPPORT_CD_CREATE_ALWAYS: oflags |= O_CREAT | O_TRUNC; break;
  case CSUPPORT_CD_CREATE_NEW: oflags |= O_CREAT | O_EXCL; break;
  case CSUPPORT_CD_OPEN_EXISTING: break;
  case CSUPPORT_CD_OPEN_ALWAYS: oflags |= O_CREAT; break;
  }
  if (append)
    oflags |= O_APPEND;
#ifdef O_CLOEXEC
  if (!(flags & CSUPPORT_OF_CHILD_INHERIT))
    oflags |= O_CLOEXEC;
#endif

  int fd = open(filename, oflags, 0666);
  if (fd < 0) {
    if (err_out) *err_out = errno;
    return -1;
  }
  if (err_out) *err_out = 0;
  return fd;
}

int csupport_fd_write_console(int fd, const char *ptr, size_t size) {
  (void)fd; (void)ptr; (void)size;
  return 0;
}

size_t csupport_get_malloc_usage(void) {
#if defined(HAVE_MALLINFO2)
  struct mallinfo2 mi = mallinfo2();
  return mi.uordblks;
#elif defined(HAVE_MALLINFO)
  struct mallinfo mi = mallinfo();
  return mi.uordblks;
#elif defined(HAVE_MALLOC_ZONE_STATISTICS) && defined(HAVE_MALLOC_MALLOC_H)
  malloc_statistics_t Stats;
  malloc_zone_statistics(malloc_default_zone(), &Stats);
  return Stats.size_in_use;
#elif defined(HAVE_MALLCTL)
  size_t alloc, sz = sizeof(size_t);
  if (mallctl("stats.allocated", &alloc, &sz, NULL, 0) == 0)
    return alloc;
  return 0;
#else
  return 0;
#endif
}

static int64_t timeval_to_ns(struct timeval *tv) {
  return (int64_t)tv->tv_sec * 1000000000LL + (int64_t)tv->tv_usec * 1000LL;
}

void csupport_get_time_usage(int64_t *elapsed_ns, int64_t *user_ns,
                             int64_t *sys_ns) {
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  *elapsed_ns = (int64_t)ts.tv_sec * 1000000000LL + (int64_t)ts.tv_nsec;
#if defined(HAVE_GETRUSAGE)
  struct rusage RU;
  getrusage(RUSAGE_SELF, &RU);
  *user_ns = timeval_to_ns(&RU.ru_utime);
  *sys_ns = timeval_to_ns(&RU.ru_stime);
#else
  *user_ns = 0;
  *sys_ns = 0;
#endif
}

void csupport_prevent_core_files(void) {
#if HAVE_SETRLIMIT
  struct rlimit rlim;
  rlim.rlim_cur = rlim.rlim_max = 0;
  setrlimit(RLIMIT_CORE, &rlim);
#endif
#if defined(HAVE_MACH_MACH_H) && !defined(__GNU__)
  {
    mach_msg_type_number_t Count = 0;
    exception_mask_t OriginalMasks[EXC_TYPES_COUNT];
    exception_port_t OriginalPorts[EXC_TYPES_COUNT];
    exception_behavior_t OriginalBehaviors[EXC_TYPES_COUNT];
    thread_state_flavor_t OriginalFlavors[EXC_TYPES_COUNT];
    kern_return_t err = task_get_exception_ports(
        mach_task_self(), EXC_MASK_ALL, OriginalMasks, &Count, OriginalPorts,
        OriginalBehaviors, OriginalFlavors);
    if (err == KERN_SUCCESS) {
      unsigned i;
      for (i = 0; i != Count; ++i)
        task_set_exception_ports(mach_task_self(), OriginalMasks[i],
                                 MACH_PORT_NULL, OriginalBehaviors[i],
                                 OriginalFlavors[i]);
    }
    signal(SIGABRT, (void (*)(int))_exit);
    signal(SIGILL, (void (*)(int))_exit);
    signal(SIGFPE, (void (*)(int))_exit);
    signal(SIGSEGV, (void (*)(int))_exit);
    signal(SIGBUS, (void (*)(int))_exit);
  }
#endif
  csupport_core_files_prevented = 1;
}

int csupport_fixup_std_fds(void) {
  int NullFD = -1;
  int StandardFDs[3] = {0, 1, 2}; /* STDIN, STDOUT, STDERR */
  int i;
  for (i = 0; i < 3; i++) {
    struct stat st;
    errno = 0;
    if (fstat(StandardFDs[i], &st) < 0) {
      if (errno != EBADF)
        return errno;
      if (NullFD < 0) {
        NullFD = open("/dev/null", O_RDWR);
        if (NullFD < 0)
          return errno;
      }
      if (NullFD != StandardFDs[i]) {
        if (dup2(NullFD, StandardFDs[i]) < 0)
          return errno;
      }
    }
  }
  if (NullFD > 2)
    close(NullFD);
  return 0;
}

int csupport_fd_is_displayed(int fd) {
#if HAVE_ISATTY
  return isatty(fd);
#else
  return 0;
#endif
}

unsigned csupport_fd_columns(int fd) {
  if (!csupport_fd_is_displayed(fd))
    return 0;
  const char *cols = getenv("COLUMNS");
  if (cols) {
    char *end = NULL;
    errno = 0;
    unsigned long columns = strtoul(cols, &end, 10);
    if (errno != ERANGE && end != cols && *end == '\0' && columns > 0 &&
        columns <= UINT_MAX)
      return (unsigned)columns;
  }
  return 0;
}

int csupport_fd_has_terminal_colors(int fd) {
  return csupport_fd_is_displayed(fd) && terminal_has_colors(fd);
}

#if !HAVE_DECL_ARC4RANDOM
static unsigned get_random_number_seed(void) {
  unsigned seed = 0;
  int urfd = open("/dev/urandom", O_RDONLY);
  if (urfd >= 0) {
    ssize_t bytes_read = read(urfd, &seed, sizeof(seed));
    close(urfd);
    if (bytes_read == (ssize_t)sizeof(seed))
      return seed;
  }
  return (unsigned)time(NULL) ^ (unsigned)getpid();
}

#if LLVM_ENABLE_THREADS == 1 && defined(HAVE_PTHREAD_H) && HAVE_PTHREAD_H
static pthread_once_t random_seed_once = PTHREAD_ONCE_INIT;
static pthread_mutex_t random_number_mutex = PTHREAD_MUTEX_INITIALIZER;

static void initialize_random_number_generator(void) {
  srand(get_random_number_seed());
}
#else
static int random_number_seeded;
#endif
#endif

unsigned csupport_get_random_number(void) {
#if HAVE_DECL_ARC4RANDOM
  return arc4random();
#else
#if LLVM_ENABLE_THREADS == 1 && defined(HAVE_PTHREAD_H) && HAVE_PTHREAD_H
  pthread_once(&random_seed_once, initialize_random_number_generator);
  pthread_mutex_lock(&random_number_mutex);
  unsigned value = (unsigned)rand();
  pthread_mutex_unlock(&random_number_mutex);
  return value;
#else
  if (!random_number_seeded) {
    srand(get_random_number_seed());
    random_number_seeded = 1;
  }
  return (unsigned)rand();
#endif
#endif
}

void csupport_use_ansi_escape_codes(int enable) { (void)enable; }

void csupport_exit_no_cleanup(int retcode) { _Exit(retcode); }

int csupport_fd_lock(int fd) {
  struct flock fl;
  memset(&fl, 0, sizeof(fl));
  fl.l_type = F_WRLCK;
  fl.l_whence = SEEK_SET;
  fl.l_start = 0;
  fl.l_len = 0;
  if (fcntl(fd, F_SETLKW, &fl) == -1)
    return errno;
  return 0;
}

int csupport_fd_try_lock_for(int fd, int64_t timeout_ms) {
  struct flock fl;
  memset(&fl, 0, sizeof(fl));
  fl.l_type = F_WRLCK;
  fl.l_whence = SEEK_SET;
  fl.l_start = 0;
  fl.l_len = 0;
  if (timeout_ms == 0) {
    if (fcntl(fd, F_SETLK, &fl) == -1)
      return errno;
    return 0;
  }
  struct timespec deadline, now;
  clock_gettime(CLOCK_MONOTONIC, &deadline);
  deadline.tv_sec += timeout_ms / 1000;
  deadline.tv_nsec += (timeout_ms % 1000) * 1000000;
  if (deadline.tv_nsec >= 1000000000) {
    deadline.tv_sec++;
    deadline.tv_nsec -= 1000000000;
  }
  for (;;) {
    if (fcntl(fd, F_SETLK, &fl) != -1)
      return 0;
    if (errno != EACCES && errno != EAGAIN)
      return errno;
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (now.tv_sec > deadline.tv_sec ||
        (now.tv_sec == deadline.tv_sec && now.tv_nsec >= deadline.tv_nsec))
      return ETIMEDOUT;
    struct timespec slp = {0, 1000000}; /* 1ms */
    nanosleep(&slp, NULL);
  }
}

#else /* Windows */

#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

unsigned csupport_get_page_size(void) {
  SYSTEM_INFO info;
  GetSystemInfo(&info);
  return info.dwPageSize;
}
int csupport_get_process_id(void) { return (int)GetCurrentProcessId(); }
const char *csupport_get_env(const char *name) { return getenv(name); }
int csupport_safely_close_fd(int fd) {
  if (_close(fd) < 0)
    return errno;
  return 0;
}

int csupport_color_needs_flush(void) { return 0; }

const char *csupport_output_color(char code, int bold, int bg) {
  return s_colorcodes[bg ? 1 : 0][bold ? 1 : 0][code & 7];
}

const char *csupport_output_bold(int bg) {
  (void)bg;
  return "\033[1m";
}
const char *csupport_output_reverse(void) { return "\033[7m"; }
const char *csupport_reset_color(void) { return "\033[0m"; }

int csupport_fd_has_colors(int fd) { return _isatty(fd); }

int csupport_fd_write(int fd, const char *ptr, size_t size) {
  while (size > 0) {
    size_t chunk = size < 0x7FFFFFFF ? size : 0x7FFFFFFF;
    int ret = _write(fd, ptr, (unsigned)chunk);
    if (ret < 0) return -errno;
    if (ret == 0) return -EIO;
    ptr += ret;
    size -= (size_t)ret;
  }
  return 0;
}

uint64_t csupport_fd_seek(int fd, uint64_t off) {
  return (uint64_t)_lseeki64(fd, (__int64)off, SEEK_SET);
}

size_t csupport_fd_preferred_buffer_size(int fd, int is_displayed) {
  if (is_displayed)
    return 0;
  struct _stat64 status;
  return _fstat64(fd, &status) == 0 ? 4096u : 0u;
}

int csupport_fd_is_regular_file(int fd) {
  struct _stat64 status;
  return _fstat64(fd, &status) == 0 && (status.st_mode & _S_IFMT) == _S_IFREG;
}

int64_t csupport_fd_tell(int fd) {
  return (int64_t)_lseeki64(fd, 0, SEEK_CUR);
}

int csupport_stdout_fileno(void) { return 1; }
int csupport_stderr_fileno(void) { return 2; }

void csupport_change_stdout_mode(csupport_open_flags_t flags) {
  /* OF_CRLF, not OF_Text, is what asks for newline translation on
     Windows; OF_Text alone only means something on z/OS. ChangeStdoutMode()
     already tests OF_CRLF. */
  if (!(flags & CSUPPORT_OF_CRLF))
    _setmode(1, _O_BINARY);
}

int csupport_fd_open(const char *filename, size_t filename_len,
                     csupport_creation_disposition_t create_disp,
                     csupport_file_access_t access,
                     csupport_open_flags_t flags, int *err_out) {
  int oflags = 0;
  if ((access & CSUPPORT_FA_READ) && (access & CSUPPORT_FA_WRITE))
    oflags = _O_RDWR;
  else if (access & CSUPPORT_FA_READ)
    oflags = _O_RDONLY;
  else if (access & CSUPPORT_FA_WRITE)
    oflags = _O_WRONLY;
  else {
    if (err_out) *err_out = EINVAL;
    return -1;
  }

  int append = (flags & CSUPPORT_OF_APPEND) != 0;

  /* Callers have always assumed append implies opening an existing file, so
     honour that over the requested disposition instead of truncating the very
     content the append was meant to extend. */
  if (append)
    create_disp = CSUPPORT_CD_OPEN_ALWAYS;

  if (create_disp > CSUPPORT_CD_OPEN_ALWAYS) {
    if (err_out) *err_out = EINVAL;
    return -1;
  }
  switch (create_disp) {
  case CSUPPORT_CD_CREATE_ALWAYS: oflags |= _O_CREAT | _O_TRUNC; break;
  case CSUPPORT_CD_CREATE_NEW: oflags |= _O_CREAT | _O_EXCL; break;
  case CSUPPORT_CD_OPEN_EXISTING: break;
  case CSUPPORT_CD_OPEN_ALWAYS: oflags |= _O_CREAT; break;
  }
  /* Only OF_CRLF asks for newline translation. Keying this off OF_Text
     turned every text-mode writer into a CRLF writer, which is why the target
     schema generator produced a golden file that differed from the checked-in
     one by its trailing newline. */
  if (!(flags & CSUPPORT_OF_CRLF))
    oflags |= _O_BINARY;
  if (append)
    oflags |= _O_APPEND;
  if (flags & CSUPPORT_OF_DELETE)
    oflags |= _O_TEMPORARY;
  if (!(flags & CSUPPORT_OF_CHILD_INHERIT))
    oflags |= _O_NOINHERIT;

  /* LLVM paths are UTF-8. The narrow CRT _open() interprets its argument in
     the active Windows code page, which can silently create a mojibake file
     instead of the requested non-ASCII path. Convert the complete byte span
     explicitly and use the wide CRT entry point. */
  if (filename_len > INT_MAX ||
      memchr(filename, '\0', filename_len) != NULL) {
    if (err_out) *err_out = EINVAL;
    return -1;
  }

  int wide_len = 0;
  if (filename_len != 0) {
    wide_len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, filename,
                                   (int)filename_len, NULL, 0);
    if (wide_len == 0) {
      if (err_out) *err_out = EINVAL;
      return -1;
    }
  }

  wchar_t *wide_filename =
      (wchar_t *)malloc(((size_t)wide_len + 1) * sizeof(wchar_t));
  if (!wide_filename) {
    if (err_out) *err_out = ENOMEM;
    return -1;
  }
  if (wide_len != 0 &&
      MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, filename,
                          (int)filename_len, wide_filename, wide_len) !=
          wide_len) {
    free(wide_filename);
    if (err_out) *err_out = EINVAL;
    return -1;
  }
  wide_filename[wide_len] = L'\0';

  int fd = _wopen(wide_filename, oflags, _S_IREAD | _S_IWRITE);
  int open_error = errno;
  free(wide_filename);
  if (fd < 0) {
    if (err_out) *err_out = open_error;
    return -1;
  }
  if (err_out) *err_out = 0;
  return fd;
}

// <windows.h> doesn't pull in <wincrypt.h> under WIN32_LEAN_AND_MEAN (which
// the build defines globally), but csupport_get_random_number() below uses
// CryptAcquireContextW/CryptGenRandom/CryptReleaseContext, so request it
// explicitly.
#include <wincrypt.h>
int csupport_fd_write_console(int fd, const char *ptr, size_t size) {
  HANDLE h = (HANDLE)_get_osfhandle(fd);
  if (GetFileType(h) != FILE_TYPE_CHAR || size > INT_MAX)
    return 0;
  int wlen = MultiByteToWideChar(CP_UTF8, 0, ptr, (int)size, NULL, 0);
  if (wlen <= 0)
    return 0;
  wchar_t *wbuf = (wchar_t *)malloc((size_t)wlen * sizeof(wchar_t));
  if (!wbuf)
    return 0;
  if (MultiByteToWideChar(CP_UTF8, 0, ptr, (int)size, wbuf, wlen) != wlen) {
    free(wbuf);
    return 0;
  }
  DWORD total = 0;
  while (total < (DWORD)wlen) {
    DWORD written = 0;
    if (!WriteConsoleW(h, wbuf + total, (DWORD)wlen - total, &written, NULL) ||
        written == 0) {
      free(wbuf);
      return 0;
    }
    total += written;
  }
  free(wbuf);
  return 1;
}

size_t csupport_get_malloc_usage(void) { return 0; }

void csupport_get_time_usage(int64_t *elapsed_ns, int64_t *user_ns,
                             int64_t *sys_ns) {
  FILETIME creation, exit_t, kernel, user;
  GetProcessTimes(GetCurrentProcess(), &creation, &exit_t, &kernel, &user);
  ULARGE_INTEGER u, k;
  u.LowPart = user.dwLowDateTime;  u.HighPart = user.dwHighDateTime;
  k.LowPart = kernel.dwLowDateTime; k.HighPart = kernel.dwHighDateTime;
  *user_ns = (int64_t)(u.QuadPart * 100);
  *sys_ns = (int64_t)(k.QuadPart * 100);
  FILETIME now;
  GetSystemTimeAsFileTime(&now);
  ULARGE_INTEGER n;
  n.LowPart = now.dwLowDateTime; n.HighPart = now.dwHighDateTime;
  *elapsed_ns = (int64_t)(n.QuadPart * 100);
}

void csupport_prevent_core_files(void) {
  csupport_core_files_prevented = 1;
}

int csupport_fixup_std_fds(void) { return 0; }
int csupport_fd_is_displayed(int fd) { return _isatty(fd); }
unsigned csupport_fd_columns(int fd) {
  if (!_isatty(fd)) return 0;
  HANDLE h = (HANDLE)_get_osfhandle(fd);
  CONSOLE_SCREEN_BUFFER_INFO info;
  if (GetConsoleScreenBufferInfo(h, &info))
    return (unsigned)(info.dwSize.X);
  return 0;
}

int csupport_fd_has_terminal_colors(int fd) { return _isatty(fd); }
unsigned csupport_get_random_number(void) {
  unsigned val = 0;
  HCRYPTPROV hProv;
  if (CryptAcquireContextW(&hProv, NULL, NULL, PROV_RSA_FULL,
                            CRYPT_VERIFYCONTEXT)) {
    const BOOL generated = CryptGenRandom(hProv, sizeof(val), (BYTE *)&val);
    CryptReleaseContext(hProv, 0);
    if (generated)
      return val;
  }
  return (unsigned)GetTickCount() ^ (unsigned)GetCurrentProcessId();
}
void csupport_use_ansi_escape_codes(int enable) {
  const DWORD handles[] = {STD_OUTPUT_HANDLE, STD_ERROR_HANDLE};
  for (size_t i = 0; i < sizeof(handles) / sizeof(handles[0]); ++i) {
    HANDLE handle = GetStdHandle(handles[i]);
    DWORD mode;
    if (!handle || handle == INVALID_HANDLE_VALUE ||
        !GetConsoleMode(handle, &mode))
      continue;
    if (enable)
      mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    else
      mode &= ~ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(handle, mode);
  }
}
void csupport_exit_no_cleanup(int retcode) { _exit(retcode); }

static int csupport_win32_error_to_errno(DWORD error) {
  switch (error) {
  case ERROR_INVALID_HANDLE:
    return EBADF;
  case ERROR_INVALID_PARAMETER:
    return EINVAL;
  case ERROR_ACCESS_DENIED:
  case ERROR_LOCK_VIOLATION:
  case ERROR_SHARING_VIOLATION:
    return EACCES;
  case ERROR_NOT_ENOUGH_MEMORY:
  case ERROR_OUTOFMEMORY:
    return ENOMEM;
  case ERROR_TOO_MANY_OPEN_FILES:
    return EMFILE;
  case ERROR_TIMEOUT:
    return ETIMEDOUT;
  default:
    return EIO;
  }
}

int csupport_fd_lock(int fd) {
  if (fd < 0)
    return EBADF;
  intptr_t os_handle = _get_osfhandle(fd);
  if (os_handle == -1)
    return errno ? errno : EBADF;
  HANDLE h = (HANDLE)os_handle;
  OVERLAPPED ov = {0};
  if (LockFileEx(h, LOCKFILE_EXCLUSIVE_LOCK, 0, MAXDWORD, MAXDWORD, &ov))
    return 0;
  return csupport_win32_error_to_errno(GetLastError());
}

int csupport_fd_try_lock_for(int fd, int64_t timeout_ms) {
  if (fd < 0)
    return EBADF;
  intptr_t os_handle = _get_osfhandle(fd);
  if (os_handle == -1)
    return errno ? errno : EBADF;
  HANDLE h = (HANDLE)os_handle;
  OVERLAPPED ov = {0};
  DWORD flags = LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY;
  if (timeout_ms <= 0) {
    if (LockFileEx(h, flags, 0, MAXDWORD, MAXDWORD, &ov))
      return 0;
    return csupport_win32_error_to_errno(GetLastError());
  }
  ULONGLONG start = GetTickCount64();
  for (;;) {
    if (LockFileEx(h, flags, 0, MAXDWORD, MAXDWORD, &ov))
      return 0;
    DWORD error = GetLastError();
    if (error != ERROR_LOCK_VIOLATION)
      return csupport_win32_error_to_errno(error);
    if (GetTickCount64() - start >= (uint64_t)timeout_ms)
      return ETIMEDOUT;
    Sleep(1);
  }
}

#endif
