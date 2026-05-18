/*===- Program.c - Program execution (pure C) -------------------*- C -*-===*/
#include "include/csupport/lprogram.h"
#include "llvm/Config/config.h"
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

/* -- ChangeStdinToBinary / ChangeStdoutToBinary -- */

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#include <stdio.h>
int csupport_change_stdin_to_binary(void) {
  int result = _setmode(_fileno(stdin), _O_BINARY);
  return result == -1 ? errno : 0;
}
int csupport_change_stdout_to_binary(void) {
  int result = _setmode(_fileno(stdout), _O_BINARY);
  return result == -1 ? errno : 0;
}
#else
int csupport_change_stdin_to_binary(void) { return 0; }
int csupport_change_stdout_to_binary(void) { return 0; }
#endif

/* -- SetMemoryLimits -- */

#ifdef LLVM_ON_UNIX
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>

#if defined(HAVE_SYS_RESOURCE_H) || defined(__APPLE__) || defined(__linux__)
#include <sys/resource.h>
#define HAS_RLIMIT 1
#endif

void csupport_set_memory_limits(unsigned size_mb) {
#ifdef HAS_RLIMIT
  struct rlimit r;
  __typeof__(r.rlim_cur) limit = (__typeof__(r.rlim_cur))(size_mb) * 1048576;
  getrlimit(RLIMIT_DATA, &r);
  r.rlim_cur = limit;
  setrlimit(RLIMIT_DATA, &r);
#ifdef RLIMIT_RSS
  getrlimit(RLIMIT_RSS, &r);
  r.rlim_cur = limit;
  setrlimit(RLIMIT_RSS, &r);
#endif
#else
  (void)size_mb;
#endif
}

int csupport_execute_and_wait(const char *program, const char *const *args,
                              int *exit_code) {
  pid_t pid = fork();
  if (pid < 0) return -1;
  if (pid == 0) {
    execvp(program, (char **)((uintptr_t)args));
    _exit(127);
  }
  int status;
  while (waitpid(pid, &status, 0) < 0) {
    if (errno != EINTR) return -1;
  }
  if (WIFEXITED(status)) {
    *exit_code = WEXITSTATUS(status);
    return 0;
  }
  return -1;
}

#else /* !LLVM_ON_UNIX */

void csupport_set_memory_limits(unsigned size_mb) {
  (void)size_mb;
}

int csupport_execute_and_wait(const char *program, const char *const *args,
                              int *exit_code) {
  (void)program; (void)args;
  *exit_code = -1;
  return -1;
}

#endif

/* --- Session 11: extract Wait/RedirectIO/cmdArgsFit from Program.inc --- */

#ifdef LLVM_ON_UNIX

/* --- RedirectIO: open + dup2 for child process I/O redirection --- */
int csupport_redirect_io(const char *path, int fd,
                         char *errmsg, size_t errmsg_cap) {
  if (!path) return 0;
  const char *file = (path[0] != '\0') ? path : "/dev/null";
  int in_fd = open(file, fd == 0 ? O_RDONLY : O_WRONLY | O_CREAT, 0666);
  if (in_fd == -1) {
    if (errmsg && errmsg_cap > 0)
      snprintf(errmsg, errmsg_cap, "Cannot open file '%s' for %s: %s",
               file, fd == 0 ? "input" : "output", strerror(errno));
    return -1;
  }
  if (dup2(in_fd, fd) == -1) {
    if (errmsg && errmsg_cap > 0)
      snprintf(errmsg, errmsg_cap, "Cannot dup2: %s", strerror(errno));
    close(in_fd);
    return -1;
  }
  close(in_fd);
  return 0;
}

