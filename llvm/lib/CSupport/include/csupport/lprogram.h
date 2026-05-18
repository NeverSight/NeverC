#ifndef CSUPPORT_LPROGRAM_H
#define CSUPPORT_LPROGRAM_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

int csupport_change_stdin_to_binary(void);
int csupport_change_stdout_to_binary(void);
void csupport_set_memory_limits(unsigned size_mb);
int csupport_write_file_contents(const char *filename, size_t filename_len,
                                 const char *contents, size_t contents_len);

/* --- Session 11: extracted from Unix/Program.inc --- */

/* Wait for a child process (core POSIX wait4/alarm logic).
 * Returns: 1=child exited, 0=non-blocking child still running, -1=error
 * out_return_code: >=0 normal exit, -1 exec error, -2 timeout/signal
 * Time values in microseconds; pass NULL to skip. */
#ifndef _WIN32
int csupport_wait_process(int child_pid, unsigned seconds_to_wait, int polling,
                          int *out_pid, int *out_return_code,
                          int64_t *out_user_time_us,
                          int64_t *out_kernel_time_us,
                          uint64_t *out_peak_memory, char *errmsg,
                          size_t errmsg_cap);
#endif

/* Redirect fd to path (open+dup2). Returns 0 on success, -1 on error. */
int csupport_redirect_io(const char *path, int fd, char *errmsg,
                         size_t errmsg_cap);

/* Check if command line fits within system ARG_MAX.
 * Returns 1 if fits, 0 if exceeds. */
int csupport_cmd_args_fit(size_t prog_len, const char *const *args,
                          size_t num_args);

/* --- Session 13: Execute fork/exec/posix_spawn core --- */

/* Redirect info for child process I/O (stdin=0, stdout=1, stderr=2).
 * path=NULL means inherit, path="" means /dev/null. */
typedef struct {
  const char *path; /* NULL=inherit, ""=/dev/null, else file path */
} csupport_redirect_t;

/* Execute a child process using posix_spawn (if available and memlimit==0)
 * or fork+exec.
 * Returns child PID on success, -1 on error.
 * errmsg filled on error. */
#ifndef _WIN32
int csupport_execute_child(const char *program, const char *const *argv,
                           const char *const *envp,
                           const csupport_redirect_t *redirects,
                           int num_redirects, unsigned memory_limit_mb,
                           char *errmsg, size_t errmsg_cap);
#endif

#ifdef __cplusplus
}
#endif
#endif