/* --- commandLineFitsWithinSystemLimits core --- */
int csupport_cmd_args_fit(size_t prog_len,
                          const char *const *args, size_t num_args) {
  long arg_max = sysconf(_SC_ARG_MAX);
  long arg_min = _POSIX_ARG_MAX;
  long effective = 128 * 1024;

  if (effective > arg_max) effective = arg_max;
  else if (effective < arg_min) effective = arg_min;

  if (arg_max == -1) return 1;

  long half = effective / 2;
  size_t total = prog_len + 1;
  for (size_t i = 0; i < num_args; i++) {
    size_t len = strlen(args[i]);
    if (len >= (32 * 4096)) return 0;
    total += len + 1;
    if (total > (size_t)half) return 0;
  }
  return 1;
}

/* --- Wait for child process (core POSIX wait4/alarm logic) --- */

static void csupport_timeout_handler_nop(int sig) { (void)sig; }

#ifdef _AIX
#ifndef _ALL_SOURCE
extern pid_t(wait4)(pid_t pid, int *status, int options, struct rusage *usage);
#endif
static pid_t csupport_wait4_aix(pid_t pid, int *status, int options,
                                struct rusage *usage) {
  if (!(options & WNOHANG))
    return wait4(pid, status, options, usage);
  siginfo_t wi;
  wi.si_pid = 0;
  int rv = waitid(P_PID, pid, &wi, WNOWAIT | WEXITED | options);
  if (rv == -1 || wi.si_pid == 0) return rv;
  return wait4(pid, status, options & ~WNOHANG, usage);
}
#define CSUPPORT_WAIT4(p, s, o, r) csupport_wait4_aix(p, s, o, r)
#elif defined(__Fuchsia__)
#define CSUPPORT_WAIT4(p, s, o, r) ((void)(r), -1)
#else
#define CSUPPORT_WAIT4(p, s, o, r) wait4(p, s, o, r)
#endif

int csupport_wait_process(int child_pid, unsigned seconds_to_wait, int polling,
                          int *out_pid, int *out_return_code,
                          int64_t *out_user_time_us, int64_t *out_kernel_time_us,
                          uint64_t *out_peak_memory,
                          char *errmsg, size_t errmsg_cap) {
  struct sigaction act, old;
  int wait_options = 0;
  int wait_until_term = 0;

  if (seconds_to_wait == (unsigned)-1) {
    wait_until_term = 1;
  } else {
    if (seconds_to_wait == 0)
      wait_options = WNOHANG;
    memset(&act, 0, sizeof(act));
    act.sa_handler = csupport_timeout_handler_nop;
    sigemptyset(&act.sa_mask);
    sigaction(SIGALRM, &act, &old);
    alarm(seconds_to_wait);
  }

  int status = 0;
  pid_t wait_pid = 0;
#ifndef __Fuchsia__
  struct rusage info;
  memset(&info, 0, sizeof(info));
  do {
    wait_pid = CSUPPORT_WAIT4(child_pid, &status, wait_options, &info);
  } while (wait_until_term && wait_pid == -1 && errno == EINTR);
#endif

  *out_pid = (int)wait_pid;

  if (wait_pid != child_pid) {
    if (wait_pid == 0) {
      *out_return_code = 0;
      return 0;
    }
    if (seconds_to_wait != (unsigned)-1 && errno == EINTR && !polling) {
      kill(child_pid, SIGKILL);
      alarm(0);
      sigaction(SIGALRM, &old, NULL);
      if (wait(&status) != child_pid) {
        if (errmsg && errmsg_cap > 0)
          snprintf(errmsg, errmsg_cap, "Child timed out but wouldn't die");
      } else {
        if (errmsg && errmsg_cap > 0)
          snprintf(errmsg, errmsg_cap, "%s", "Child timed out");
      }
      *out_return_code = -2;
      return 1;
    }
    if (errno != EINTR) {
      if (errmsg && errmsg_cap > 0)
        snprintf(errmsg, errmsg_cap, "Error waiting for child process");
      *out_return_code = -1;
      return -1;
    }
  }

  if (seconds_to_wait != (unsigned)-1 && !wait_until_term) {
    alarm(0);
    sigaction(SIGALRM, &old, NULL);
  }

#ifndef __Fuchsia__
  if (out_user_time_us)
    *out_user_time_us = (int64_t)info.ru_utime.tv_sec * 1000000LL +
                        info.ru_utime.tv_usec;
  if (out_kernel_time_us)
    *out_kernel_time_us = (int64_t)info.ru_stime.tv_sec * 1000000LL +
                          info.ru_stime.tv_usec;
  if (out_peak_memory) {
#if !defined(__HAIKU__) && !defined(__MVS__)
    *out_peak_memory = (uint64_t)info.ru_maxrss;
#else
    *out_peak_memory = 0;
#endif
  }
#else
  if (out_user_time_us) *out_user_time_us = 0;
  if (out_kernel_time_us) *out_kernel_time_us = 0;
  if (out_peak_memory) *out_peak_memory = 0;
#endif

  if (WIFEXITED(status)) {
    int result = WEXITSTATUS(status);
    *out_return_code = result;
    if (result == 127) {
      if (errmsg && errmsg_cap > 0) {
        char *msg = strerror(ENOENT);
        snprintf(errmsg, errmsg_cap, "%s", msg ? msg : "No such file or directory");
      }
      *out_return_code = -1;
      return 1;
    }
    if (result == 126) {
      if (errmsg && errmsg_cap > 0)
        snprintf(errmsg, errmsg_cap, "Program could not be executed");
      *out_return_code = -1;
      return 1;
    }
    return 1;
  }

  if (WIFSIGNALED(status)) {
    if (errmsg && errmsg_cap > 0) {
      const char *sig = strsignal(WTERMSIG(status));
      int n = snprintf(errmsg, errmsg_cap, "%s",
                       sig ? sig : "Unknown signal");
#ifdef WCOREDUMP
      if (WCOREDUMP(status) && n > 0 && (size_t)n < errmsg_cap)
        snprintf(errmsg + n, errmsg_cap - (size_t)n, " (core dumped)");
#endif
    }
    *out_return_code = -2;
    return 1;
  }

  *out_return_code = 0;
  return 1;
}

/* --- Session 13: Execute fork/exec/posix_spawn core --- */

#ifdef HAVE_POSIX_SPAWN
#include <spawn.h>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#if defined(__APPLE__) && !(defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
#define CSUPPORT_USE_NSGETENVIRON 1
#else
#define CSUPPORT_USE_NSGETENVIRON 0
#endif

#if !CSUPPORT_USE_NSGETENVIRON
extern char **environ;
#else
#include <crt_externs.h>
#endif
#endif /* HAVE_POSIX_SPAWN */

int csupport_execute_child(const char *program,
                           const char *const *argv,
                           const char *const *envp,
                           const csupport_redirect_t *redirects,
                           int num_redirects,
                           unsigned memory_limit_mb,
                           char *errmsg, size_t errmsg_cap) {
#ifdef HAVE_POSIX_SPAWN
  if (memory_limit_mb == 0) {
    posix_spawn_file_actions_t file_actions_store;
    posix_spawn_file_actions_t *file_actions = NULL;

    if (redirects && num_redirects >= 3) {
      file_actions = &file_actions_store;
      posix_spawn_file_actions_init(file_actions);

      for (int i = 0; i < 3; i++) {
        if (!redirects[i].path) continue;
        const char *file = redirects[i].path[0] ? redirects[i].path : "/dev/null";

        if (i == 1 && redirects[2].path && redirects[1].path &&
            strcmp(redirects[1].path, redirects[2].path) == 0) {
          int err = posix_spawn_file_actions_addopen(
              file_actions, i, file, O_WRONLY | O_CREAT, 0666);
          if (err) {
            if (errmsg && errmsg_cap > 0)
              snprintf(errmsg, errmsg_cap, "posix_spawn_file_actions_addopen: %s", strerror(err));
            posix_spawn_file_actions_destroy(file_actions);
            return -1;
          }
          err = posix_spawn_file_actions_adddup2(file_actions, 1, 2);
          if (err) {
            if (errmsg && errmsg_cap > 0)
              snprintf(errmsg, errmsg_cap, "posix_spawn_file_actions_adddup2: %s", strerror(err));
            posix_spawn_file_actions_destroy(file_actions);
            return -1;
          }
          continue;
        }

        int err = posix_spawn_file_actions_addopen(
            file_actions, i, file, i == 0 ? O_RDONLY : O_WRONLY | O_CREAT, 0666);
        if (err) {
          if (errmsg && errmsg_cap > 0)
            snprintf(errmsg, errmsg_cap, "posix_spawn_file_actions_addopen: %s", strerror(err));
          posix_spawn_file_actions_destroy(file_actions);
          return -1;
        }
      }
    }

    const char *const *eff_envp = envp;
    if (!eff_envp) {
#if !CSUPPORT_USE_NSGETENVIRON
      eff_envp = (const char *const *)environ;
#else
      eff_envp = (const char *const *)*_NSGetEnviron();
#endif
    }

    int retries = 0;
    pid_t pid;
    int err;
    do {
      pid = 0;
      err = posix_spawn(&pid, program, file_actions, NULL,
                        (char **)((uintptr_t)argv),
                        (char **)((uintptr_t)eff_envp));
    } while (err == EINTR && ++retries < 8);

    if (file_actions)
      posix_spawn_file_actions_destroy(file_actions);

    if (err) {
      if (errmsg && errmsg_cap > 0)
        snprintf(errmsg, errmsg_cap, "posix_spawn failed: %s", strerror(err));
      return -1;
    }
    return (int)pid;
  }
#endif /* HAVE_POSIX_SPAWN */

  int child = fork();
  switch (child) {
  case -1:
    if (errmsg && errmsg_cap > 0)
      snprintf(errmsg, errmsg_cap, "fork failed: %s", strerror(errno));
    return -1;

  case 0: {
    if (redirects && num_redirects >= 3) {
      for (int i = 0; i < 3; i++) {
        if (!redirects[i].path) continue;

        if (i == 2 && redirects[1].path && redirects[2].path &&
            strcmp(redirects[1].path, redirects[2].path) == 0) {
          if (dup2(1, 2) == -1) _exit(126);
          continue;
        }

        char redir_err[256] = {0};
        if (csupport_redirect_io(redirects[i].path, i, redir_err, sizeof(redir_err)))
          _exit(126);
      }
    }

    if (memory_limit_mb != 0)
      csupport_set_memory_limits(memory_limit_mb);

    if (envp != NULL)
      execve(program, (char **)((uintptr_t)argv), (char **)((uintptr_t)envp));
    else
      execv(program, (char **)((uintptr_t)argv));

    _exit(errno == ENOENT ? 127 : 126);
  }

  default:
    break;
  }

  return child;
}

#else /* _WIN32 */

int csupport_redirect_io(const char *path, int fd,
                         char *errmsg, size_t errmsg_cap) {
  (void)path; (void)fd; (void)errmsg; (void)errmsg_cap;
  return -1;
}

int csupport_cmd_args_fit(size_t prog_len,
                          const char *const *args, size_t num_args) {
  (void)prog_len; (void)args; (void)num_args;
  return 1;
}

#endif /* LLVM_ON_UNIX / _WIN32 */

/* -- writeFileWithEncoding (Unix: simple write, Windows: stays in C++) -- */

#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>

int csupport_write_file_contents(const char *filename, size_t filename_len,
                                 const char *contents, size_t contents_len) {
  (void)filename_len;
  int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if (fd < 0) return errno;
  while (contents_len > 0) {
    ssize_t n = write(fd, contents, contents_len);
    if (n < 0) {
      int e = errno;
      close(fd);
      return e;
    }
    contents += n;
    contents_len -= (size_t)n;
  }
  close(fd);
  return 0;
}
#else
int csupport_write_file_contents(const char *filename, size_t filename_len,
                                 const char *contents, size_t contents_len) {
  (void)filename; (void)filename_len;
  (void)contents; (void)contents_len;
  return -1;
}
#endif
